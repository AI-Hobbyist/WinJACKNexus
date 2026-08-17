#include "Mixer/SoloMuteResolver.h"

#include <algorithm>

namespace mixerpro
{

bool SoloMuteResolver::shouldPass(const InputChannelState& channel,
                                  const std::vector<InputChannelState>& allInputs) noexcept
{
    bool anySolo = false;

    for (const auto& input : allInputs)
    {
        if (input.solo)
        {
            anySolo = true;
            break;
        }
    }

    if (anySolo)
        return channel.solo && !channel.mute;

    return !channel.mute;
}

} // namespace mixerpro
