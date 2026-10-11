/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/NativeFx/NativeFxApi.h>

namespace WinJACKNexus::Common
{

enum class DeEsserMode : unsigned char
{
    Wideband,
    Split,
    DynamicEq
};
enum class DeEsserDetector : unsigned char
{
    Rms,
    Peak
};
enum class DeEsserListen : unsigned char
{
    Off,
    Sibilance,
    Original
};

struct DeEsserParameters final
{
    float centerFrequencyHz = 6500.0F;
    float bandwidthHz = 4000.0F;
    float thresholdDb = -12.0F;
    float attackMs = 0.0F;
    float releaseMs = 100.0F;
    float outputGainDb = 0.0F;
    DeEsserMode mode = DeEsserMode::Split;
    DeEsserDetector detector = DeEsserDetector::Rms;
    DeEsserListen listen = DeEsserListen::Off;
    bool enabled = true;
};

// Mono real-time sibilance suppressor. Call setParameters outside the audio
// callback, and call prepare again when the sample rate changes.
class WJN_FX_API DeEsserProcessor final
{
  public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const DeEsserParameters &parameters) noexcept;
    float process(float sample) noexcept;

  private:
    struct Biquad final
    {
        double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        float process(float input) noexcept;
    };
    static void configureBandPass(Biquad &filter, double frequency, double q,
                                  double sampleRate) noexcept;
    void updateFilters() noexcept;
    DeEsserParameters parameters_{};
    Biquad detectorFilter_{}, treatmentFilter_{};
    double sampleRate_ = 48000.0;
    float envelope_ = 0.0F;
    float gain_ = 1.0F;
    float outputGain_ = 1.0F;
};

}
