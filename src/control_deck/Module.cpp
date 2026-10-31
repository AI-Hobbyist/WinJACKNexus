/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Service.hpp"
#include "Sound.hpp"
#include <QJsonDocument>
#include <QUuid>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <array>
#include <cmath>
#include <vector>

#ifndef WJN_SWITCH_CONTROL
#define WJN_SWITCH_CONTROL 0
#endif

namespace SS = WinJACKNexus::ControlDeck;
namespace DSP = WinJACKNexus::Common::ControlDeck;
namespace Audio = WinJACKNexus::Audio;

namespace
{
constexpr unsigned channels = WJN_SWITCH_CHANNELS;
constexpr bool isMidi = channels == 0;
constexpr bool isSound = WJN_SWITCH_CONTROL == 1;
constexpr bool isPeak = WJN_SWITCH_CONTROL == 2;
constexpr bool isGain = WJN_SWITCH_CONTROL == 3;
constexpr unsigned inputPortCount = isSound ? 0 : (isMidi ? 1 : channels);
constexpr const char *labels[]{"L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"};
constexpr const char *modelName = WJN_SWITCH_NAME;

class SwitchNode final : public WinJACKNexus::Node::INode
{
  public:
    explicit SwitchNode(uint32_t mode) : mode_(mode)
    {
        for (unsigned direction = 0; direction < 2; ++direction)
        {
            if ((isSound && !direction) || (isPeak && direction))
                continue;
            for (unsigned channel = 0; channel < (isMidi ? 1 : channels); ++channel)
            {
                names_.push_back(QByteArray(direction ? "OUT_" : "IN_") + (isMidi          ? "MIDI"
                                                                           : channels == 1 ? "MONO"
                                                                                           : labels[channel]));
            }
        }
        SS::Endpoint endpoint;
        endpoint.id = switchId_;
        endpoint.name = name_;
        endpoint.version = modelName;
        endpoint.domain = "signalSwitch." WJN_SWITCH_KIND;
        endpoint.gate = &gate_;
        endpoint.desired = !(isSound || isPeak || isGain);
        if constexpr (isSound)
        {
            sound_ = std::make_unique<SS::Sound>();
            endpoint.sound = sound_.get();
        }
        if constexpr (isPeak)
            endpoint.peak = &peak_;
        if constexpr (isGain)
            endpoint.gainLinear = &gain_;
        endpoint.rename = [this](const QString &name) { return rename(name); };
        SS::Service::instance().attach(this, std::move(endpoint));
    }
    ~SwitchNode() override
    {
        shutdown();
        SS::Service::instance().detach(this);
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {modelName, "Control Deck", "1.0.0", "WinJACKNexus", "Control Deck node"};
    }
    bool init() noexcept override
    {
        if (mode_ != WJN_NODE_MODE_JACK)
            return false;
        try
        {
            if (client_.isActive())
                return true;
            if (!client_.open({name_.toUtf8().constData(), false, false, nullptr}))
                return false;
            name_ = QString::fromUtf8(client_.name());
            ports_.resize(names_.size());
            for (unsigned index = 0; index < names_.size(); ++index)
            {
                if (!ports_[index].registerPort(client_, names_[index].constData(),
                                                isMidi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE,
                                                index < inputPortCount ? JackPortIsInput : JackPortIsOutput))
                {
                    shutdown();
                    return false;
                }
            }
            rate_.store(client_.sampleRate());
            if (client_.setProcessCallback(&jackProcess, this) || client_.setSampleRateCallback(&rateChanged, this) ||
                !client_.activate())
            {
                shutdown();
                return false;
            }
            gate_.running.store(true, std::memory_order_release);
            return true;
        }
        catch (...)
        {
            shutdown();
            return false;
        }
    }
    void shutdown() noexcept override
    {
        client_.deactivate();
        ports_.clear();
        client_.close();
        gate_.running.store(false, std::memory_order_release);
        midi_.reset();
        if (sound_)
            sound_->stop();
        peak_ = 0;
        prepared_ = false;
    }
    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }
    void destroyWindow(HWND) noexcept override
    {
    }
    uint32_t virtualPortCount() const noexcept
    {
        return unsigned(names_.size());
    }
    bool virtualPort(uint32_t index, WjnNodePortV1 *description) const noexcept
    {
        if (!description || description->structSize != sizeof(*description) || index >= names_.size())
            return false;
        *description = {sizeof(*description),
                        index + 1,
                        uint32_t(index < inputPortCount ? WJN_NODE_PORT_INPUT : WJN_NODE_PORT_OUTPUT),
                        uint32_t(isMidi ? WJN_NODE_PORT_MIDI : WJN_NODE_PORT_AUDIO),
                        isMidi ? 0u : 1u,
                        0,
                        names_[index].constData()};
        return true;
    }
    bool virtualPrepare(const WjnNodePrepareV1 *context) noexcept
    {
        if (mode_ != WJN_NODE_MODE_VIRT || !context || context->structSize != sizeof(*context) || !context->maxFrames ||
            context->maxFrames > 65536 || !(context->sampleRate > 0))
            return false;
        maxFrames_ = context->maxFrames;
        rate_.store(context->sampleRate);
        prepared_ = true;
        midi_.reset();
        gate_.running.store(true, std::memory_order_release);
        return true;
    }
    void virtualReset() noexcept
    {
        midi_.reset();
        if (sound_)
            sound_->stop();
        peak_ = 0;
    }
    void virtualStop() noexcept
    {
        prepared_ = false;
        virtualReset();
        midi_.reset();
        gate_.running.store(false, std::memory_order_release);
    }
    void virtualProcess(WjnNodeProcessV1 *block) noexcept
    {
        if (!block || !prepared_ || block->frames > maxFrames_)
            return;
        const auto command = gate_.command.load(std::memory_order_acquire);
        if constexpr (isSound)
        {
            std::array<float *, 2> outputs{};
            for (unsigned index = 0; index < block->audioOutputCount; ++index)
            {
                const auto &port = block->audioOutputs[index];
                if (port.portId >= 1 && port.portId <= 2 && port.channels && port.samples)
                    outputs[port.portId - 1] = port.samples[0];
            }
            sound_->render(outputs[0], outputs[1], block->frames, rate_.load());
        }
        else if constexpr (isPeak)
        {
            const float *input = nullptr;
            for (unsigned index = 0; index < block->audioInputCount; ++index)
            {
                const auto &port = block->audioInputs[index];
                if (port.portId == 1 && port.channels && port.samples)
                    input = port.samples[0];
            }
            DSP::capturePeak(input, block->frames, peak_);
        }
        else if constexpr (isMidi)
        {
            WjnNodeMidiOutputV1 *output = nullptr;
            const WjnNodeMidiInputV1 *input = nullptr;
            for (unsigned i = 0; i < block->midiInputCount; ++i)
            {
                if (block->midiInputs[i].portId == 1)
                    input = &block->midiInputs[i];
            }
            for (unsigned i = 0; i < block->midiOutputCount; ++i)
            {
                auto &candidate = block->midiOutputs[i];
                candidate.eventCount = candidate.byteCount = 0;
                if (candidate.portId == 2)
                    output = &candidate;
            }
            const bool quiet = midi_.process(
                command & 1, block->frames, input && input->events ? input->eventCount : 0,
                [input](unsigned index) {
                    const auto &event = input->events[index];
                    return DSP::Event{event.frame,
                                      event.data ? std::span(event.data, event.size) : std::span<const uint8_t>{}};
                },
                [output](DSP::Event event) {
                    if (!output)
                        return false;
                    if (!output->events || !output->bytes || output->eventCount >= output->eventCapacity ||
                        event.bytes.size() > output->byteCapacity - output->byteCount)
                    {
                        ++output->dropped;
                        return false;
                    }
                    auto *target = output->bytes + output->byteCount;
                    std::copy(event.bytes.begin(), event.bytes.end(), target);
                    output->events[output->eventCount++] = {event.frame, unsigned(event.bytes.size()), target};
                    output->byteCount += unsigned(event.bytes.size());
                    return true;
                });
            if ((command & 1) || quiet)
                gate_.acknowledged.store(command, std::memory_order_release);
        }
        else
        {
            std::array<const float *, 8> inputs{};
            std::array<float *, 8> outputs{};
            for (unsigned i = 0; i < block->audioInputCount; ++i)
            {
                const auto &port = block->audioInputs[i];
                if (port.portId >= 1 && port.portId <= channels && port.channels && port.samples)
                    inputs[port.portId - 1] = port.samples[0];
            }
            for (unsigned i = 0; i < block->audioOutputCount; ++i)
            {
                const auto &port = block->audioOutputs[i];
                if (port.portId > channels && port.portId <= channels * 2 && port.channels && port.samples)
                    outputs[port.portId - channels - 1] = port.samples[0];
            }
            processAudio(command & 1, inputs.data(), outputs.data(), block->frames);
            gate_.acknowledged.store(command, std::memory_order_release);
        }
    }
    const char *settings()
    {
        const auto &endpoint = SS::Service::instance().endpoints[this];
        QJsonObject object{{"version", 1},
                           {"clientName", name_},
                           {"switchId", switchId_},
                           {"displayName", endpoint.name},
                           {"customName", endpoint.userNamed},
                           {"enabled", endpoint.desired}};
        if constexpr (isSound)
        {
            object["soundFile"] = sound_->path();
            object["shortcut"] = endpoint.shortcut;
        }
        if constexpr (isGain)
        {
            object["gainDb"] = endpoint.gainDb;
            object["gainMinimum"] = endpoint.gainMinimum;
            object["gainMaximum"] = endpoint.gainMaximum;
        }
        if constexpr (isPeak)
            object["vertical"] = endpoint.peakVertical;
        json_ = QJsonDocument(object).toJson(QJsonDocument::Compact);
        return json_.constData();
    }
    bool restore(const char *json)
    {
        if (!json)
            return false;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return false;
        const auto object = document.object();
        if (object["version"] != 1 || !object["clientName"].isString() || !object["enabled"].isBool())
            return false;
        if (object.contains("switchId") &&
            (!object["switchId"].isString() || QUuid(object["switchId"].toString()).isNull()))
            return false;
        if (object.contains("displayName") &&
            (!object["displayName"].isString() || object["displayName"].toString().trimmed().isEmpty()))
            return false;
        if (object.contains("customName") && !object["customName"].isBool())
            return false;
        if (isPeak && object.contains("vertical") && !object["vertical"].isBool())
            return false;
        if constexpr (isSound)
            if (!object["soundFile"].isString() || !object["shortcut"].isString())
                return false;
        if constexpr (isGain)
        {
            const double low = object["gainMinimum"].toDouble(-1000);
            const double high = object["gainMaximum"].toDouble(1000);
            const double db = object["gainDb"].toDouble(1000);
            if (!object["gainDb"].isDouble() || !object["gainMinimum"].isDouble() ||
                !object["gainMaximum"].isDouble() || low < -720 || high > 720 || low >= high || db < low || db > high)
                return false;
        }
        if (!rename(object["clientName"].toString()))
            return false;
        auto &service = SS::Service::instance();
        bool copy = false;
        if (object.contains("switchId"))
        {
            const auto savedId = object["switchId"].toString();
            const auto *existing = service.find(savedId);
            copy = existing && existing != &service.endpoints[this];
            // A copy receives a fresh identity; a destroyed/recreated instance restores its ID.
            if (!existing || existing == &service.endpoints[this])
            {
                switchId_ = savedId;
                service.bind(this, savedId, service.endpoints[this].name, service.endpoints[this].context);
            }
        }
        if (object["displayName"].isString())
            service.endpoints[this].name = object["displayName"].toString();
        service.endpoints[this].userNamed = object["customName"].toBool(object["displayName"].isString() &&
                                                                        object["displayName"].toString() != modelName);
        if constexpr (isSound)
        {
            auto &endpoint = service.endpoints[this];
            endpoint.shortcut = copy ? QString() : object["shortcut"].toString();
            const auto path = object["soundFile"].toString();
            if (!path.isEmpty())
                sound_->load(path, true);
            service.notify();
            return true;
        }
        if constexpr (isGain)
        {
            return service.setGainRange(switchId_, object["gainMinimum"].toDouble(),
                                        object["gainMaximum"].toDouble()) &&
                   service.setGain(switchId_, object["gainDb"].toDouble());
        }
        if constexpr (isPeak)
        {
            service.endpoints[this].peakVertical = object["vertical"].toBool();
            service.notify();
            return true;
        }
        return service.restoreDesired(this, copy || object["enabled"].toBool());
    }
    const char *clientId()
    {
        nameBytes_ = name_.toUtf8();
        return nameBytes_.constData();
    }

