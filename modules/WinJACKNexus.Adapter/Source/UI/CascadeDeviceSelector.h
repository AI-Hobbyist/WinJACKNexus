#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
        int channels = 2;
    };

    using Callback = std::function<void (Selection)>;

    static void show (juce::Component& target, Callback callback);
    static void showMidi (juce::Component& target, bool input, Callback callback);

private:
    static void showDriverMenu (juce::Component& target, Callback callback);
    static void showStreamMenu (juce::Component& target, Callback callback, juce::String driver);
    static void showDeviceMenu (juce::Component& target, Callback callback,
                                juce::String driver, juce::String streamType);
    static void showChannelMenu (juce::Component& target, Callback callback,
                                 juce::String driver, juce::String streamType,
                                 juce::String device);
};

} // namespace wjn::adapter
