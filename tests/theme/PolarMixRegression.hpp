/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

inline void verifyPreMixPolar(void (*check)(bool, const char*))
{
    using namespace WinJACKNexus::Common;
    constexpr std::size_t frames = 960;
    for (const auto layout : {MixerLayout::mono, MixerLayout::stereo,
                             MixerLayout::surround51, MixerLayout::surround71}) {
        for (const auto kind : {MixerStripKind::input, MixerStripKind::aux}) {
            DigitalMixerState state;
            const auto inputId = state.add(MixerStripKind::input, layout);
            auto selectedId = inputId;
            state.strip(inputId)->faderPosition = mixerUnityFaderPosition;
            if (kind == MixerStripKind::aux) {
                selectedId = state.add(kind, layout);
                state.strip(inputId)->mixToMain = false;
                state.strip(inputId)->auxSendsEnabled = true;
                check(state.setSend(inputId, {selectedId, true, true,
                    defaultMixerPanner(layout, layout)}), "configure AUX polar source");
            }
            state.strip(selectedId)->inputGainDb = 6.0;
            state.strip(selectedId)->faderPosition = .95 * 24.0 / 84.0;
            state.strip(state.main().id)->faderPosition = mixerUnityFaderPosition;
            std::array<std::array<float, frames>, 8> inputData {};
            inputData[0].fill(.25F);
            std::vector<const float*> inputs;
            for (std::size_t c = 0; c < static_cast<std::size_t>(layout); ++c)
                inputs.push_back(inputData[c].data());
            const auto measure = [&](MixerPolarSnapshot& snapshot, bool& mainSilent) {
                DigitalMixerProcessor processor;
                if (!processor.configure(state, frames)) return false;
                std::vector<std::array<float, frames>> audio(processor.outputPortCount());
                std::vector<float*> outputs;
                for (auto& channel : audio) outputs.push_back(channel.data());
                if (!processor.process(inputs, outputs, frames)
                    || !processor.readPolar(selectedId, state.main().id, snapshot)) return false;
                mainSilent = std::all_of(audio.end() - 2, audio.end(), [](const auto& channel) {
                    return std::all_of(channel.begin(), channel.end(), [](float v) { return v == 0.0F; });
                });
                return true;
            };
            MixerPolarSnapshot enabled, disabled, muted;
            bool mainSilent = true;
            const auto enabledOk = measure(enabled, mainSilent);
            check(enabledOk && !mainSilent, "enabled MIX sends post-fader audio to MAIN");
            state.strip(selectedId)->mixToMain = false;
            const auto disabledOk = measure(disabled, mainSilent);
            check(disabledOk && mainSilent, "disabled MIX keeps actual MAIN outputs silent");
            check(enabledOk && disabledOk && disabled.sampleCount == frames
                  && disabled.outputSumSquares[0] > 0.0,
                  "input and AUX polar Output remains available before MIX in every layout");
            for (std::size_t c = 0; c < 8; ++c) {
                check(std::abs(enabled.inputSumSquares[c] - disabled.inputSumSquares[c]) < .000001
                      && std::abs(enabled.outputSumSquares[c] - disabled.outputSumSquares[c]) < .000001,
                      "MIX switch leaves both polar sample measurements unchanged");
            }
            state.strip(selectedId)->mute = true;
            check(measure(muted, mainSilent) && mainSilent
                  && muted.inputSumSquares[0] > 0.0
                  && std::all_of(muted.outputSumSquares.begin(), muted.outputSumSquares.end(),
                                 [](double v) { return v == 0.0; }),
                  "pre-MIX polar Output still respects mute while gain-after Input stays live");
        }
    }
}
