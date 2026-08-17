#pragma once

namespace mixerpro
{

struct Eq3BandState
{
    float lowGainDb = 0.0f;
    float midGainDb = 0.0f;
    float highGainDb = 0.0f;
    float lowFrequencyHz = 100.0f;
    float midFrequencyHz = 1000.0f;
    float highFrequencyHz = 10000.0f;
    float midQ = 0.707f;
};

} // namespace mixerpro
