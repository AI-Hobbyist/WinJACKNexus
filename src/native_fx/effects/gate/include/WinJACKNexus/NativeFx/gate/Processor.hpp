/*
 * This file is part of WinJACKNexus.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#pragma once

#include <WinJACKNexus/NativeFx/NativeFxApi.h>

namespace WinJACKNexus::Common {

struct GateParameters final {
    float thresholdDb = -32.0F;
    float ratio = 4.0F;
    float attackMs = 10.0F;
    float releaseMs = 150.0F;
    float floorDb = -60.0F;
    float holdDb = 3.0F;
    float highPassHz = 80.0F;
    float lowPassHz = 8000.0F;
    bool sidechainFilter = true;
    bool enabled = true;
};

// A single mono downward expander. Call setParameters outside the audio callback.
class WJN_FX_API GateProcessor final {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const GateParameters& parameters) noexcept;
    float process(float sample) noexcept;
    float currentGainDb() const noexcept;

private:
    GateParameters parameters_;
    double sampleRate_ = 48000.0;
    float highPassAlpha_ = 0.0F;
    float lowPassAlpha_ = 0.0F;
    float attackAlpha_ = 0.0F;
    float releaseAlpha_ = 0.0F;
    float detectorAlpha_ = 0.0F;
    float highPassInput_ = 0.0F;
    float highPassOutput_ = 0.0F;
    float lowPassOutput_ = 0.0F;
    float envelope_ = 0.0F;
    float gainDb_ = 0.0F;
    bool gateOpen_ = false;
};

}
