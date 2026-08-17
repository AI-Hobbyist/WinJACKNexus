#pragma once

#include "Mixer/ChannelStrip.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace mixerpro
{

class MixerGraph
{
public:
    MixerGraph();

    const MasterChannelState& getMaster() const noexcept;
    const std::vector<InputChannelState>& getInputs() const noexcept;
    const std::vector<AuxChannelState>& getAuxes() const noexcept;
    const std::vector<SubmixChannelState>& getSubmixes() const noexcept;

    void setInputs(std::vector<InputChannelState> newInputs);
    void setAuxes(std::vector<AuxChannelState> newAuxes);
    void setSubmixes(std::vector<SubmixChannelState> newSubmixes);

    bool validateRouting() const;

    void process(const std::vector<const juce::AudioBuffer<float>*>& inputBuffers,
                 juce::AudioBuffer<float>& masterOutput) const noexcept;

private:
    static float dbToGain(float db) noexcept;
    static void addWithGain(const juce::AudioBuffer<float>& source,
                            juce::AudioBuffer<float>& destination,
                            float gain) noexcept;

    bool reachesSubmix(SubmixId start, SubmixId target) const;
    const AuxChannelState* findAux(AuxId id) const noexcept;
    const SubmixChannelState* findSubmix(SubmixId id) const noexcept;

    std::vector<InputChannelState> inputs;
    std::vector<AuxChannelState> auxes;
    std::vector<SubmixChannelState> submixes;
    MasterChannelState master;
};

} // namespace mixerpro
