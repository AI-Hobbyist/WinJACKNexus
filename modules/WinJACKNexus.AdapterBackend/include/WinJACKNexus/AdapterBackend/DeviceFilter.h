#pragma once

#include <juce_core/juce_core.h>

namespace wjn::adapter::backend
{

struct AudioDeviceFilterSettings
{
    juce::String virtualDevicePattern = "virtual audio cable";
    juce::String inputDevicePattern = R"(\bLine\s*\d*[13579]\b)";
    juce::String outputDevicePattern = R"(\bLine\s*\d*[02468]\b)";
};

bool areAudioFilterPatternsValid (const AudioDeviceFilterSettings& settings);
bool isAudioDeviceAllowed (const juce::String& deviceName, bool wantsInput,
                           const AudioDeviceFilterSettings& settings);

} // namespace wjn::adapter::backend
