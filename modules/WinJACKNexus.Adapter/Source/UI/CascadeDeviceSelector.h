#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace wjn::adapter
{

class CascadeDeviceSelector final
{
public:
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

    static void show (juce::Component& target, const juce::StringArray& excludedDeviceIdentifiers,
                      Callback callback);
    static void showVirtual (juce::Component& target, bool input,
                             const juce::StringArray& excludedDeviceIdentifiers, Callback callback);
    static void showMidi (juce::Component& target, bool input,
                          const juce::StringArray& excludedDeviceIdentifiers, Callback callback);

private:
    static void showDriverMenu (juce::Component& target, const juce::StringArray& excludedDeviceIdentifiers,
                                Callback callback);
    static void showStreamMenu (juce::Component& target, const juce::StringArray& excludedDeviceIdentifiers,
                                Callback callback, juce::String driver,
                                juce::WASAPIDeviceMode wasapiMode);
    static void showDeviceMenu (juce::Component& target, Callback callback,
                                juce::String driver, juce::String streamType,
                                const juce::StringArray& excludedDeviceIdentifiers,
                                juce::WASAPIDeviceMode wasapiMode);
    static void showVirtualDeviceMenu (juce::Component& target, Callback callback, bool input,
                                       const juce::StringArray& excludedDeviceIdentifiers);
    static void showChannelMenu (juce::Component& target, Callback callback,
                                 juce::String driver, juce::String streamType,
                                 juce::String device, juce::String deviceIdentifier = {},
                                 juce::WASAPIDeviceMode wasapiMode = juce::WASAPIDeviceMode::shared);
};

} // namespace wjn::adapter
