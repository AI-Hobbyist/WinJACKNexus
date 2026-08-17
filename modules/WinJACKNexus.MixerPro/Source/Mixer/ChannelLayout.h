#pragma once

#include <array>

namespace mixerpro
{

enum class ChannelMode
{
    mono,
    stereo,
    twoPointOne,
    fivePointOne,
    sevenPointOne
};

enum class SpeakerRole
{
    left,
    right,
    center,
    lfe,
    leftSurround,
    rightSurround,
    leftRearSurround,
    rightRearSurround
};

struct ChannelLayout
{
    ChannelMode mode = ChannelMode::stereo;
    std::array<SpeakerRole, 8> speakers {};
    int channelCount = 2;

    static ChannelLayout mono() noexcept
    {
        return { ChannelMode::mono, { SpeakerRole::center }, 1 };
    }

    static ChannelLayout stereo() noexcept
    {
        return { ChannelMode::stereo, { SpeakerRole::left, SpeakerRole::right }, 2 };
    }

    static ChannelLayout twoPointOne() noexcept
    {
        return { ChannelMode::twoPointOne,
                 { SpeakerRole::left, SpeakerRole::right, SpeakerRole::lfe },
                 3 };
    }

    static ChannelLayout fivePointOne() noexcept
    {
        return { ChannelMode::fivePointOne,
                 { SpeakerRole::left, SpeakerRole::right, SpeakerRole::center,
                   SpeakerRole::lfe, SpeakerRole::leftSurround, SpeakerRole::rightSurround },
                 6 };
    }

    static ChannelLayout sevenPointOne() noexcept
    {
        return { ChannelMode::sevenPointOne,
                 { SpeakerRole::left, SpeakerRole::right, SpeakerRole::center,
                   SpeakerRole::lfe, SpeakerRole::leftSurround, SpeakerRole::rightSurround,
                   SpeakerRole::leftRearSurround, SpeakerRole::rightRearSurround },
                 8 };
    }
};

} // namespace mixerpro
