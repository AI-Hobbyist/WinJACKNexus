#include "CascadeDeviceSelector.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <WinJACKNexus/AdapterBackend/DeviceEnumerator.h>
#include <WinJACKNexus/AdapterBackend/DeviceFilter.h>
#include <WinJACKNexus/Common/Audio/JackAudioOutput.h>
#include <WinJACKNexus/Common/UI/CommonControls.h>

namespace wjn::adapter
{
namespace
{

juce::String text (const char* value)
{
    return juce::String::fromUTF8 (value);
}

void showAsync (wjn::common::NexusPopupMenu menu, juce::Component& target,
                std::function<void (int)> callback)
{
    menu.showMenuAsync (wjn::common::NexusPopupMenu::Options().withTargetComponent (&target),
                        std::move (callback));
}

} // namespace

void CascadeDeviceSelector::show (juce::Component& target, bool input,
                                  const juce::StringArray& excludedDeviceIdentifiers,
                                  AudioDeviceFilterSettings settings, Callback callback)
{
    showDriverMenu (target, input, excludedDeviceIdentifiers, std::move (settings),
                    std::move (callback));
}

void CascadeDeviceSelector::showVirtual (juce::Component& target, bool input,
                                         const juce::StringArray& excludedDeviceIdentifiers,
                                         Callback callback)
{
    showVirtualDeviceMenu (target, std::move (callback), input, excludedDeviceIdentifiers);
}

void CascadeDeviceSelector::showMidi (juce::Component& target, bool input,
                                      const juce::StringArray& excludedDeviceIdentifiers,
                                      Callback callback)
{
    wjn::common::NexusPopupMenu menu;
    const auto devices = wjn::adapter::backend::enumerateMidiDevices (input);

    juce::Array<juce::MidiDeviceInfo> availableDevices;
    for (const auto& device : devices)
        if (! excludedDeviceIdentifiers.contains (device.identifier))
            availableDevices.add (device);

    for (int index = 0; index < availableDevices.size(); ++index)
        menu.addItem (index + 1, availableDevices[index].name);

    if (availableDevices.isEmpty())
        menu.addItem (1, text ("未找到 MIDI 设备"), false, false);

    showAsync (std::move (menu), target,
               [callback = std::move (callback), availableDevices, input] (int result) mutable
               {
                   if (result < 1 || result > availableDevices.size())
                       return;

                   callback ({ text ("WinMM / WinRT MIDI"),
                               input ? text ("Input") : text ("Output"),
                               availableDevices[result - 1].name,
                               availableDevices[result - 1].identifier,
                               0,
                               true });
               });
}

bool CascadeDeviceSelector::areFilterPatternsValid (const AudioDeviceFilterSettings& settings)
{
    return wjn::adapter::backend::areAudioFilterPatternsValid (settings);
}

void CascadeDeviceSelector::showVirtualDeviceMenu (juce::Component& target, Callback callback, bool input,
                                                   juce::StringArray excludedDeviceIdentifiers)
{
    wjn::common::NexusPopupMenu menu;
    const char* const inputDevices[] {
        "系统音频（Loopback）", "应用 / 游戏播放", "虚拟音频回放", "通信播放"
    };
    const char* const outputDevices[] {
        "虚拟音频输入", "通信 / 录音注入", "虚拟音频设备", "应用播放注入"
    };
    const auto& devices = input ? inputDevices : outputDevices;

    juce::StringArray availableDevices;
    for (const auto* device : devices)
        if (! excludedDeviceIdentifiers.contains (text (device)))
            availableDevices.add (text (device));

    for (int index = 0; index < availableDevices.size(); ++index)
        menu.addItem (index + 1, availableDevices[index]);

    if (availableDevices.isEmpty())
        menu.addItem (1, text ("没有可添加的虚拟设备"), false, false);

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback), input, availableDevices] (int result) mutable
               {
                   if (result < 1 || result > availableDevices.size())
                       return;

                   showChannelMenu (target, std::move (callback), "WASAPI",
                                    input ? "Loopback" : "Injector", availableDevices[result - 1],
                                    availableDevices[result - 1], juce::WASAPIDeviceMode::shared);
               });
}

