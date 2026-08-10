#pragma once

#include <juce_events/juce_events.h>

namespace wjn::adapter
{

class MockEngine final : private juce::Timer
{
public:
    using AudioCallback = std::function<void (float, bool)>;
    using MidiCallback = std::function<void()>;

    MockEngine();
    ~MockEngine() override;

    void setAudioCallback (AudioCallback callback);
    void setMidiCallback (MidiCallback callback);
    void start (bool midiMode);
    void stop();

private:
    void timerCallback() override;

    AudioCallback audioCallback;
    MidiCallback midiCallback;
    bool midiMode = false;
    int tick = 0;
    float phase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockEngine)
};

} // namespace wjn::adapter
