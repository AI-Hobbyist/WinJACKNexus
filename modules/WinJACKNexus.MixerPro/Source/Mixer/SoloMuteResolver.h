#pragma once

#include "Mixer/ChannelStrip.h"

#include <vector>

namespace mixerpro
{

class SoloMuteResolver
{
public:
    static bool shouldPass(const InputChannelState& channel,
                           const std::vector<InputChannelState>& allInputs) noexcept;
};

} // namespace mixerpro
