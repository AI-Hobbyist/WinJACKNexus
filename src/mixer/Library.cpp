/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include "EffectGraph.hpp"
#include <QPointer>
#include <QMessageBox>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <memory>

namespace {
std::atomic_bool occupied { false };

class DigitalMixerNode final : public WinJACKNexus::Node::INode {
public:
    ~DigitalMixerNode() override
    {
        shutdown();
        occupied.store(false, std::memory_order_release);
    }

    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { "Digital Mixer", "Digital Mixer", "0.1.0", "WinJACKNexus",
                 "Single-instance Digital Mixer node." };
    }

    bool init() noexcept override
    {
        if (initialized_) return true;
        if (!state_.validate() || !client_.open({"DigitalMixer", false, true, nullptr})) return false;
        sampleRate_.store(client_.sampleRate(), std::memory_order_release);
        if (!processor_.configure(state_, std::max<jack_nframes_t>(1, client_.bufferSize()),
                                  nullptr, client_.sampleRate())) {
            client_.close();
            return false;
        }
        configuredSampleRate_.store(processor_.sampleRate(), std::memory_order_release);
        if (!effects_.configure(state_, client_.sampleRate(), nullptr)) {
            client_.close();
            return false;
        }
        processor_.setEffectProcessor(this, &DigitalMixerNode::processEffect);
        meterSlots_ = makeMeterSlots(state_, meterCount_);
        if (client_.setProcessCallback(&DigitalMixerNode::process, this) != 0
            || client_.setBufferSizeCallback(&DigitalMixerNode::bufferSizeChanged, this) != 0
            || client_.setSampleRateCallback(&DigitalMixerNode::sampleRateChanged, this) != 0
            || !replacePorts(state_.ports()) || !bindPortBuffers(state_.ports()) || !client_.activate()) {
            ports_.clear();
            client_.close();
            return false;
        }
        initialized_ = true;
        try {
            maintenance_ = std::jthread([this](std::stop_token stop) { maintainBuffers(stop); });
        } catch (...) {
            shutdown();
            return false;
        }
        return true;
    }

    void shutdown() noexcept override
    {
        if (maintenance_.joinable()) {
            maintenance_.request_stop();
            maintenance_.join();
        }
        client_.deactivate();
        ports_.clear();
        client_.close();
        effects_ = WinJACKNexus::Mixer::EffectGraph {};
        destroyWindow(nullptr);
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        if (!initialized_) return nullptr;
        if (window_.isNull()) {
            window_ = new WinJACKNexus::UI::MixerWindow(&state_,
                [this](const WinJACKNexus::Common::DigitalMixerState& candidate, QString* error) {
                    return commitState(candidate, error);
                }, [this](std::uint64_t id, bool post) { return meterPeak(id, post); },
                [this](std::uint64_t source, std::uint64_t target,
                       WinJACKNexus::Common::MixerPolarSnapshot& snapshot) {
                    std::lock_guard controlLock(controlMutex_);
                    return processor_.readPolar(source, target, snapshot);
                }, [this](std::uint64_t id, bool post, std::size_t channel) {
                    return channelMeterPeak(id, post, channel);
                }, [this](std::uint64_t id, std::size_t index, float input[8],
                          float output[8], float reduction[8], void* spectrum,
                          std::uint32_t bytes) {
                    std::lock_guard controlLock(controlMutex_);
                    return effects_.readLiveAnalysis(id, index, input, output,
                                                     reduction, spectrum, bytes);
                });
            window_->setAttribute(Qt::WA_DeleteOnClose);
        }
        window_->show();
        showEffectWarning();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        if (!window_.isNull()) {
            window_->close();
            // WA_DeleteOnClose may defer destruction. The DLL must not unload
            // while a queued widget still has its vtable in this module.
            if (!window_.isNull()) delete window_.data();
        }
        window_.clear();
    }

    const char* settingsJson()
    {
        serialized_ = state_.toJson();
        return serialized_.constData();
    }

    bool restore(const char* json)
    {
        if (!json) return false;
        auto candidate = state_;
        if (!candidate.loadJson(QByteArray(json))) return false;
        return commitState(candidate, nullptr);
    }

