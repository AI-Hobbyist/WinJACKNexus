#include "Audio/AudioEngine.h"
#include "Audio/NullAudioBackend.h"
#include "DSP/GainStage.h"
#include "DSP/LevelMeterProbe.h"
#include "DSP/Panner.h"
#include "Mixer/MixerGraph.h"
#include "Mixer/SoloMuteResolver.h"

#include <cmath>
#include <iostream>

namespace
{

bool nearlyEqual(float a, float b)
{
    return std::abs(a - b) < 0.000001f;
}

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

int failValue(const char* message, float value)
{
    std::cerr << message << ": " << value << '\n';
    return 1;
}

int failState(const char* message, bool inputASolo, bool inputAMute, bool inputBSolo, bool inputBMute)
{
    std::cerr << message
              << " A(solo=" << inputASolo << ", mute=" << inputAMute << ")"
              << " B(solo=" << inputBSolo << ", mute=" << inputBMute << ")\n";
    return 1;
}

} // namespace

int main()
{
    mixerpro::NullAudioBackend backend;

    mixerpro::AudioDeviceSettings requested;
    requested.requestedSampleRate = 48000.0;
    requested.requestedBlockSize = 128;

    backend.open(requested);

    if (!backend.isOpen())
        return fail("Null backend did not open");

    const auto effective = backend.getEffectiveSettings();

    mixerpro::AudioEngine engine;
    engine.prepare(effective);
    engine.start();
    backend.start(&engine);

    if (!backend.isRunning())
        return fail("Null backend did not start");

    juce::AudioBuffer<float> input(2, effective.blockSize);
    juce::AudioBuffer<float> output(2, effective.blockSize);

    for (int sample = 0; sample < effective.blockSize; ++sample)
    {
        input.setSample(0, sample, static_cast<float>(sample) / 128.0f);
        input.setSample(1, sample, static_cast<float>(128 - sample) / 128.0f);
    }

    engine.processBlock(input, output);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < effective.blockSize; ++sample)
            if (!nearlyEqual(input.getSample(channel, sample), output.getSample(channel, sample)))
                return fail("Stereo pass-through sample mismatch");

    backend.stop();
    engine.stop();

    mixerpro::MixerGraph graph;

    if (graph.getMaster().deletable)
        return fail("Default master should be non-deletable");

    mixerpro::InputChannelState inputA;
    inputA.id = 1;
    inputA.name = "Input A";

    mixerpro::InputChannelState inputB;
    inputB.id = 2;
    inputB.name = "Input B";

    graph.setInputs({ inputA, inputB });

    if (graph.getInputs().size() != 2)
        return fail("Graph did not store input channels");

    if (!mixerpro::SoloMuteResolver::shouldPass(graph.getInputs()[0], graph.getInputs()))
        return failState("Default input should pass solo/mute resolver",
                         graph.getInputs()[0].solo,
                         graph.getInputs()[0].mute,
                         graph.getInputs()[1].solo,
                         graph.getInputs()[1].mute);

    if (graph.getInputs()[0].outputTarget.kind != mixerpro::OutputTargetKind::mainMix)
        return fail("Default input output target should be Main Mix");

    juce::AudioBuffer<float> sourceA(2, 4);
    juce::AudioBuffer<float> sourceB(2, 4);
    juce::AudioBuffer<float> mixed(2, 4);

    sourceA.clear();
    sourceB.clear();

    for (int sample = 0; sample < 4; ++sample)
    {
        sourceA.setSample(0, sample, 0.25f);
        sourceA.setSample(1, sample, 0.25f);
        sourceB.setSample(0, sample, 0.5f);
        sourceB.setSample(1, sample, 0.5f);
    }

    graph.process({ &sourceA, &sourceB }, mixed);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            if (!nearlyEqual(mixed.getSample(channel, sample), 0.75f))
                return failValue("Input summing mismatch", mixed.getSample(channel, sample));

    inputB.mute = true;
    graph.setInputs({ inputA, inputB });
    graph.process({ &sourceA, &sourceB }, mixed);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            if (!nearlyEqual(mixed.getSample(channel, sample), 0.25f))
                return fail("Muted input contributed to mix");

    inputA.solo = false;
    inputB.mute = false;
    inputB.solo = true;
    graph.setInputs({ inputA, inputB });
    graph.process({ &sourceA, &sourceB }, mixed);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            if (!nearlyEqual(mixed.getSample(channel, sample), 0.5f))
                return fail("Solo resolver mismatch");

    mixerpro::SubmixChannelState submix;
    submix.submixId = 10;
    submix.id = 10;
    submix.name = "Music";

    inputA.solo = false;
    inputB.solo = false;
    inputA.outputTarget = mixerpro::OutputTarget::submix(10);
    inputB.mute = true;
    graph.setInputs({ inputA, inputB });
    graph.setSubmixes({ submix });

    if (!graph.validateRouting())
        return fail("Valid submix route rejected");

    graph.process({ &sourceA, &sourceB }, mixed);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            if (!nearlyEqual(mixed.getSample(channel, sample), 0.25f))
                return fail("Submix route did not reach master");

    mixerpro::SubmixChannelState submixA;
    submixA.submixId = 20;
    submixA.outputTarget = mixerpro::OutputTarget::submix(21);

    mixerpro::SubmixChannelState submixB;
    submixB.submixId = 21;
    submixB.outputTarget = mixerpro::OutputTarget::submix(20);

    graph.setSubmixes({ submixA, submixB });

    if (graph.validateRouting())
        return fail("Submix routing cycle was not rejected");

    mixerpro::MixerGraph auxGraph;
    mixerpro::InputChannelState auxInput;
    auxInput.id = 100;
    auxInput.name = "Aux Source";
    auxInput.faderDb = -6.0f;
    auxInput.outputTarget.kind = mixerpro::OutputTargetKind::backendOutput;

    mixerpro::AuxSendState preSend;
    preSend.targetAux = 1;
    preSend.enabled = true;
    preSend.preFader = true;
    auxInput.sends = { preSend };

    mixerpro::AuxChannelState aux;
    aux.auxId = 1;
    aux.name = "Aux 1";

    juce::AudioBuffer<float> auxSource(2, 4);
    juce::AudioBuffer<float> auxMixed(2, 4);

    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 4; ++sample)
            auxSource.setSample(channel, sample, 1.0f);

    auxGraph.setInputs({ auxInput });
    auxGraph.setAuxes({ aux });
    auxGraph.process({ &auxSource }, auxMixed);

    if (!nearlyEqual(auxMixed.getSample(0, 0), 1.0f))
        return fail("Pre-fader send should not follow input fader");

    auxInput.sends[0].preFader = false;
    auxGraph.setInputs({ auxInput });
    auxGraph.process({ &auxSource }, auxMixed);

    if (auxMixed.getSample(0, 0) >= 0.6f)
        return fail("Post-fader send should follow input fader attenuation");

    mixerpro::GainStage gainStage;
    gainStage.prepare(48000.0, 4, 1);
    gainStage.setGainDecibels(-6.0f);

    juce::AudioBuffer<float> gainBuffer(1, 4);
    gainBuffer.clear();

    for (int sample = 0; sample < 4; ++sample)
        gainBuffer.setSample(0, sample, 1.0f);

    gainStage.process(gainBuffer);

    if (gainBuffer.getSample(0, 0) >= 1.0f)
        return fail("GainStage did not attenuate");

    mixerpro::StandardPanner panner;
    panner.setPan(0.0f);

    juce::AudioBuffer<float> mono(1, 4);
    juce::AudioBuffer<float> stereo(2, 4);

    for (int sample = 0; sample < 4; ++sample)
        mono.setSample(0, sample, 1.0f);

    panner.processMonoToStereo(mono, stereo);

    if (!nearlyEqual(stereo.getSample(0, 0), stereo.getSample(1, 0)))
        return fail("Center pan should produce equal left/right power");

    panner.setPan(-2.0f);

    if (!nearlyEqual(panner.getPan(), -1.0f))
        return fail("Panner did not clamp low value");

    panner.setPan(2.0f);

    if (!nearlyEqual(panner.getPan(), 1.0f))
        return fail("Panner did not clamp high value");

    mixerpro::LevelMeterProbe meter;
    meter.prepare(2);

    juce::AudioBuffer<float> meterBuffer(2, 4);
    meterBuffer.clear();
    meterBuffer.setSample(0, 0, 0.5f);
    meterBuffer.setSample(0, 1, -0.5f);
    meterBuffer.setSample(1, 0, 1.2f);

    meter.process(meterBuffer);

    const auto& frame = meter.getLatestFrame();

    if (!nearlyEqual(frame.peak[0], 0.5f))
        return fail("Meter peak mismatch");

    if (frame.rms[0] <= 0.0f || frame.rms[0] >= 0.5f)
        return fail("Meter RMS outside expected range");

    if (!frame.overload)
        return fail("Meter overload was not detected");

    meterBuffer.clear();
    meter.process(meterBuffer);

    if (!nearlyEqual(meter.getLatestFrame().peakHold[0], 0.5f))
        return fail("Meter peak hold did not retain previous peak");

    return 0;
}