  private:
    uint32_t mode_;
    QString switchId_{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    QString name_{QString::fromLatin1(modelName)};
    QByteArray nameBytes_, json_;
    std::vector<QByteArray> names_;
    std::vector<Audio::JackPort> ports_;
    Audio::JackClient client_;
    DSP::Gate gate_;
    DSP::Midi midi_;
    std::unique_ptr<SS::Sound> sound_;
    std::atomic<float> peak_{};
    std::atomic<float> gain_{1};
    std::atomic<double> rate_{48000};
    bool prepared_{};
    unsigned maxFrames_{};

    static int rateChanged(jack_nframes_t rate, void *user) noexcept
    {
        static_cast<SwitchNode *>(user)->rate_.store(rate);
        return 0;
    }

    void processAudio(bool enabled, const float *const *inputs, float *const *outputs, unsigned frames) noexcept
    {
        if constexpr (isGain)
        {
            DSP::audioGain(gain_.load(std::memory_order_relaxed), inputs, outputs, channels, frames);
        }
        else
        {
            DSP::audio(enabled, inputs, outputs, channels, frames);
        }
    }

    bool rename(const QString &name)
    {
        const auto trimmed = name.trimmed();
        if (trimmed.isEmpty() || trimmed.toUtf8().size() >= 64 || trimmed.contains(':'))
            return false;
        for (const auto character : trimmed)
        {
            if (character.unicode() < 32 || character.unicode() == 127)
                return false;
        }
        // Display renames never recreate a running JACK client or its connections.
        if (!client_.isActive())
            name_ = trimmed;
        auto &endpoint = SS::Service::instance().endpoints[this];
        endpoint.name = trimmed;
        endpoint.userNamed = true;
        SS::Service::instance().notify(true);
        return true;
    }
    static int jackProcess(jack_nframes_t frames, void *context) noexcept
    {
        auto &node = *static_cast<SwitchNode *>(context);
        const auto command = node.gate_.command.load(std::memory_order_acquire);
        if constexpr (isSound)
        {
            node.sound_->render(node.ports_[0].audioBuffer(frames), node.ports_[1].audioBuffer(frames), frames,
                                node.rate_.load());
        }
        else if constexpr (isPeak)
        {
            DSP::capturePeak(node.ports_[0].audioBuffer(frames), frames, node.peak_);
        }
        else if constexpr (isMidi)
        {
            auto &api = Audio::JackApi::instance();
            auto *input = node.ports_[0].midiBuffer(frames);
            auto *output = node.ports_[1].midiBuffer(frames);
            if (output)
                api.midiClearBuffer(output);
            const bool quiet = node.midi_.process(
                command & 1, frames, input ? api.midiEventCount(input) : 0,
                [&](unsigned index) {
                    jack_midi_event_t event{};
                    if (api.midiEventGet(&event, input, index))
                        return DSP::Event{};
                    return DSP::Event{event.time, {event.buffer, event.size}};
                },
                [&](DSP::Event event) {
                    return output &&
                           api.midiEventWrite(output, event.frame, event.bytes.data(), event.bytes.size()) == 0;
                });
            if ((command & 1) || quiet)
                node.gate_.acknowledged.store(command, std::memory_order_release);
        }
        else
        {
            std::array<const float *, 8> inputs{};
            std::array<float *, 8> outputs{};
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                inputs[channel] = node.ports_[channel].audioBuffer(frames);
                outputs[channel] = node.ports_[channels + channel].audioBuffer(frames);
            }
            node.processAudio(command & 1, inputs.data(), outputs.data(), frames);
            node.gate_.acknowledged.store(command, std::memory_order_release);
        }
        return 0;
    }
};
} // namespace

