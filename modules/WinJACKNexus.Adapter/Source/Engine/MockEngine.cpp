#include "MockEngine.h"

#include <cmath>

namespace wjn::adapter
{

MockEngine::MockEngine() = default;

MockEngine::~MockEngine()
{
    stop();
}

void MockEngine::setAudioCallback (AudioCallback callback)
{
    audioCallback = std::move (callback);
}

void MockEngine::setMidiCallback (MidiCallback callback)
{
    midiCallback = std::move (callback);
}

void MockEngine::start (bool shouldUseMidi)
{
    midiMode = shouldUseMidi;
    tick = 0;
    phase = 0.0f;
    startTimerHz (25);
}

void MockEngine::stop()
{
    stopTimer();
    audioCallback = nullptr;
    midiCallback = nullptr;
}

void MockEngine::timerCallback()
{
    ++tick;
    phase += 0.13f;

    if (midiMode)
    {
        if (tick % 8 == 0 && midiCallback != nullptr)
            midiCallback();
        return;
    }

    const auto level = 0.18f + 0.38f * (0.5f + 0.5f * std::sin (phase));
    const auto clipping = tick % 97 == 0;
    if (audioCallback != nullptr)
        audioCallback (level, clipping);
}

} // namespace wjn::adapter
