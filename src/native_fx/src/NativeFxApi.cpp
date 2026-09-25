/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/UI/Controls.hpp>
#if WJN_FX_MODULE_INDEX == 0
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 1
#include <WinJACKNexus/NativeFx/graphic_eq/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 2
#include <WinJACKNexus/NativeFx/deesser/Ui.hpp>
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
#include <cmath>

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

struct WjnFxInstance {
    virtual ~WjnFxInstance() = default;
    virtual WjnFxInstance* clone() const = 0;
    virtual void prepare(double) = 0;
    virtual void process(uint32_t, const float*, float*, uint32_t) = 0;
    virtual void setChannel(int) = 0;
    virtual void setTheme(const UI::Controls::Palette&) = 0;
    virtual QWidget* createEditor(QWidget*) = 0;
    virtual int currentPreset() const = 0;
    virtual void selectPreset(int) = 0;
};

template<class Dsp, class Editor, int StripHeight>
class MixerEffect final : public WjnFxInstance {
public:
    using Processor = typename Dsp::Processor;
    using Parameters = typename Dsp::Parameters;
    MixerEffect() { prepare(48000.0); }
    ~MixerEffect() override { if (view_) delete view_; }
    WjnFxInstance* clone() const override {
        auto* copy = new MixerEffect;
        copy->parameters_ = parameters_;
        copy->prepare(sampleRate_);
        copy->channel_ = channel_;
        copy->palette_ = palette_;
        return copy;
    }
    void prepare(double rate) override {
        if (!std::isfinite(rate) || rate < 8000.0) return;
        sampleRate_ = rate;
        for (std::size_t c = 0; c < 8; ++c) {
            Dsp::prepare(processors_[c], rate);
            Dsp::setParameters(processors_[c], parameters_[c]);
        }
    }
    void process(uint32_t channel, const float* input, float* output, uint32_t frames) override {
        if (!output || channel >= 8) return;
        if (!input) { std::fill_n(output, frames, 0.0F); return; }
        Dsp::process(processors_[channel], parameters_[channel], input, output, frames);
    }
    void setChannel(int channel) override {
        channel_ = std::clamp(channel, -1, 7);
        if (editor_) editor_->setSelectedChannel(channel_);
    }
    void setTheme(const UI::Controls::Palette& palette) override {
        palette_ = palette;
        UI::Controls::applyPalette(view_, palette_);
        UI::Controls::applyPalette(editor_, palette_);
    }
    QWidget* createEditor(QWidget* parent) override {
        if (view_) {
            view_->setParent(parent);
            return view_;
        }
        auto* editor = new Editor(parent);
        editor_ = editor;
        editor->setParameters(parameters_);
        editor->setSelectedChannel(channel_);
        editor->setChangeHandler([this](std::size_t channel, const Parameters& value) {
            if (channel >= 8) return;
            parameters_[channel] = value;
            Dsp::setParameters(processors_[channel], value);
        });
        view_ = new UI::Controls::EmbeddedFxEditor(editor, StripHeight, parent);
        setTheme(palette_);
        return view_;
    }
    int currentPreset() const override { return editor_ ? editor_->hostPresetIndex() : -1; }
    void selectPreset(int index) override { if (editor_) editor_->chooseHostPreset(index); }
private:
    std::array<Processor, 8> processors_ {};
    std::array<Parameters, 8> parameters_ {};
    QPointer<Editor> editor_;
    QPointer<QWidget> view_;
    UI::Controls::Palette palette_;
    int channel_ = -1;
    double sampleRate_ = 48000.0;
};