void CascadeDeviceSelector::showDriverMenu (juce::Component& target, bool input,
                                            juce::StringArray excludedDeviceIdentifiers,
                                            AudioDeviceFilterSettings settings, Callback callback)
{
    wjn::common::NexusPopupMenu menu;
    menu.addItem (1, text ("WASAPI（共享 / 非独占）"));
    menu.addItem (2, text ("WASAPI（独占）"));

    showAsync (std::move (menu), target,
               [&target, input, excludedDeviceIdentifiers = std::move (excludedDeviceIdentifiers),
                settings = std::move (settings), callback = std::move (callback)] (int result) mutable
               {
                   if (result == 1 || result == 2)
                   {
                       const auto wasapiMode = result == 1 ? juce::WASAPIDeviceMode::shared
                                                           : juce::WASAPIDeviceMode::exclusive;
                       showDeviceMenu (target, std::move (callback), "WASAPI",
                                       input ? "Record" : "Playback", wasapiMode,
                                       std::move (excludedDeviceIdentifiers),
                                       std::move (settings));
                   }
               });
}

void CascadeDeviceSelector::showDeviceMenu (juce::Component& target, Callback callback,
                                            juce::String driver, juce::String streamType,
                                            juce::WASAPIDeviceMode wasapiMode,
                                            juce::StringArray excludedDeviceIdentifiers,
                                            AudioDeviceFilterSettings settings)
{
    wjn::common::NexusPopupMenu menu;
    const auto wantsInput = streamType == "Record";
    juce::StringArray availableDevices;
    for (const auto& device : wjn::adapter::backend::enumerateWasapiDevices (wantsInput, wasapiMode))
        if (! excludedDeviceIdentifiers.contains (device.identifier)
            && wjn::adapter::backend::isAudioDeviceAllowed (device.name, wantsInput, settings))
            availableDevices.add (device.name);

    for (int index = 0; index < availableDevices.size(); ++index)
        menu.addItem (index + 1, availableDevices[index]);

    if (availableDevices.isEmpty())
        menu.addItem (1, text ("未找到可添加的 WASAPI 设备"), false, false);

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback), driver, streamType, wasapiMode,
                availableDevices] (int result) mutable
               {
                   if (result >= 1 && result <= availableDevices.size())
                       showChannelMenu (target, std::move (callback), driver, streamType,
                                        availableDevices[result - 1], availableDevices[result - 1], wasapiMode);
               });
}

void CascadeDeviceSelector::showChannelMenu (juce::Component& target, Callback callback,
                                             juce::String driver, juce::String streamType,
                                             juce::String device, juce::String deviceIdentifier,
                                             juce::WASAPIDeviceMode wasapiMode)
{
    wjn::common::NexusPopupMenu menu;
    menu.addItem (1, text ("自动（按驱动获取）"));
    menu.addItem (2, "1");
    menu.addItem (3, "2");
    menu.addItem (4, "4");
    menu.addItem (5, "6");
    menu.addItem (6, "8");

    showAsync (std::move (menu), target,
               [callback = std::move (callback), driver, streamType, device,
                deviceIdentifier, wasapiMode] (int result) mutable
               {
                   if (result >= 1 && result <= 6)
                   {
                       const int channels[] { wjn::adapter::backend::detectWasapiChannelCount (
                                                  deviceIdentifier, streamType == "Record", wasapiMode),
                                              1, 2, 4, 6, 8 };
                       callback ({ driver, streamType, device, deviceIdentifier, channels[result - 1],
                                   false, wasapiMode });
                   }
               });
}

} // namespace wjn::adapter
