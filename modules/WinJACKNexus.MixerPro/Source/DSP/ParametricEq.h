#pragma once

#include <array>

namespace mixerpro
{

enum class ParametricEqBandType
{
    bell,
    lowShelf,
    highShelf,
    highPass,
    lowPass
};

struct ParametricEqBandState
{
    bool enabled = false;
    ParametricEqBandType type = ParametricEqBandType::bell;
    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
};

struct ParametricEqState
{
    static constexpr int maxBands = 8;
    std::array<ParametricEqBandState, maxBands> bands {};
};

} // namespace mixerpro
