/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/NativeFx/I18n.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#if WJN_FX_MODULE_INDEX <= 7
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#endif
#if WJN_FX_MODULE_INDEX == 0
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 1
#include <WinJACKNexus/NativeFx/graphic_eq/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 2
#include <WinJACKNexus/NativeFx/deesser/Ui.hpp>
#include <WinJACKNexus/NativeFx/deesser/SpectrumSample.hpp>
#elif WJN_FX_MODULE_INDEX == 3
#include <WinJACKNexus/NativeFx/compressor/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 4
#include <WinJACKNexus/NativeFx/limiter/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 5
#include <WinJACKNexus/NativeFx/delay/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 6
#include <WinJACKNexus/NativeFx/parametric_eq/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 7
#include <WinJACKNexus/NativeFx/reverb/Ui.hpp>
#endif
#include <QPointer>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace UI = WinJACKNexus::UI;
namespace Common = WinJACKNexus::Common;

#ifndef WJN_FX_MODULE_INDEX
#error Each Native Fx DLL must declare WJN_FX_MODULE_INDEX
#endif
#if WJN_FX_MODULE_INDEX == 0
#include "../effects/gate/Meta.hpp"
#include "../effects/gate/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 1
#include "../effects/graphic_eq/Meta.hpp"
#include "../effects/graphic_eq/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 2
#include "../effects/deesser/Meta.hpp"
#include "../effects/deesser/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 3
#include "../effects/compressor/Meta.hpp"
#include "../effects/compressor/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 4
#include "../effects/limiter/Meta.hpp"
#include "../effects/limiter/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 5
#include "../effects/delay/Meta.hpp"
#include "../effects/delay/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 6
#include "../effects/parametric_eq/Meta.hpp"
#include "../effects/parametric_eq/include/Dsp.hpp"
#elif WJN_FX_MODULE_INDEX == 7
#include "../effects/reverb/Meta.hpp"
#include "../effects/reverb/include/Dsp.hpp"
#else
#error Invalid Native Fx module index
#endif

#if WJN_FX_MODULE_INDEX == 0
using ActiveDsp = GateDsp;
#elif WJN_FX_MODULE_INDEX == 1
using ActiveDsp = GraphicEqDsp;
#elif WJN_FX_MODULE_INDEX == 2
using ActiveDsp = DeEsserDsp;
#elif WJN_FX_MODULE_INDEX == 3
using ActiveDsp = CompressorDsp;
#elif WJN_FX_MODULE_INDEX == 4
using ActiveDsp = LimiterDsp;
#elif WJN_FX_MODULE_INDEX == 5
using ActiveDsp = DelayDsp;
#elif WJN_FX_MODULE_INDEX == 6
using ActiveDsp = ParametricEqDsp;
#elif WJN_FX_MODULE_INDEX == 7
using ActiveDsp = ReverbDsp;
#endif

struct WjnFxInstance
{
    virtual ~WjnFxInstance() = default;
    virtual WjnFxInstance *clone() const = 0;
    virtual void prepare(double) = 0;
    virtual void process(uint32_t, const float *, float *, uint32_t) = 0;
    virtual void setChannel(int) = 0;
    virtual void setTheme(const UI::Controls::Palette &) = 0;
    virtual QWidget *createEditor(QWidget *) = 0;
    virtual int currentPreset() const = 0;
    virtual void selectPreset(int) = 0;
    virtual bool exportSnapshot(void *, uint32_t) const = 0;
    virtual bool importSnapshot(const void *, uint32_t) = 0;
    virtual void setChangeCallback(WjnFxParametersChanged, void *) = 0;
    virtual bool readAnalysis(float *, float *) const = 0;
    virtual bool readLiveAnalysis(float *, float *, float *, void *, uint32_t) = 0;
    virtual void setLiveAnalysisReader(WjnFxLiveAnalysisReader, void *) = 0;
};

