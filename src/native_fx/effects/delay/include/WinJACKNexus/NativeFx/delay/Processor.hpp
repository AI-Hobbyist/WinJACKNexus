/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <vector>

namespace WinJACKNexus::Common
{
struct DelayParameters
{
    float timeMs = 500, feedback = 35, wetDb = -6, dryDb = 0, highPassHz = 200, lowPassHz = 8000,
          bpm = 120;
    bool enabled = true, sync = false, dotted = false, highPass = true, lowPass = true;
    int note = 2, mixMode = 0;
};

class WJN_FX_API DelayProcessor final
{
  public:
    void setSampleRate(float sampleRate);
    void reset() noexcept;
    void process(const float *input, float *output, unsigned frames,
                 const DelayParameters &parameters) noexcept;
    static float delayMilliseconds(const DelayParameters &parameters) noexcept;

  private:
    std::vector<float> ring_;
    float sampleRate_ = 48000, highPassState_ = 0, lowPassState_ = 0, previousInput_ = 0;
    unsigned write_ = 0;
};
}