const char *wjn_get_node_api_version()
{
    return "4";
}
const char *wjn_get_node_name()
{
    return modelName;
}
const char *wjn_get_node_category()
{
    return "Control Deck";
}

uint32_t wjn_get_node_i18n_api_version()
{
    return WJN_NODE_I18N_API_VERSION;
}
const char *wjn_get_node_i18n_descriptor_json()
{
    return "{\"descriptorVersion\":1,\"i18nApiVersion\":1,"
           "\"moduleId\":\"winjacknexus-signal-switch-" WJN_SWITCH_KIND "\","
           "\"domains\":[\"signalSwitch." WJN_SWITCH_KIND "\"],\"locatePath\":\"locate\",\"catalogVersion\":1,"
           "\"displayNameKey\":{\"domain\":\"signalSwitch." WJN_SWITCH_KIND "\",\"key\":\"name\"}}";
}
namespace
{
const WjnI18nHostV1 *languageHost{};
}
int32_t wjn_i18n_attach_host(const WjnI18nHostV1 *host)
{
    if (!host || host->structSize < sizeof(*host) || host->apiVersion != 1 || !host->text ||
        (languageHost && languageHost != host))
        return WJN_I18N_INVALID_ARGUMENT;
    languageHost = host;
    return WJN_I18N_OK;
}
void wjn_i18n_language_changed(const char *, uint64_t)
{
}
void wjn_i18n_detach_host()
{
    languageHost = nullptr;
}
uint32_t wjn_get_node_theme_api_version()
{
    return WJN_NODE_THEME_API_VERSION;
}
const char *wjn_get_node_theme_namespace_json()
{
    return "{\"descriptorVersion\":1,\"themeApiVersion\":1,"
           "\"moduleId\":\"module-winjacknexus-signal-switch-" WJN_SWITCH_KIND "\","
           "\"namespace\":\"--wjn-theme-module-winjacknexus-signal-switch-" WJN_SWITCH_KIND "-\","
           "\"displayName\":\"" WJN_SWITCH_NAME "\",\"tokens\":[{"
           "\"name\":\"--wjn-theme-module-winjacknexus-signal-switch-" WJN_SWITCH_KIND "-node-card-background\","
           "\"type\":\"color\",\"default\":\"var(--wjn-theme-patchbay-node-card-background)\","
           "\"description\":\"Signal Switch graph node background\"}]}";
}
WinJACKNexus::Node::INode *wjn_create_node()
{
    return new SwitchNode(WJN_NODE_MODE_JACK);
}
void wjn_destroy_node(WinJACKNexus::Node::INode *node)
{
    delete node;
}
const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *node)
{
    return node ? static_cast<SwitchNode *>(node)->settings() : nullptr;
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode *node, const char *json)
{
    return node && static_cast<SwitchNode *>(node)->restore(json);
}
extern "C" WINJACK_NODE_API const char *wjn_get_node_client_id(WinJACKNexus::Node::INode *node)
{
    return node ? static_cast<SwitchNode *>(node)->clientId() : nullptr;
}
#define WJN_NODE_MODULE_TYPE SwitchNode
#define WJN_NODE_MODULE_FEATURES 0
#include <WinJACKNexus/Node/DualModeModuleV4.inc>
