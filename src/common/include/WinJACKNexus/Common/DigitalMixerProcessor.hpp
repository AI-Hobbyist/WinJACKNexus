/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/MixerPolarAnalysis.hpp>
#include <WinJACKNexus/Common/Export.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>

namespace WinJACKNexus::Common {

struct MixerLevelSnapshot final {
    std::uint64_t stripId {};
    float inputPeak {};
    float postPeak {};
    std::array<float, 8> channelInputPeaks {};
    std::array<float, 8> channelPostPeaks {};
};

// Owns every audio buffer and routing matrix. configure() runs on the control
// thread; process() uses only preallocated storage and plain numeric values.
class WINJACK_COMMON_API DigitalMixerProcessor final {
public:
    using EffectProcess = void (*)(void*, std::uint64_t, std::uint32_t, float*, std::size_t) noexcept;
    DigitalMixerProcessor() = default;
    DigitalMixerProcessor(const DigitalMixerProcessor&) = delete;
    DigitalMixerProcessor& operator=(const DigitalMixerProcessor&) = delete;
    DigitalMixerProcessor(DigitalMixerProcessor&&) noexcept = default;
    DigitalMixerProcessor& operator=(DigitalMixerProcessor&&) noexcept = default;
    bool configure(const DigitalMixerState& state, std::size_t maxFrames, QString* error = nullptr,
                   std::uint32_t sampleRate = 48000);
    void adoptGainHistory(const DigitalMixerProcessor& previous) noexcept;
    void adoptPolarHistory(DigitalMixerProcessor& previous) noexcept;
    void setEffectProcessor(void* context, EffectProcess process) noexcept {
        effectContext_ = context; effectProcess_ = process;
    }
    bool process(std::span<const float* const> inputs,
                 std::span<float* const> outputs, std::size_t frames) noexcept;

    std::size_t inputPortCount() const noexcept { return inputPortCount_; }
    std::size_t outputPortCount() const noexcept { return outputPortCount_; }
    std::size_t maxFrames() const noexcept { return maxFrames_; }
    std::uint32_t sampleRate() const noexcept { return sampleRate_; }
    const std::vector<MixerLevelSnapshot>& levels() const noexcept { return levels_; }
    bool readPolar(std::uint64_t sourceId, std::uint64_t targetId,
                   MixerPolarSnapshot& snapshot) noexcept;
    std::uint64_t polarDropped(std::uint64_t sourceId, std::uint64_t targetId) const noexcept;

private:
    using Matrix = std::array<std::array<float, 8>, 8>;

    struct Route final {
        std::size_t targetIndex {};
        std::uint64_t targetId {};
        bool enabled {};
        bool preFader {};
        bool subMix {};
        Matrix matrix {};
        std::vector<float> contribution;
        std::unique_ptr<MixerPolarAnalyzer> analyzer;
    };
    struct Strip final {
        std::uint64_t id {};
        std::size_t channels {};
        std::size_t portOffset {};
        bool exposeOutput { true };
        bool subMix {};
        Matrix outputMatrix {};
        std::vector<float> outputProcessed;
        std::size_t levelIndex {};
        float inputGain { 1.0F };
        float faderGain { 1.0F };
        float currentInputGain { 1.0F };
        float currentFaderGain { 1.0F };
        std::size_t inputRampRemaining {};
        std::size_t faderRampRemaining {};
        bool mute {};
        bool solo {};
        MixerListenMode listenMode { MixerListenMode::off };
        bool soloAudible {};
        bool mixToMain {};
        bool auxSendsEnabled {};
        Matrix mainMatrix {};
        Matrix monitorMatrix {};
        std::vector<Route> sends;
        std::vector<float> pre;
        std::vector<float> gainTap;
        std::vector<float> post;
        std::vector<float> mainContribution;
        std::unique_ptr<MixerPolarAnalyzer> mainAnalyzer;
    };

    std::vector<Strip> inputs_;
    std::vector<Strip> auxiliaries_;
    std::vector<std::size_t> auxOrder_;
    std::vector<MixerLevelSnapshot> levels_;
    std::vector<float> mainInput_;
    std::vector<float> mainOutput_;
    std::vector<float> monitorInput_;
    std::unique_ptr<MixerPolarAnalyzer> monitorAnalyzer_;
    Matrix monitorMainMatrix_ {};
    std::size_t monitorChannels_ { 2 };
    float monitorFaderGain_ { 1.0F };
    Matrix mainMatrix_ {};
    float mainInputGain_ { 1.0F };
    float mainFaderGain_ { 1.0F };
    float mainCurrentInputGain_ { 1.0F };
    float mainCurrentFaderGain_ { 1.0F };
    std::size_t mainInputRampRemaining_ {};
    std::size_t mainFaderRampRemaining_ {};
    bool mainMute_ {};
    std::size_t inputPortCount_ {};
    std::size_t outputPortCount_ {};
    std::size_t maxFrames_ {};
    std::uint32_t sampleRate_ {};
    std::uint64_t mainId_ {};
    std::uint64_t topologyVersion_ {};
    void* effectContext_ {};
    EffectProcess effectProcess_ {};
};

}