    const char* clientId() const noexcept { return client_.name(); }

#if defined(_DEBUG)
    void failRegistrationAfter(int successfulRegistrations) noexcept
    {
        registrationFailureCountdown_ = successfulRegistrations;
    }
#endif

private:
    bool commitState(const WinJACKNexus::Common::DigitalMixerState& candidate, QString* error)
    {
        std::lock_guard controlLock(controlMutex_);
        if (!candidate.validate(error)) return false;
        WinJACKNexus::Common::DigitalMixerProcessor nextProcessor;
        const auto capacity = client_.isOpen()
            ? std::max<std::size_t>(1, client_.bufferSize()) : 1024;
        if (!nextProcessor.configure(candidate, capacity, error,
                                     sampleRate_.load(std::memory_order_acquire))) return false;
        const bool reuseEffects = effects_.sameTopology(candidate)
            && processor_.sampleRate() == sampleRate_.load(std::memory_order_acquire);
        WinJACKNexus::Mixer::EffectGraph nextEffects;
        if (reuseEffects) {
            if (!effects_.validateSnapshots(candidate, error)) return false;
        } else if (!nextEffects.configure(candidate,
                       sampleRate_.load(std::memory_order_acquire), error)) return false;
        std::size_t nextMeterCount = 0;
        auto nextMeters = makeMeterSlots(candidate, nextMeterCount);
        const bool sameMeters = nextMeterCount == meterCount_
            && std::equal(nextMeters.get(), nextMeters.get() + nextMeterCount,
                          meterSlots_.get(), [](const MeterSlot& next, const MeterSlot& current) {
                              return next.id == current.id;
                          });
        if (!sameMeters) {
            for (std::size_t next = 0; next < nextMeterCount; ++next) {
                for (std::size_t current = 0; current < meterCount_; ++current) {
                    if (nextMeters[next].id != meterSlots_[current].id) continue;
                    nextMeters[next].input.store(meterSlots_[current].input.load(std::memory_order_acquire),
                                                std::memory_order_relaxed);
                    nextMeters[next].post.store(meterSlots_[current].post.load(std::memory_order_acquire),
                                               std::memory_order_relaxed);
                    for (std::size_t channel = 0; channel < 8; ++channel) {
                        nextMeters[next].channelInput[channel].store(
                            meterSlots_[current].channelInput[channel].load(std::memory_order_acquire),
                            std::memory_order_relaxed);
                        nextMeters[next].channelPost[channel].store(
                            meterSlots_[current].channelPost[channel].load(std::memory_order_acquire),
                            std::memory_order_relaxed);
                    }
                    break;
                }
            }
        }
        const auto desired = candidate.ports();
        const auto portsUnchanged = [this, &desired] {
            std::size_t input = 0, output = 0;
            for (const auto& wanted : desired) {
                const auto& boundSlots = wanted.direction == WinJACKNexus::Common::MixerPortDirection::input
                    ? inputSlots_ : outputSlots_;
                const auto index = wanted.direction == WinJACKNexus::Common::MixerPortDirection::input
                    ? input++ : output++;
                if (index >= boundSlots.size()) return false;
                const auto& current = boundSlots[index]->descriptor;
                if (current.stripId != wanted.stripId || current.name != wanted.name
                    || current.direction != wanted.direction
                    || current.channelIndex != wanted.channelIndex) return false;
            }
            return input == inputSlots_.size() && output == outputSlots_.size();
        }();
        if (initialized_) {
            if (portsUnchanged) {
                // Parameter edits only need a short processor swap; keep JACK ports bound.
                switchingPorts_.store(true, std::memory_order_release);
                while (callbacksInFlight_.load(std::memory_order_acquire) != 0)
                    std::this_thread::yield();
            } else {
                if (!replacePorts(desired, true)) {
                    switchingPorts_.store(false, std::memory_order_release);
                    if (error) *error = QStringLiteral("JACK 端口注册失败，旧拓扑已保留。");
                    return false;
                }
                if (!bindPortBuffers(desired)) {
                    switchingPorts_.store(false, std::memory_order_release);
                    if (error) *error = QStringLiteral("JACK 端口映射失败。");
                    return false;
                }
            }
        }
        if (initialized_) {
            nextProcessor.adoptGainHistory(processor_);
            nextProcessor.adoptPolarHistory(processor_);
        }
        if (reuseEffects) effects_.applySnapshots(candidate);
        else std::swap(effects_, nextEffects);
        nextProcessor.setEffectProcessor(this, &DigitalMixerNode::processEffect);
        processor_ = std::move(nextProcessor);
        configuredSampleRate_.store(processor_.sampleRate(), std::memory_order_release);
        if (!sameMeters) {
            meterSlots_ = std::move(nextMeters);
            meterCount_ = nextMeterCount;
        }
        state_ = candidate;
        switchingPorts_.store(false, std::memory_order_release);
        showEffectWarning();
        return true;
    }

