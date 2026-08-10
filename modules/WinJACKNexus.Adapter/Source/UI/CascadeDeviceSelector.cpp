#include "CascadeDeviceSelector.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace wjn::adapter
{
namespace
{

juce::String text (const char* value)
{
    return juce::String::fromUTF8 (value);
}

void showAsync (juce::PopupMenu menu, juce::Component& target,
                std::function<void (int)> callback)
{
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        std::move (callback));
}

} // namespace

void CascadeDeviceSelector::show (juce::Component& target, Callback callback)
{
    showDriverMenu (target, std::move (callback));
}

void CascadeDeviceSelector::showMidi (juce::Component& target, bool input, Callback callback)
{
    juce::PopupMenu menu;
    const auto devices = input ? juce::MidiInput::getAvailableDevices()
                               : juce::MidiOutput::getAvailableDevices();

    for (int index = 0; index < devices.size(); ++index)
        menu.addItem (index + 1, devices[index].name);

    if (devices.isEmpty())
        menu.addItem (1, text ("未找到 MIDI 设备"), false, false);

    showAsync (std::move (menu), target,
               [callback = std::move (callback), devices, input] (int result) mutable
               {
                   if (result < 1 || result > devices.size())
                       return;

                   callback ({ text ("WinMM / WinRT MIDI"),
                               input ? text ("Input") : text ("Output"),
                               devices[result - 1].name,
                               0 });
               });
}

void CascadeDeviceSelector::showDriverMenu (juce::Component& target, Callback callback)
{
    juce::PopupMenu menu;
    menu.addItem (1, "WASAPI");
    menu.addItem (2, "MME");
    menu.addItem (3, "KS");

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback)] (int result) mutable
               {
                   if (result >= 1 && result <= 3)
                   {
                       const char* drivers[] { "WASAPI", "MME", "KS" };
                       showStreamMenu (target, std::move (callback), drivers[result - 1]);
                   }
               });
}

void CascadeDeviceSelector::showStreamMenu (juce::Component& target, Callback callback,
                                            juce::String driver)
{
    juce::PopupMenu menu;
    menu.addItem (1, text ("Playback"));
    menu.addItem (2, text ("Record"));

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback), driver] (int result) mutable
               {
                   if (result == 1 || result == 2)
                       showDeviceMenu (target, std::move (callback), driver,
                                       result == 1 ? text ("Playback") : text ("Record"));
               });
}

void CascadeDeviceSelector::showDeviceMenu (juce::Component& target, Callback callback,
                                            juce::String driver, juce::String streamType)
{
    juce::PopupMenu menu;
    menu.addItem (1, text ("系统默认设备"));
    menu.addItem (2, text ("扬声器 / 耳机"));
    menu.addItem (3, text ("麦克风 / 线路输入"));
    menu.addItem (4, text ("虚拟音频设备"));

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback), driver, streamType] (int result) mutable
               {
                   if (result >= 1 && result <= 4)
                   {
                       const char* devices[] {
                           "系统默认设备", "扬声器 / 耳机", "麦克风 / 线路输入", "虚拟音频设备"
                       };
                       showChannelMenu (target, std::move (callback), driver, streamType,
                                        text (devices[result - 1]));
                   }
               });
}

void CascadeDeviceSelector::showChannelMenu (juce::Component& target, Callback callback,
                                             juce::String driver, juce::String streamType,
                                             juce::String device)
{
    juce::PopupMenu menu;
    menu.addItem (1, text ("自动（按驱动获取）"));
    menu.addItem (2, "1");
    menu.addItem (3, "2");
    menu.addItem (4, "4");
    menu.addItem (5, "6");
    menu.addItem (6, "8");

    showAsync (std::move (menu), target,
               [callback = std::move (callback), driver, streamType, device] (int result) mutable
               {
                   if (result >= 1 && result <= 6)
                   {
                       const int channels[] { 2, 1, 2, 4, 6, 8 };
                       callback ({ driver, streamType, device, channels[result - 1] });
                   }
               });
}

} // namespace wjn::adapter
