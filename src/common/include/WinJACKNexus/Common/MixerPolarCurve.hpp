/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/MixerPolarAnalysis.hpp>

#include <array>

namespace WinJACKNexus::Common
{

struct WINJACK_COMMON_API MixerPolarContour final
{
    std::array<float, 360> inputRadius{};
    std::array<float, 360> outputRadius{};
    bool inputOverrange{};
    bool outputOverrange{};
    bool valid{};
};

// UI-thread smoothing and shared fixed-scale contour construction.
class WINJACK_COMMON_API MixerPolarCurve final
{
  public:
    bool accept(const MixerPolarSnapshot &snapshot) noexcept;
    MixerPolarContour contour() const noexcept;
    void clear() noexcept;
    static float radiusForPower(double power) noexcept;

  private:
    MixerPolarSnapshot last_;
    std::array<double, 8> inputPower_{};
    std::array<double, 8> outputPower_{};
    bool active_{};
};

}