    void showEffectWarning()
    {
        if (window_.isNull()) return;
        const auto& warning = effects_.warning();
        if (warning.isEmpty()) { lastEffectWarning_.clear(); return; }
        if (warning == lastEffectWarning_) return;
        lastEffectWarning_ = warning;
        auto* notice = new QMessageBox(QMessageBox::Warning,
            QStringLiteral("FX 模块已旁通"), warning, QMessageBox::Ok, window_);
        notice->setAttribute(Qt::WA_DeleteOnClose);
        notice->show();
    }

    struct PortSlot final {
        WinJACKNexus::Common::MixerPortDescriptor descriptor;
        WinJACKNexus::Audio::JackPort port;
    };

    struct MeterSlot final {
        std::uint64_t id {};
        std::atomic<float> input { 0.0F };
        std::atomic<float> post { 0.0F };
        std::array<std::atomic<float>, 8> channelInput {};
        std::array<std::atomic<float>, 8> channelPost {};
    };

    static std::unique_ptr<MeterSlot[]> makeMeterSlots(
        const WinJACKNexus::Common::DigitalMixerState& state, std::size_t& count)
    {
        count = state.inputs().size() + state.auxiliaries().size() + state.subMixes().size() + 2;
        auto meters = std::make_unique<MeterSlot[]>(count);
        std::size_t index = 0;
        for (const auto& strip : state.inputs()) meters[index++].id = strip.id;
        for (const auto& strip : state.auxiliaries()) meters[index++].id = strip.id;
        for (const auto& strip : state.subMixes()) meters[index++].id = strip.id;
        meters[index++].id = 0;
        meters[index].id = state.main().id;
        return meters;
    }

    float meterPeak(std::uint64_t id, bool post) const noexcept
    {
        for (std::size_t i = 0; i < meterCount_; ++i)
            if (meterSlots_[i].id == id)
                return (post ? meterSlots_[i].post : meterSlots_[i].input)
                    .load(std::memory_order_acquire);
        return 0.0F;
    }

    float channelMeterPeak(std::uint64_t id, bool post, std::size_t channel) const noexcept
    {
        if (channel >= 8) return 0.0F;
        for (std::size_t i = 0; i < meterCount_; ++i)
            if (meterSlots_[i].id == id)
                return (post ? meterSlots_[i].channelPost[channel]
                             : meterSlots_[i].channelInput[channel]).load(std::memory_order_acquire);
        return 0.0F;
    }

    static int bufferSizeChanged(jack_nframes_t frames, void* context) noexcept
    {
        static_cast<DigitalMixerNode*>(context)->requestedBufferSize_.store(frames, std::memory_order_release);
        return 0;
    }

    static int sampleRateChanged(jack_nframes_t rate, void* context) noexcept
    {
        static_cast<DigitalMixerNode*>(context)->sampleRate_.store(rate, std::memory_order_release);
        return 0;
    }