template <class Dsp, class Editor, int StripHeight> class MixerEffect final : public WjnFxInstance
{
  public:
    using Processor = typename Dsp::Processor;
    using Parameters = typename Dsp::Parameters;
    static_assert(std::is_trivially_copyable_v<Parameters>);
    MixerEffect()
    {
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        spectrumAnalyzer_ = std::make_unique<Common::GraphicEqSpectrumAnalyzer>();
#elif WJN_FX_MODULE_INDEX == 2
        for (auto &buffer : deEsserSpectrum_)
            buffer = std::make_unique<Common::AudioRingBuffer<Common::DeEsserSpectrumSample>>(8192);
#endif
        prepare(48000.0);
    }
    ~MixerEffect() override
    {
        if (view_)
            delete view_;
    }
    WjnFxInstance *clone() const override
    {
        auto *copy = new MixerEffect;
        copy->parameters_ = parameters_;
        copy->prepare(sampleRate_);
        copy->channel_ = channel_;
        copy->palette_ = palette_;
        return copy;
    }
    void prepare(double rate) override
    {
        if (!std::isfinite(rate) || rate < 8000.0)
            return;
        sampleRate_ = rate;
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        spectrumAnalyzer_->setSampleRate(static_cast<std::uint32_t>(rate));
#endif
        for (std::size_t c = 0; c < 8; ++c)
        {
            Dsp::prepare(processors_[c], rate);
            Dsp::setParameters(processors_[c], parameters_[c]);
        }
    }
    void process(uint32_t channel, const float *input, float *output, uint32_t frames) override
    {
        if (!output || channel >= 8)
            return;
        if (!input)
        {
            std::fill_n(output, frames, 0.0F);
            inputPeaks_[channel].store(0.0F, std::memory_order_release);
            outputPeaks_[channel].store(0.0F, std::memory_order_release);
            return;
        }
        float inputPeak = 0.0F;
        for (uint32_t i = 0; i < frames; ++i)
            inputPeak = std::max(inputPeak, std::abs(input[i]));
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        spectrumAnalyzer_->pushChannelInput(channel, input, frames);
#endif
#if WJN_FX_MODULE_INDEX == 2
        std::array<Common::DeEsserSpectrumSample, 512> samples{};
        for (uint32_t start = 0; start < frames;)
        {
            const auto count = std::min<std::uint32_t>(512, frames - start);
            for (uint32_t offset = 0; offset < count; ++offset)
            {
                const float dry = input[start + offset];
                const float wet = processors_[channel].process(dry);
                output[start + offset] = wet;
                samples[offset] = {dry, wet};
            }
            deEsserSpectrum_[channel]->write(samples.data(), count);
            start += count;
        }
#else
        Dsp::process(processors_[channel], parameters_[channel], input, output, frames);
#endif
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        spectrumAnalyzer_->pushChannelOutput(channel, output, frames);
#elif WJN_FX_MODULE_INDEX == 3
        gainReduction_[channel].store(processors_[channel].gainReductionDb(),
                                      std::memory_order_release);
#elif WJN_FX_MODULE_INDEX == 4
        gainReduction_[channel].store(processors_[channel].meter().gainReductionDb,
                                      std::memory_order_release);
#endif
        float outputPeak = 0.0F;
        for (uint32_t i = 0; i < frames; ++i)
            outputPeak = std::max(outputPeak, std::abs(output[i]));
        inputPeaks_[channel].store(inputPeak, std::memory_order_release);
        outputPeaks_[channel].store(outputPeak, std::memory_order_release);
    }
    void setChannel(int channel) override
    {
        channel_ = std::clamp(channel, -1, 7);
        if (editor_)
            editor_->setSelectedChannel(channel_);
    }
    void setTheme(const UI::Controls::Palette &palette) override
    {
        palette_ = palette;
        UI::Controls::applyPalette(view_, palette_);
        UI::Controls::applyPalette(editor_, palette_);
    }
    QWidget *createEditor(QWidget *parent) override
    {
        if (view_)
        {
            view_->setParent(parent);
            return view_;
        }
        auto *editor = new Editor(parent);
        editor_ = editor;
        editor->setParameters(parameters_);
        editor->setSelectedChannel(channel_);
        editor->setChangeHandler([this](std::size_t channel, const Parameters &value) {
            if (channel >= 8)
                return;
            parameters_[channel] = value;
            Dsp::setParameters(processors_[channel], value);
            if (changeCallback_)
                changeCallback_(changeContext_);
        });
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        editor->setSpectrumFrameReader([this](Common::GraphicEqSpectrumFrame &frame) {
            return liveReader_ &&
                   liveReader_(liveContext_, nullptr, nullptr, nullptr, &frame, sizeof(frame)) == 1;
        });
#elif WJN_FX_MODULE_INDEX == 2
        editor->setSpectrumFrameReader([this](Common::DeEsserLiveSpectrumFrame &frame) {
            return liveReader_ &&
                   liveReader_(liveContext_, nullptr, nullptr, nullptr, &frame, sizeof(frame)) == 1;
        });
#elif WJN_FX_MODULE_INDEX == 3
        editor->setMeterReader([this] {
            std::array<UI::CompressorMeter, 8> meters{};
            float input[8]{}, output[8]{}, reduction[8]{};
            if (!liveReader_ ||
                liveReader_(liveContext_, input, output, reduction, nullptr, 0) != 1)
                return meters;
            for (std::size_t channel = 0; channel < 8; ++channel)
            {
                meters[channel].inputDb =
                    input[channel] > 0.0F ? 20.0F * std::log10(input[channel]) : -120.0F;
                meters[channel].outputDb =
                    output[channel] > 0.0F ? 20.0F * std::log10(output[channel]) : -120.0F;
                meters[channel].gainReductionDb = reduction[channel];
                meters[channel].active = input[channel] > 0.0F;
            }
            return meters;
        });
#elif WJN_FX_MODULE_INDEX == 4
        editor->setMeterReader([this] {
            std::array<UI::LimiterMeter, 8> meters{};
            float input[8]{}, output[8]{}, reduction[8]{};
            if (!liveReader_ ||
                liveReader_(liveContext_, input, output, reduction, nullptr, 0) != 1)
                return meters;
            for (std::size_t channel = 0; channel < 8; ++channel)
            {
                meters[channel].inputDb =
                    input[channel] > 0.0F ? 20.0F * std::log10(input[channel]) : -120.0F;
                meters[channel].outputDb =
                    output[channel] > 0.0F ? 20.0F * std::log10(output[channel]) : -120.0F;
                meters[channel].gainReductionDb = reduction[channel];
                meters[channel].active = input[channel] > 0.0F;
            }
            return meters;
        });
#endif
        view_ = new UI::Controls::EmbeddedFxEditor(editor, StripHeight, parent);
        WinJACKNexus::NativeFx::I18n::bind(view_, [view = view_.data(), editor] {
            view->setWindowTitle(editor->windowTitle());
            view->setAccessibleDescription(editor->accessibleDescription());
        });
        setTheme(palette_);
        return view_;
    }
    int currentPreset() const override
    {
        return editor_ ? editor_->hostPresetIndex() : -1;
    }
    void selectPreset(int index) override
    {
        if (editor_)
            editor_->chooseHostPreset(index);
    }
    bool exportSnapshot(void *destination, uint32_t bytes) const override
    {
        constexpr auto required = sizeof(WjnFxParameterSnapshotHeader) + sizeof(parameters_);
        if (!destination || bytes != required)
            return false;
        WjnFxParameterSnapshotHeader header{};
        header.formatVersion = WJN_FX_PARAMETER_SNAPSHOT_VERSION;
        header.parameterBytes = sizeof(Parameters);
        header.channelMask = 0xff;
        std::memcpy(header.moduleId, activeMeta.id,
                    std::min(std::strlen(activeMeta.id), sizeof(header.moduleId) - 1));
        std::memcpy(destination, &header, sizeof(header));
        std::memcpy(static_cast<std::byte *>(destination) + sizeof(header), parameters_.data(),
                    sizeof(parameters_));
        return true;
    }
    bool importSnapshot(const void *source, uint32_t bytes) override
    {
        constexpr auto required = sizeof(WjnFxParameterSnapshotHeader) + sizeof(parameters_);
        if (!source || bytes != required)
            return false;
        WjnFxParameterSnapshotHeader header{};
        std::memcpy(&header, source, sizeof(header));
        if (header.formatVersion != WJN_FX_PARAMETER_SNAPSHOT_VERSION ||
            header.parameterBytes != sizeof(Parameters) || header.reserved != 0 ||
            header.channelMask == 0 || (header.channelMask & ~0xffU) != 0 ||
            std::strncmp(header.moduleId, activeMeta.id, sizeof(header.moduleId)) != 0)
            return false;
        const auto *values = static_cast<const std::byte *>(source) + sizeof(header);
        for (std::size_t channel = 0; channel < parameters_.size(); ++channel)
        {
            if ((header.channelMask & (1U << channel)) == 0)
                continue;
            Parameters next{};
            std::memcpy(&next, values + channel * sizeof(Parameters), sizeof(Parameters));
            parameters_[channel] = next;
            Dsp::setParameters(processors_[channel], next);
        }
        if (editor_)
            editor_->setParameters(parameters_);
        return true;
    }
    void setChangeCallback(WjnFxParametersChanged callback, void *context) override
    {
        changeCallback_ = callback;
        changeContext_ = context;
    }
    bool readAnalysis(float *input, float *output) const override
    {
        if (!input || !output)
            return false;
        for (std::size_t channel = 0; channel < 8; ++channel)
        {
            input[channel] = inputPeaks_[channel].load(std::memory_order_acquire);
            output[channel] = outputPeaks_[channel].load(std::memory_order_acquire);
        }
        return true;
    }
    bool readLiveAnalysis(float *input, float *output, float *reduction, void *spectrum,
                          uint32_t bytes) override
    {
        if (input && output && reduction)
        {
            for (std::size_t channel = 0; channel < 8; ++channel)
            {
                input[channel] = inputPeaks_[channel].load(std::memory_order_acquire);
                output[channel] = outputPeaks_[channel].load(std::memory_order_acquire);
                reduction[channel] = gainReduction_[channel].load(std::memory_order_acquire);
            }
            return true;
        }
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        if (spectrum && bytes == sizeof(Common::GraphicEqSpectrumFrame))
            return spectrumAnalyzer_->tryPopLatest(
                *static_cast<Common::GraphicEqSpectrumFrame *>(spectrum));
#elif WJN_FX_MODULE_INDEX == 2
        if (spectrum && bytes == sizeof(Common::DeEsserLiveSpectrumFrame))
        {
            auto &frame = *static_cast<Common::DeEsserLiveSpectrumFrame *>(spectrum);
            frame.sampleRate = static_cast<std::uint32_t>(sampleRate_);
            bool received = false;
            for (std::size_t channel = 0; channel < 8; ++channel)
            {
                auto &queue = *deEsserSpectrum_[channel];
                while (queue.available() > frame.samples[channel].size())
                    queue.read(frame.samples[channel].data(), frame.samples[channel].size());
                frame.counts[channel] =
                    queue.read(frame.samples[channel].data(), frame.samples[channel].size());
                received |= frame.counts[channel] != 0;
            }
            return received;
        }
#endif
        return false;
    }
    void setLiveAnalysisReader(WjnFxLiveAnalysisReader reader, void *context) override
    {
        liveReader_ = reader;
        liveContext_ = context;
    }

  private:
    std::array<Processor, 8> processors_{};
    std::array<Parameters, 8> parameters_{};
    QPointer<Editor> editor_;
    QPointer<QWidget> view_;
    UI::Controls::Palette palette_;
    int channel_ = -1;
    double sampleRate_ = 48000.0;
    WjnFxParametersChanged changeCallback_{};
    void *changeContext_{};
    std::array<std::atomic<float>, 8> inputPeaks_{};
    std::array<std::atomic<float>, 8> outputPeaks_{};
    std::array<std::atomic<float>, 8> gainReduction_{};
    WjnFxLiveAnalysisReader liveReader_{};
    void *liveContext_{};
#if WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
    std::unique_ptr<Common::GraphicEqSpectrumAnalyzer> spectrumAnalyzer_;
#elif WJN_FX_MODULE_INDEX == 2
    std::array<std::unique_ptr<Common::AudioRingBuffer<Common::DeEsserSpectrumSample>>, 8>
        deEsserSpectrum_;
#endif
};

