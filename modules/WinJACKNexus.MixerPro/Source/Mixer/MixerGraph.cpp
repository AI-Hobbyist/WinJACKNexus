#include "Mixer/MixerGraph.h"
#include "Mixer/SoloMuteResolver.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mixerpro
{

MixerGraph::MixerGraph()
{
    master.id = 0;
    master.name = "Master";
    master.layout = ChannelLayout::stereo();
    master.deletable = false;
}

const MasterChannelState& MixerGraph::getMaster() const noexcept
{
    return master;
}

const std::vector<InputChannelState>& MixerGraph::getInputs() const noexcept
{
    return inputs;
}

const std::vector<AuxChannelState>& MixerGraph::getAuxes() const noexcept
{
    return auxes;
}

const std::vector<SubmixChannelState>& MixerGraph::getSubmixes() const noexcept
{
    return submixes;
}

void MixerGraph::setInputs(std::vector<InputChannelState> newInputs)
{
    inputs = std::move(newInputs);
}

void MixerGraph::setAuxes(std::vector<AuxChannelState> newAuxes)
{
    auxes = std::move(newAuxes);
}

void MixerGraph::setSubmixes(std::vector<SubmixChannelState> newSubmixes)
{
    submixes = std::move(newSubmixes);
}

bool MixerGraph::validateRouting() const
{
    for (const auto& submix : submixes)
    {
        if (submix.outputTarget.kind == OutputTargetKind::submix)
        {
            if (!submix.outputTarget.submixId.has_value())
                return false;

            if (*submix.outputTarget.submixId == submix.submixId)
                return false;

            if (reachesSubmix(*submix.outputTarget.submixId, submix.submixId))
                return false;
        }
    }

    return true;
}

void MixerGraph::process(const std::vector<const juce::AudioBuffer<float>*>& inputBuffers,
                         juce::AudioBuffer<float>& masterOutput) const noexcept
{
    masterOutput.clear();

    std::vector<juce::AudioBuffer<float>> submixBuffers;
    submixBuffers.reserve(submixes.size());

    std::vector<juce::AudioBuffer<float>> auxBuffers;
    auxBuffers.reserve(auxes.size());

    for (size_t index = 0; index < auxes.size(); ++index)
    {
        auxBuffers.emplace_back(masterOutput.getNumChannels(), masterOutput.getNumSamples());
        auxBuffers.back().clear();
    }

    for (size_t index = 0; index < submixes.size(); ++index)
    {
        submixBuffers.emplace_back(masterOutput.getNumChannels(), masterOutput.getNumSamples());
        submixBuffers.back().clear();
    }

    const auto routeToTarget = [&](const juce::AudioBuffer<float>& source,
                                   const OutputTarget& target,
                                   float gain) noexcept
    {
        if (target.kind == OutputTargetKind::mainMix)
        {
            addWithGain(source, masterOutput, gain);
            return;
        }

        if (target.kind == OutputTargetKind::submix && target.submixId.has_value())
        {
            for (size_t index = 0; index < submixes.size(); ++index)
            {
                if (submixes[index].submixId == *target.submixId)
                {
                    addWithGain(source, submixBuffers[index], gain);
                    return;
                }
            }
        }
    };

    const auto inputCount = std::min(inputBuffers.size(), inputs.size());

    for (size_t index = 0; index < inputCount; ++index)
    {
        const auto* source = inputBuffers[index];

        if (source == nullptr || !SoloMuteResolver::shouldPass(inputs[index], inputs))
            continue;

        const auto inputGainDb = inputs[index].inputGainDb;
        const auto postFaderGainDb = inputGainDb + inputs[index].faderDb;

        routeToTarget(*source, inputs[index].outputTarget, dbToGain(postFaderGainDb));

        for (const auto& send : inputs[index].sends)
        {
            if (!send.enabled)
                continue;

            for (size_t auxIndex = 0; auxIndex < auxes.size(); ++auxIndex)
            {
                if (auxes[auxIndex].auxId != send.targetAux)
                    continue;

                const auto tapGainDb = send.preFader ? inputGainDb : postFaderGainDb;
                addWithGain(*source, auxBuffers[auxIndex], dbToGain(tapGainDb + send.sendLevelDb));
                break;
            }
        }
    }

    for (size_t index = 0; index < auxes.size(); ++index)
    {
        const auto& aux = auxes[index];

        if (aux.mute)
            continue;

        const auto gain = dbToGain(aux.inputGainDb + aux.faderDb);
        routeToTarget(auxBuffers[index], aux.outputTarget, gain);
    }

    for (size_t index = 0; index < submixes.size(); ++index)
    {
        const auto& submix = submixes[index];

        if (submix.mute)
            continue;

        const auto gain = dbToGain(submix.inputGainDb + submix.faderDb);
        routeToTarget(submixBuffers[index], submix.outputTarget, gain);
    }
}

float MixerGraph::dbToGain(float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

void MixerGraph::addWithGain(const juce::AudioBuffer<float>& source,
                             juce::AudioBuffer<float>& destination,
                             float gain) noexcept
{
    const auto channels = std::min(source.getNumChannels(), destination.getNumChannels());
    const auto samples = std::min(source.getNumSamples(), destination.getNumSamples());

    for (int channel = 0; channel < channels; ++channel)
        destination.addFrom(channel, 0, source, channel, 0, samples, gain);
}

bool MixerGraph::reachesSubmix(SubmixId start, SubmixId target) const
{
    const auto* current = findSubmix(start);
    int guard = 0;

    while (current != nullptr && guard++ < static_cast<int>(submixes.size()))
    {
        if (current->submixId == target)
            return true;

        if (current->outputTarget.kind != OutputTargetKind::submix
            || !current->outputTarget.submixId.has_value())
            return false;

        current = findSubmix(*current->outputTarget.submixId);
    }

    return false;
}

const SubmixChannelState* MixerGraph::findSubmix(SubmixId id) const noexcept
{
    const auto found = std::find_if(submixes.begin(), submixes.end(), [id](const auto& submix)
    {
        return submix.submixId == id;
    });

    return found != submixes.end() ? &*found : nullptr;
}

const AuxChannelState* MixerGraph::findAux(AuxId id) const noexcept
{
    const auto found = std::find_if(auxes.begin(), auxes.end(), [id](const auto& aux)
    {
        return aux.auxId == id;
    });

    return found != auxes.end() ? &*found : nullptr;
}

} // namespace mixerpro
