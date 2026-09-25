/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/NativeFx/NativeFxApi.h>

namespace WinJACKNexus::Common {

struct CompressorParameters final {
    float thresholdDb = -18.0F;
    float ratio = 3.0F;
    float kneeDb = 3.0F;
    float attackMs = 10.0F;
    float releaseMs = 100.0F;
    float makeupDb = 0.0F;
    float highPassHz = 80.0F;
    float lowPassHz = 8000.0F;
    bool enabled = true;
    bool rms = true;
    bool sidechainFilter = true;
    bool autoMakeup = false;
};

// Stateful mono compressor. Use one instance per independent audio channel.
// Parameter changes may be applied between blocks, never concurrently with process().
class WJN_FX_API CompressorProcessor final {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const CompressorParameters& parameters) noexcept;
    float process(float sample) noexcept;

    float inputLevelDb() const noexcept;
    float outputLevelDb() const noexcept;
    float detectorLevelDb() const noexcept;
    float gainReductionDb() const noexcept;

private:
    void updateDetectorCoefficients() noexcept;
    float transferOutputDb(float inputDb) const noexcept;
    CompressorParameters parameters_ {};
    double sampleRate_ = 48000.0;
    float highPassAlpha_ = 0.0F;
    float lowPassAlpha_ = 0.0F;
    float attackAlpha_ = 0.0F;
    float releaseAlpha_ = 0.0F;
    float highPassInput_ = 0.0F;
    float highPassOutput_ = 0.0F;
    float lowPassOutput_ = 0.0F;
    float detectorEnvelope_ = 0.0F;
    float inputMeterEnvelope_ = 0.0F;
    float outputMeterEnvelope_ = 0.0F;
    float gainReductionDb_ = 0.0F;
    float makeupDb_ = 0.0F;
};

}
