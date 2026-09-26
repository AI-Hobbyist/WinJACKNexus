/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/NativeFx/NativeFxApi.h>

namespace WinJACKNexus::Common {
struct LimiterParameters final {
    float thresholdDb=-1.0F;
    float ceilingDb=-1.0F;
    float releaseMs=100.0F;
    float lookaheadDb=0.0F;
    int mode=1;
    int oversampling=2;
    bool autoGain=false;
    bool enabled=true;
};

struct LimiterMeter final {
    float inputDb=-120.0F;
    float outputDb=-120.0F;
    float gainReductionDb=0.0F;
    bool active=false;
};

// Stateful mono peak limiter. Keep one processor per independent channel.
class WJN_FX_API LimiterProcessor final {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const LimiterParameters& parameters) noexcept;
    float process(float sample) noexcept;
    LimiterMeter meter() const noexcept;
    float ceilingLinear() const noexcept { return ceilingLinear_; }

private:
    void updateCoefficients() noexcept;
    LimiterParameters parameters_{};
    double sampleRate_=48000.0;
    float ceilingLinear_=0.89125094F;
    float releaseAlpha_=0.0F;
    float meterAttackAlpha_=0.0F;
    float meterReleaseAlpha_=0.0F;
    float gain_=1.0F;
    float inputEnergy_=0.0F;
    float outputEnergy_=0.0F;
};
}