    void maintainBuffers(std::stop_token stop) noexcept
    {
        while (!stop.stop_requested()) {
            const auto requested = requestedBufferSize_.exchange(0, std::memory_order_acq_rel);
            const auto rate = sampleRate_.load(std::memory_order_acquire);
            if (requested > 0 || (rate > 0
                && rate != configuredSampleRate_.load(std::memory_order_acquire))) {
                std::lock_guard controlLock(controlMutex_);
                if ((requested > 0 && requested != processor_.maxFrames())
                    || rate != processor_.sampleRate()) {
                    WinJACKNexus::Common::DigitalMixerProcessor next;
                    const auto capacity = requested > 0 ? requested : processor_.maxFrames();
                    if (next.configure(state_, capacity, nullptr, rate)) {
                        WinJACKNexus::Mixer::EffectGraph nextEffects;
                        const bool replaceEffects = rate != processor_.sampleRate();
                        if (replaceEffects && !nextEffects.configure(state_, rate, nullptr)) continue;
                        switchingPorts_.store(true, std::memory_order_release);
                        while (callbacksInFlight_.load(std::memory_order_acquire) != 0)
                            std::this_thread::yield();
                        next.adoptGainHistory(processor_);
                        next.adoptPolarHistory(processor_);
                        if (replaceEffects) std::swap(effects_, nextEffects);
                        next.setEffectProcessor(this, &DigitalMixerNode::processEffect);
                        processor_ = std::move(next);
                        configuredSampleRate_.store(processor_.sampleRate(), std::memory_order_release);
                        switchingPorts_.store(false, std::memory_order_release);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    static int process(jack_nframes_t frames, void* context) noexcept
    {
        auto* self = static_cast<DigitalMixerNode*>(context);
        self->callbacksInFlight_.fetch_add(1, std::memory_order_acq_rel);
        if (self->switchingPorts_.load(std::memory_order_acquire)) {
            self->callbacksInFlight_.fetch_sub(1, std::memory_order_release);
            return 0;
        }
        for (std::size_t i = 0; i < self->inputSlots_.size(); ++i)
            self->inputBuffers_[i] = self->inputSlots_[i]->port.audioBuffer(frames);
        for (std::size_t i = 0; i < self->outputSlots_.size(); ++i)
            self->outputBuffers_[i] = self->outputSlots_[i]->port.audioBuffer(frames);
        if (!self->processor_.process(self->inputBuffers_, self->outputBuffers_, frames)) {
            for (auto* output : self->outputBuffers_)
                if (output) std::fill_n(output, frames, 0.0F);
            for (std::size_t i = 0; i < self->meterCount_; ++i) {
                self->meterSlots_[i].input.store(0.0F, std::memory_order_release);
                self->meterSlots_[i].post.store(0.0F, std::memory_order_release);
                for (std::size_t channel = 0; channel < 8; ++channel) {
                    self->meterSlots_[i].channelInput[channel].store(0.0F, std::memory_order_release);
                    self->meterSlots_[i].channelPost[channel].store(0.0F, std::memory_order_release);
                }
            }
        } else {
            const auto& levels = self->processor_.levels();
            for (std::size_t i = 0; i < self->meterCount_; ++i) {
                self->meterSlots_[i].input.store(levels[i].inputPeak, std::memory_order_release);
                self->meterSlots_[i].post.store(levels[i].postPeak, std::memory_order_release);
                for (std::size_t channel = 0; channel < 8; ++channel) {
                    self->meterSlots_[i].channelInput[channel].store(
                        levels[i].channelInputPeaks[channel], std::memory_order_release);
                    self->meterSlots_[i].channelPost[channel].store(
                        levels[i].channelPostPeaks[channel], std::memory_order_release);
                }
            }
        }
        self->callbacksInFlight_.fetch_sub(1, std::memory_order_release);
        return 0;
    }

    static void processEffect(void* context, std::uint64_t stripId, std::uint32_t channel,
                              float* samples, std::size_t frames) noexcept
    {
        static_cast<DigitalMixerNode*>(context)->effects_.process(stripId, channel, samples, frames);
    }

    bool bindPortBuffers(const std::vector<WinJACKNexus::Common::MixerPortDescriptor>& desired)
    {
        std::vector<PortSlot*> inputs, outputs;
        for (const auto& descriptor : desired) {
            const auto match = std::find_if(ports_.begin(), ports_.end(), [&descriptor](const PortSlot& slot) {
                return slot.descriptor.stripId == descriptor.stripId
                    && slot.descriptor.direction == descriptor.direction
                    && slot.descriptor.channelIndex == descriptor.channelIndex;
            });
            if (match == ports_.end()) return false;
            (descriptor.direction == WinJACKNexus::Common::MixerPortDirection::input
                ? inputs : outputs).push_back(&*match);
        }
        inputSlots_ = std::move(inputs);
        outputSlots_ = std::move(outputs);
        inputBuffers_.resize(inputSlots_.size());
        outputBuffers_.resize(outputSlots_.size());
        return true;
    }

    bool replacePorts(const std::vector<WinJACKNexus::Common::MixerPortDescriptor>& desired,
                      bool keepPaused = false)
    {
        if (!client_.isOpen()) return false;
        const bool wasActive = client_.isActive();
        if (wasActive) {
            switchingPorts_.store(true, std::memory_order_release);
            while (callbacksInFlight_.load(std::memory_order_acquire) != 0)
                std::this_thread::yield();
        }
        const auto resume = [this, wasActive, keepPaused] {
            if (wasActive && !keepPaused) switchingPorts_.store(false, std::memory_order_release);
        };
        const auto samePort = [](const auto& left, const auto& right) {
            return left.stripId == right.stripId && left.direction == right.direction
                && left.channelIndex == right.channelIndex;
        };
        struct Rename {
            std::size_t index;
            QString oldName;
            QString temporaryName;
            QString newName;
            bool finalNameSet {};
        };
        std::vector<Rename> renames;
        for (std::size_t index = 0; index < ports_.size(); ++index) {
            const auto wanted = std::find_if(desired.cbegin(), desired.cend(), [&, index](const auto& item) {
                return samePort(ports_[index].descriptor, item);
            });
            if (wanted == desired.cend() || wanted->name == ports_[index].descriptor.name) continue;
            const auto temporary = QStringLiteral("__wjn_tmp_%1_%2")
                .arg(static_cast<qulonglong>(reinterpret_cast<std::uintptr_t>(this)), 0, 16)
                .arg(static_cast<qulonglong>(index));
            renames.push_back({index, ports_[index].descriptor.name, temporary, wanted->name});
        }
        const auto rollback = [&] {
            // Free all final names first, including when two existing ports swap names.
            for (auto& rename : renames) {
                if (!rename.finalNameSet) continue;
                if (!ports_[rename.index].port.rename(rename.temporaryName.toUtf8().constData()))
                    WJN_LOG_ERROR("Unable to restore temporary mixer port name");
                rename.finalNameSet = false;
            }
            for (auto& rename : renames) {
                const auto current = QString::fromUtf8(ports_[rename.index].port.shortName());
                if (current != rename.temporaryName) continue;
                if (!ports_[rename.index].port.rename(rename.oldName.toUtf8().constData()))
                    WJN_LOG_ERROR("Unable to restore original mixer port name");
            }
        };
        for (auto& rename : renames) {
            if (ports_[rename.index].port.rename(rename.temporaryName.toUtf8().constData())) continue;
            rollback();
            resume();
            return false;
        }
        std::vector<PortSlot> added;
        for (const auto& descriptor : desired) {
            const bool exists = std::any_of(ports_.cbegin(), ports_.cend(), [&descriptor, &samePort](const auto& slot) {
                return samePort(slot.descriptor, descriptor);
            });
            if (exists) continue;
#if defined(_DEBUG)
            if (registrationFailureCountdown_ == 0) {
                registrationFailureCountdown_ = -1;
                added.clear();
                rollback();
                resume();
                return false;
            }
            if (registrationFailureCountdown_ > 0) --registrationFailureCountdown_;
#endif
            PortSlot slot;
            slot.descriptor = descriptor;
            const auto utf8 = descriptor.name.toUtf8();
            const auto flags = descriptor.direction == WinJACKNexus::Common::MixerPortDirection::input
                ? JackPortIsInput : JackPortIsOutput;
            if (!slot.port.registerPort(client_, utf8.constData(), JACK_DEFAULT_AUDIO_TYPE, flags)) {
                added.clear();
                rollback();
                resume();
                return false;
            }
            added.push_back(std::move(slot));
        }
        for (auto& rename : renames) {
            if (!ports_[rename.index].port.rename(rename.newName.toUtf8().constData())) {
                added.clear();
                rollback();
                resume();
                return false;
            }
            rename.finalNameSet = true;
        }
        // Keep each native JACK port for the same strip and channel. Renaming the
        // native port preserves its existing connections.
        ports_.erase(std::remove_if(ports_.begin(), ports_.end(), [&desired](const auto& slot) {
            return std::none_of(desired.cbegin(), desired.cend(), [&slot](const auto& wanted) {
                return wanted.stripId == slot.descriptor.stripId
                    && wanted.direction == slot.descriptor.direction
                    && wanted.channelIndex == slot.descriptor.channelIndex;
            });
        }), ports_.end());
        for (auto& slot : ports_) {
            const auto wanted = std::find_if(desired.cbegin(), desired.cend(), [&](const auto& item) {
                return samePort(slot.descriptor, item);
            });
            slot.descriptor = *wanted;
        }
        for (auto& slot : added) ports_.push_back(std::move(slot));
        resume();
        return true;
    }

    WinJACKNexus::Common::DigitalMixerState state_;
    WinJACKNexus::Common::DigitalMixerProcessor processor_;
    WinJACKNexus::Mixer::EffectGraph effects_;
    WinJACKNexus::Audio::JackClient client_;
    std::vector<PortSlot> ports_;
    std::vector<PortSlot*> inputSlots_;
    std::vector<PortSlot*> outputSlots_;
    std::vector<const float*> inputBuffers_;
    std::vector<float*> outputBuffers_;
    std::unique_ptr<MeterSlot[]> meterSlots_;
    std::size_t meterCount_ {};
    std::atomic_bool switchingPorts_ { false };
    std::atomic_uint32_t callbacksInFlight_ { 0 };
    std::atomic<jack_nframes_t> requestedBufferSize_ { 0 };
    std::atomic<jack_nframes_t> sampleRate_ { 0 };
    std::atomic<jack_nframes_t> configuredSampleRate_ { 0 };
    std::mutex controlMutex_;
    std::jthread maintenance_;
#if defined(_DEBUG)
    int registrationFailureCountdown_ { -1 };
#endif
    QByteArray serialized_;
    QString lastEffectWarning_;
    QPointer<WinJACKNexus::UI::MixerWindow> window_;
    bool initialized_ {};
};
}

const char* wjn_get_node_api_version(void) { return "2"; }
std::uint32_t wjn_get_node_theme_api_version(void) { return WJN_NODE_THEME_API_VERSION; }
const char* wjn_get_node_theme_namespace_json(std::uint32_t index)
{
    static constexpr char descriptor[] = R"json({
        "descriptorVersion":1,"themeApiVersion":1,
        "moduleId":"module-winjacknexus-digital-mixer",
        "namespace":"--wjn-theme-module-winjacknexus-digital-mixer-",
        "displayName":"Digital Mixer",
        "tokens":[{
            "name":"--wjn-theme-module-winjacknexus-digital-mixer-node-card-background",
            "type":"color",
            "default":"var(--wjn-theme-patchbay-node-card-background)",
            "description":"Host-drawn card background for the Digital Mixer node type"
        }]
    })json";
    return index == 0 ? descriptor : nullptr;
}
std::uint32_t wjn_get_node_count(void) { return 1; }
const char* wjn_get_node_name(std::uint32_t index) { return index == 0 ? "Digital Mixer" : ""; }
const char* wjn_get_node_category(std::uint32_t index) { return index == 0 ? "Digital Mixer" : ""; }
WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    if (index != 0 || occupied.exchange(true, std::memory_order_acq_rel)) return nullptr;
    try { return new DigitalMixerNode; }
    catch (...) { occupied.store(false, std::memory_order_release); return nullptr; }
}
void wjn_destroy_node(WinJACKNexus::Node::INode* node) { delete node; }
void* wjn_create_settings_view(WinJACKNexus::Node::INode*, void*) { return nullptr; }
void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*) {}
QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode*, QWidget*) { return nullptr; }
void wjn_destroy_settings_widget(WinJACKNexus::Node::INode*, QWidget*) {}
const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node)
{
    auto* mixer = dynamic_cast<DigitalMixerNode*>(node);
    return mixer ? mixer->settingsJson() : "";
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node, const char* json)
{
    auto* mixer = dynamic_cast<DigitalMixerNode*>(node);
    return mixer && mixer->restore(json) ? 1 : 0;
}
extern "C" WINJACK_NODE_API const char* wjn_get_node_client_id(WinJACKNexus::Node::INode* node)
{
    auto* mixer = dynamic_cast<DigitalMixerNode*>(node);
    return mixer ? mixer->clientId() : nullptr;
}
#if defined(_DEBUG)
extern "C" WINJACK_NODE_API void wjn_digital_mixer_fail_registration_after(
    WinJACKNexus::Node::INode* node, int successfulRegistrations)
{
    if (auto* mixer = dynamic_cast<DigitalMixerNode*>(node))
        mixer->failRegistrationAfter(successfulRegistrations);
}
#endif
