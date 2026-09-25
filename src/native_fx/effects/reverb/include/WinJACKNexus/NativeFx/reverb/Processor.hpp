/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <array>
#include <vector>

namespace WinJACKNexus::Common {
struct ReverbParameters {
    int scene=1; // Room, Hall, Plate, Church, Studio, Spring, Custom
    float decay=2.4F, size=60, preDelayMs=20, diffusion=100;
    float earlyDb=-6, earlyTimeMs=80, highFreqHz=4000, highDampDb=-6;
    float lowFreqHz=200, lowDampDb=-3, wet=30, dryDb=-3, width=120, outputDb=0;
    bool enabled=true;
};

class WJN_FX_API ReverbProcessor final {
public:
    void setSampleRate(float rate);
    void reset() noexcept;
    void process(const float* input, float* output, unsigned frames, const ReverbParameters& p) noexcept;
private:
    struct Line { std::vector<float> data; unsigned at=0; float filter=0; };
    std::array<Line,4> combs_;
    std::array<Line,2> diffusers_;
    std::vector<float> preDelay_;
    unsigned preAt_=0;
    float rate_=48000, lowState_=0, highState_=0;
};
}
