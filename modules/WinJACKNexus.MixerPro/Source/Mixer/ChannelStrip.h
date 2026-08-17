#pragma once

#include "Audio/AudioBackend.h"
#include "Mixer/ChannelLayout.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace mixerpro
{

using ChannelId = int;
using AuxId = int;
using SubmixId = int;

enum class OutputTargetKind
{
    mainMix,
    submix,
    backendOutput
};

struct OutputTarget
{
    OutputTarget() = default;

    OutputTargetKind kind = OutputTargetKind::mainMix;
    std::optional<SubmixId> submixId;
    std::optional<BackendPortIdentity> backendOutput;

    static OutputTarget mainMix() noexcept
    {
        return {};
    }

    static OutputTarget submix(SubmixId id)
    {
        OutputTarget target;
        target.kind = OutputTargetKind::submix;
        target.submixId = id;
        return target;
    }
};

struct ChannelStripState
{
    ChannelStripState() = default;

    ChannelId id = 0;
    juce::String name;
    ChannelLayout layout = ChannelLayout::stereo();
    bool mute = false;
    bool solo = false;
    float inputGainDb = 0.0f;
    float faderDb = 0.0f;
};

struct AuxSendState
{
    AuxSendState() = default;

    AuxId targetAux = 0;
    bool enabled = false;
    bool preFader = false;
    float sendLevelDb = 0.0f;
    float pan = 0.0f;
};

struct InputChannelState : ChannelStripState
{
    InputChannelState() = default;

    OutputTarget outputTarget = OutputTarget::mainMix();
    std::vector<AuxSendState> sends;
};

struct AuxChannelState : ChannelStripState
{
    AuxChannelState() = default;

    AuxId auxId = 0;
    OutputTarget outputTarget = OutputTarget::mainMix();
};

struct SubmixChannelState : ChannelStripState
{
    SubmixChannelState() = default;

    SubmixId submixId = 0;
    OutputTarget outputTarget = OutputTarget::mainMix();
};

struct MasterChannelState : ChannelStripState
{
    MasterChannelState() = default;

    bool deletable = false;
};

} // namespace mixerpro