extern "C" {
uint32_t wjn_fx_api_version(void) { return WJN_FX_API_VERSION; }
uint32_t wjn_fx_preset_count(void) {
#if WJN_FX_MODULE_INDEX == 0 || WJN_FX_MODULE_INDEX == 3 || WJN_FX_MODULE_INDEX == 4
    return 4;
#elif WJN_FX_MODULE_INDEX == 1 || WJN_FX_MODULE_INDEX == 6 || WJN_FX_MODULE_INDEX == 5
    return WJN_FX_MODULE_INDEX == 1 ? 5 : 6;
#elif WJN_FX_MODULE_INDEX == 2
    return 4;
#else
    return 7;
#endif
}
const char* wjn_fx_preset_name(uint32_t index) {
#if WJN_FX_MODULE_INDEX == 0
    static const char* names[] = {"Vocal Gate", "Tight Gate", "Gentle Gate", "Custom"};
#elif WJN_FX_MODULE_INDEX == 1
    static const char* names[] = {"平直", "Vocal Bright", "Bass Boost", "Classical", "Custom"};
#elif WJN_FX_MODULE_INDEX == 2
    static const char* names[] = {"Vocal De-Esser", "Bright Vocal", "Soft Voice", "Custom"};
#elif WJN_FX_MODULE_INDEX == 3
    static const char* names[] = {"Vocal Smooth", "Punchy Drums", "Gentle Level", "Custom"};
#elif WJN_FX_MODULE_INDEX == 4
    static const char* names[] = {"Vocal Safe", "Broadcast", "Max Loudness", "Custom"};
#elif WJN_FX_MODULE_INDEX == 5
    static const char* names[] = {"自定义", "人声回声", "短 slap", "节奏回声", "氛围长回声", "磁带回声"};
#elif WJN_FX_MODULE_INDEX == 6
    static const char* names[] = {"Vocal Clear", "Flat", "Bass Boost", "Warmth", "Presence", "Custom"};
#else
    static const char* names[] = {"Small Room", "Large Hall", "Bright Plate", "Cathedral", "Studio Booth", "Vintage Spring", "Custom"};
#endif
    return index < wjn_fx_preset_count() ? names[index] : nullptr;
}
int wjn_fx_current_preset(const WjnFxInstance* instance) { return instance ? instance->currentPreset() : -1; }
void wjn_fx_select_preset(WjnFxInstance* instance, int index) { if (instance) instance->selectPreset(index); }
uint32_t wjn_fx_count(void) { return 1; }
const WjnFxMeta* wjn_fx_meta(uint32_t index) { return index == 0 ? &activeMeta : nullptr; }
WjnFxInstance* wjn_fx_create_mixer(uint32_t index) {
    if (const auto* meta = wjn_fx_meta(index); !meta || !(meta->placements & WJN_FX_MIXER)) return nullptr;
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
void wjn_fx_destroy_mixer(WjnFxInstance* instance) { delete instance; }
WjnFxInstance* wjn_fx_clone_mixer(const WjnFxInstance* source) { return source ? source->clone() : nullptr; }
void wjn_fx_prepare(WjnFxInstance* instance, double rate) { if (instance) instance->prepare(rate); }
void wjn_fx_process(WjnFxInstance* instance, uint32_t channel, const float* input, float* output, uint32_t frames) {
    if (instance) instance->process(channel, input, output, frames);
}
void wjn_fx_set_channel(WjnFxInstance* instance, int channel) { if (instance) instance->setChannel(channel); }
void wjn_fx_set_theme(WjnFxInstance* instance, const WjnFxTheme* theme) {
    if (!instance || !theme) return;
    UI::Controls::Palette palette;
    if (theme->background) palette.background = QColor(theme->background);
    if (theme->panel) palette.panel = QColor(theme->panel);
    if (theme->text) palette.text = QColor(theme->text);
    if (theme->accent) palette.accent = QColor(theme->accent);
    if (theme->border) palette.border = QColor(theme->border);
    instance->setTheme(palette);
}
QWidget* wjn_fx_create_mixer_editor(WjnFxInstance* instance, QWidget* parent) {
    return instance ? instance->createEditor(parent) : nullptr;
}
}