extern "C"
{
    uint32_t wjn_fx_api_version(void)
    {
        return WJN_FX_API_VERSION;
    }
    uint32_t wjn_fx_preset_count(void)
    {
#if WJN_FX_MODULE_INDEX == 0 || WJN_FX_MODULE_INDEX == 3 || WJN_FX_MODULE_INDEX == 4
        return 4;
#elif WJN_FX_MODULE_INDEX == 5
        return 7;
#elif WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6
        return WJN_FX_MODULE_INDEX == 1 ? 5 : 6;
#elif WJN_FX_MODULE_INDEX == 2
        return 4;
#else
        return 7;
#endif
    }
    const char *wjn_fx_preset_name(uint32_t index)
    {
#if WJN_FX_MODULE_INDEX == 0
        static constexpr const char *keys[] = {"preset.default", "preset.tight", "preset.gentle",
                                               "preset.custom"};
        static constexpr const char *english[] = {"Default", "Tight Gate", "Gentle Gate", "Custom"};
#elif WJN_FX_MODULE_INDEX == 1
        static constexpr const char *keys[] = {"preset.default", "preset.vocalBright",
                                               "preset.bassBoost", "preset.classical",
                                               "preset.custom"};
        static constexpr const char *english[] = {"Default", "Vocal Bright", "Bass Boost",
                                                  "Classical", "Custom"};
#elif WJN_FX_MODULE_INDEX == 2
        static constexpr const char *keys[] = {"preset.default", "preset.brightVocal",
                                               "preset.softVoice", "preset.custom"};
        static constexpr const char *english[] = {"Default", "Bright Vocal", "Soft Voice",
                                                  "Custom"};
#elif WJN_FX_MODULE_INDEX == 3
        static constexpr const char *keys[] = {"preset.default", "preset.punchyDrums",
                                               "preset.gentleLevel", "preset.custom"};
        static constexpr const char *english[] = {"Default", "Punchy Drums", "Gentle Level",
                                                  "Custom"};
#elif WJN_FX_MODULE_INDEX == 4
        static constexpr const char *keys[] = {"preset.default", "preset.broadcast",
                                               "preset.maxLoudness", "preset.custom"};
        static constexpr const char *english[] = {"Default", "Broadcast", "Max Loudness", "Custom"};
#elif WJN_FX_MODULE_INDEX == 5
        static constexpr const char *keys[] = {
            "preset.default",     "preset.vocalEcho", "preset.shortSlap", "preset.rhythmicEcho",
            "preset.ambientEcho", "preset.tapeEcho",  "preset.custom"};
        static constexpr const char *english[] = {"Default",       "Vocal Echo",   "Short Slap",
                                                  "Rhythmic Echo", "Ambient Echo", "Tape Echo",
                                                  "Custom"};
#elif WJN_FX_MODULE_INDEX == 6
        static constexpr const char *keys[] = {"preset.default",   "preset.vocalClear",
                                               "preset.bassBoost", "preset.warmth",
                                               "preset.presence",  "preset.custom"};
        static constexpr const char *english[] = {"Default", "Vocal Clear", "Bass Boost",
                                                  "Warmth",  "Presence",    "Custom"};
#elif WJN_FX_MODULE_INDEX == 7
        static constexpr const char *keys[] = {
            "preset.default",     "preset.smallRoom",     "preset.brightPlate", "preset.cathedral",
            "preset.studioBooth", "preset.vintageSpring", "preset.custom"};
        static constexpr const char *english[] = {"Default",   "Small Room",   "Bright Plate",
                                                  "Cathedral", "Studio Booth", "Vintage Spring",
                                                  "Custom"};
#endif
        if (index >= std::size(keys))
            return nullptr;
        static std::array<QByteArray, std::size(keys)> names;
        const auto current =
            WinJACKNexus::NativeFx::I18n::text(keys[index], english[index]).toUtf8();
        if (names[index] != current)
            names[index] = current;
        return names[index].constData();
    }
    int wjn_fx_current_preset(const WjnFxInstance *instance)
    {
        return instance ? instance->currentPreset() : -1;
    }
    void wjn_fx_select_preset(WjnFxInstance *instance, int index)
    {
        if (instance)
            instance->selectPreset(index);
    }
    uint32_t wjn_fx_count(void)
    {
        return 1;
    }
    const WjnFxMeta *wjn_fx_meta(uint32_t index)
    {
        return index == 0 ? &activeMeta : nullptr;
    }
#if WJN_FX_MODULE_INDEX <= 7
    uint32_t wjn_fx_theme_api_version(void)
    {
        return WJN_FX_THEME_API_VERSION;
    }
    const char *wjn_fx_theme_namespace_json(uint32_t index)
    {
        if (index != 0)
            return nullptr;
        static const QByteArray descriptor = [] {
            UI::ThemeRegistry registry;
            if (!UI::ThemeRegistry::builtIn(&registry))
                return QByteArray{};
            const QString moduleId = QString::fromLatin1(activeMeta.id);
            const QString nameSpace = QStringLiteral("--wjn-theme-native-fx-") + moduleId + '-';
            QJsonArray tokens;
            for (const auto &name : registry.names())
            {
                if (!name.startsWith(nameSpace) || name.startsWith(nameSpace + "background-"))
                    continue;
                const auto token = registry.token(name);
                QJsonObject item{
                    {"name", token.name}, {"type", token.type}, {"description", token.description}};
                item["default"] =
                    token.hasDefault ? QJsonValue(token.defaultValue) : QJsonValue::Null;
                if (!token.inherits.isEmpty())
                {
                    QJsonArray parents;
                    for (const auto &parent : token.inherits)
                        parents.append(parent);
                    item["inherits"] = parents;
                }
                tokens.append(item);
            }
            if (tokens.isEmpty())
                return QByteArray{};
#if WJN_FX_MODULE_INDEX == 0
            const QJsonArray controls{
                QJsonObject{{"id", "knob"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{QJsonObject{{"id", "lower-transfer"}},
                                         QJsonObject{{"id", "upper-transfer"}}};
#elif WJN_FX_MODULE_INDEX == 1
            const QJsonArray controls{
                QJsonObject{{"id", "knob"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "effect-band-slider"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{QJsonObject{{"id", "input-spectrum"}},
                                         QJsonObject{{"id", "output-spectrum"}},
                                         QJsonObject{{"id", "response"}}};
#elif WJN_FX_MODULE_INDEX == 2
            const QJsonArray controls{
                QJsonObject{{"id", "knob"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{QJsonObject{{"id", "input-spectrum"}},
                                         QJsonObject{{"id", "output-spectrum"}},
                                         QJsonObject{{"id", "response-bell"}}};
#elif WJN_FX_MODULE_INDEX == 3
            const QJsonArray controls{
                QJsonObject{{"id", "knob"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{QJsonObject{{"id", "transfer"}},
                                         QJsonObject{{"id", "level-fill"}}};
            const QJsonArray meterZones{QJsonObject{{"id", "reduction-zone-single"}}};
#elif WJN_FX_MODULE_INDEX == 4
            const QJsonArray controls{
                QJsonObject{{"id", "knob"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{QJsonObject{{"id", "transfer"}}};
            const QJsonArray meterZones{QJsonObject{{"id", "level-zone-low"}},
                                        QJsonObject{{"id", "level-zone-mid"}},
                                        QJsonObject{{"id", "level-zone-high"}},
                                        QJsonObject{{"id", "reduction-zone-single"}}};
#elif WJN_FX_MODULE_INDEX == 5
            const QJsonArray controls{
                QJsonObject{{"id", "knob"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{QJsonObject{{"id", "tap-primary"}},
                                         QJsonObject{{"id", "tap-repeat"}},
                                         QJsonObject{{"id", "baseline"}}};
#elif WJN_FX_MODULE_INDEX == 6
            const QJsonArray controls{
                QJsonObject{{"id", "knob-band-1"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "knob-band-2"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "knob-band-3"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "knob-band-4"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "knob-filter"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{
                QJsonObject{{"id", "input-spectrum"}}, QJsonObject{{"id", "processed-spectrum"}},
                QJsonObject{{"id", "band-1"}},         QJsonObject{{"id", "band-2"}},
                QJsonObject{{"id", "band-3"}},         QJsonObject{{"id", "band-4"}},
                QJsonObject{{"id", "total-response"}}};
#else
            const QJsonArray controls{
                QJsonObject{{"id", "knob-violet"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "knob-cyan"}, {"states", QJsonArray{"normal"}}},
                QJsonObject{{"id", "toggle"}, {"states", QJsonArray{"normal"}}}};
            const QJsonArray chartSeries{};
            const QJsonArray sceneElements{QJsonObject{{"id", "accent-cyan"}},
                                           QJsonObject{{"id", "shape-light"}},
                                           QJsonObject{{"id", "glow"}}};
#endif
            const QJsonObject root{{"descriptorVersion", 1},
                                   {"themeApiVersion", 1},
                                   {"moduleId", moduleId},
                                   {"namespace", nameSpace},
                                   {"displayName", QString::fromUtf8(activeMeta.name)},
                                   {"tokens", tokens},
                                   {"controls", controls},
                                   {"chartSeries", chartSeries}
#if WJN_FX_MODULE_INDEX == 3 || WJN_FX_MODULE_INDEX == 4
                                   ,
                                   {"meterZones", meterZones}
#endif
#if WJN_FX_MODULE_INDEX == 7
                                   ,
                                   {"sceneElements", sceneElements}
#endif
            };
            return QJsonDocument(root).toJson(QJsonDocument::Compact);
        }();
        return descriptor.isEmpty() ? nullptr : descriptor.constData();
    }
#endif
    WjnFxInstance *wjn_fx_create_mixer(uint32_t index)
    {
        if (const auto *meta = wjn_fx_meta(index); !meta || !(meta->placements & WJN_FX_MIXER))
            return nullptr;
#if WJN_FX_MODULE_INDEX == 0
        return new MixerEffect<ActiveDsp, UI::GateEffectWidget, 145>;
#elif WJN_FX_MODULE_INDEX == 1
        return new MixerEffect<ActiveDsp, UI::GraphicEqWidget, 146>;
#elif WJN_FX_MODULE_INDEX == 2
        return new MixerEffect<ActiveDsp, UI::DeEsserWidget, 145>;
#elif WJN_FX_MODULE_INDEX == 3
        return new MixerEffect<ActiveDsp, UI::CompressorWidget, 145>;
#elif WJN_FX_MODULE_INDEX == 4
        return new MixerEffect<ActiveDsp, UI::LimiterWidget, 145>;
#elif WJN_FX_MODULE_INDEX == 5
        return new MixerEffect<ActiveDsp, UI::DelayWidget, 145>;
#elif WJN_FX_MODULE_INDEX == 6
        return new MixerEffect<ActiveDsp, UI::ParametricEqWidget, 88>;
#elif WJN_FX_MODULE_INDEX == 7
        return new MixerEffect<ActiveDsp, UI::ReverbWidget, 143>;
#endif
    }
    void wjn_fx_destroy_mixer(WjnFxInstance *instance)
    {
        delete instance;
    }
    WjnFxInstance *wjn_fx_clone_mixer(const WjnFxInstance *source)
    {
        return source ? source->clone() : nullptr;
    }
    void wjn_fx_prepare(WjnFxInstance *instance, double rate)
    {
        if (instance)
            instance->prepare(rate);
    }
    void wjn_fx_process(WjnFxInstance *instance, uint32_t channel, const float *input,
                        float *output, uint32_t frames)
    {
        if (instance)
            instance->process(channel, input, output, frames);
    }
    uint32_t wjn_fx_parameter_snapshot_version(void)
    {
        return WJN_FX_PARAMETER_SNAPSHOT_VERSION;
    }
    uint32_t wjn_fx_parameter_snapshot_size(void)
    {
        return static_cast<uint32_t>(sizeof(WjnFxParameterSnapshotHeader) +
                                     sizeof(ActiveDsp::Parameters) * 8);
    }
    int wjn_fx_export_parameter_snapshot(const WjnFxInstance *instance, void *destination,
                                         uint32_t bytes)
    {
        return instance && instance->exportSnapshot(destination, bytes) ? 1 : 0;
    }
    int wjn_fx_import_parameter_snapshot(WjnFxInstance *instance, const void *source,
                                         uint32_t bytes)
    {
        return instance && instance->importSnapshot(source, bytes) ? 1 : 0;
    }
    void wjn_fx_set_parameter_change_callback(WjnFxInstance *instance,
                                              WjnFxParametersChanged callback, void *context)
    {
        if (instance)
            instance->setChangeCallback(callback, context);
    }
    int wjn_fx_read_analysis(const WjnFxInstance *instance, float inputPeaks[8],
                             float outputPeaks[8])
    {
        return instance && instance->readAnalysis(inputPeaks, outputPeaks) ? 1 : 0;
    }
    void wjn_fx_set_live_analysis_reader(WjnFxInstance *instance, WjnFxLiveAnalysisReader reader,
                                         void *context)
    {
        if (instance)
            instance->setLiveAnalysisReader(reader, context);
    }
    int wjn_fx_read_live_analysis(const WjnFxInstance *instance, float inputPeaks[8],
                                  float outputPeaks[8], float gainReductionDb[8], void *spectrum,
                                  uint32_t spectrumBytes)
    {
        return instance && const_cast<WjnFxInstance *>(instance)->readLiveAnalysis(
                               inputPeaks, outputPeaks, gainReductionDb, spectrum, spectrumBytes)
                   ? 1
                   : 0;
    }
    void wjn_fx_set_channel(WjnFxInstance *instance, int channel)
    {
        if (instance)
            instance->setChannel(channel);
    }
    void wjn_fx_set_theme(WjnFxInstance *instance, const WjnFxTheme *theme)
    {
        if (!instance || !theme)
            return;
        UI::Controls::Palette palette;
        if (theme->background)
            palette.background = QColor(theme->background);
        if (theme->panel)
            palette.panel = QColor(theme->panel);
        if (theme->text)
            palette.text = QColor(theme->text);
        if (theme->accent)
            palette.accent = QColor(theme->accent);
        if (theme->border)
            palette.border = QColor(theme->border);
        instance->setTheme(palette);
    }
    QWidget *wjn_fx_create_mixer_editor(WjnFxInstance *instance, QWidget *parent)
    {
        return instance ? instance->createEditor(parent) : nullptr;
    }
}
