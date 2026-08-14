#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace wjn::adapter::debug
{

inline juce::String pointerText (const void* pointer)
{
    return juce::String::toHexString (static_cast<juce::int64> (reinterpret_cast<std::uintptr_t> (pointer)));
}

inline void trace (const juce::String& message)
{
    static juce::CriticalSection mutex;
    const juce::ScopedLock lock (mutex);
    const auto line = juce::Time::getCurrentTime().toISO8601 (true)
                    + " " + message + juce::newLine;
    juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("WinJACKNexus.Adapter.add-device.log")
        .appendText (line, false, false);
#if JUCE_WINDOWS
    OutputDebugStringA (line.toRawUTF8());
#endif
}

} // namespace wjn::adapter::debug
