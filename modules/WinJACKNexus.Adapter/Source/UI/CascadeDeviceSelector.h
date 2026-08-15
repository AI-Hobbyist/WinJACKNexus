#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace wjn::adapter
{

class CascadeDeviceSelector final
{
public:
    struct AudioDeviceFilterSettings
    {
        juce::String virtualDevicePattern = "virtual audio cable";
        juce::String inputDevicePattern = R"(\bLine\s*\d*[13579]\b)";
        juce::String outputDevicePattern = R"(\bLine\s*\d*[02468]\b)";
    };

    struct Selection
    {
        juce::String driver;
        juce::String streamType;
        juce::String device;
        juce::String deviceIdentifier;
        int channels = 2;
        bool midi = false;
        juce::WASAPIDeviceMode wasapiMode = juce::WASAPIDeviceMode::shared;
    };

    using Callback = std::function<void (Selection)>;

    static void show (juce::Component& target, bool input,
                      const juce::StringArray& excludedDeviceIdentifiers,
                      AudioDeviceFilterSettings settings, Callback callback);
    static void showVirtual (juce::Component& target, bool input,
                             const juce::StringArray& excludedDeviceIdentifiers,
                             Callback callback);
    static void showMidi (juce::Component& target, bool input,
                          const juce::StringArray& excludedDeviceIdentifiers,
                          Callback callback);
    static bool areFilterPatternsValid (const AudioDeviceFilterSettings& settings);

private:
    static void showDriverMenu (juce::Component& target, bool input,
                                juce::StringArray excludedDeviceIdentifiers,
                                AudioDeviceFilterSettings settings, Callback callback);
    static void showDeviceMenu (juce::Component& target, Callback callback,
                                juce::String driver, juce::String streamType,
                                juce::WASAPIDeviceMode wasapiMode,
                                juce::StringArray excludedDeviceIdentifiers,
                                AudioDeviceFilterSettings settings);
    static void showVirtualDeviceMenu (juce::Component& target, Callback callback, bool input,
                                       juce::StringArray excludedDeviceIdentifiers);
    static void showChannelMenu (juce::Component& target, Callback callback,
                                 juce::String driver, juce::String streamType,
                                 juce::String device, juce::String deviceIdentifier = {},
                                 juce::WASAPIDeviceMode wasapiMode = juce::WASAPIDeviceMode::shared);
};

} // namespace wjn::adapter
