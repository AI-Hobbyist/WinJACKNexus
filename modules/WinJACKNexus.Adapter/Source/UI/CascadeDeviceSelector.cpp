#include "CascadeDeviceSelector.h"

#include <juce_audio_devices/juce_audio_devices.h>

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

int detectWdmChannelCount (const juce::String& streamType, const juce::String& deviceName,
                           juce::WASAPIDeviceMode wasapiMode)
{
    const auto wantsInput = streamType == "Record";
    if (deviceName.isEmpty() || (! wantsInput && streamType != "Playback"))
        return 2;

    std::unique_ptr<juce::AudioIODeviceType> deviceType (
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (wasapiMode));
    if (deviceType == nullptr)
        return 2;

    deviceType->scanForDevices();
    std::unique_ptr<juce::AudioIODevice> device (
        deviceType->createDevice (wantsInput ? juce::String() : deviceName,
                                  wantsInput ? deviceName : juce::String()));
    if (device == nullptr)
        return 2;

    const auto channelNames = wantsInput ? device->getInputChannelNames()
                                         : device->getOutputChannelNames();
    return juce::jlimit (1, wjn::common::JackAudioOutput::maxChannels, channelNames.size());
}

} // namespace

void CascadeDeviceSelector::show (juce::Component& target,
                                  const juce::StringArray& excludedDeviceIdentifiers, Callback callback)
{
    showDriverMenu (target, excludedDeviceIdentifiers, std::move (callback));
}

void CascadeDeviceSelector::showVirtual (juce::Component& target, bool input,
                                         const juce::StringArray& excludedDeviceIdentifiers, Callback callback)
{
    showVirtualDeviceMenu (target, std::move (callback), input, excludedDeviceIdentifiers);
}

void CascadeDeviceSelector::showMidi (juce::Component& target, bool input,
                                      const juce::StringArray& excludedDeviceIdentifiers, Callback callback)
{
    wjn::common::NexusPopupMenu menu;
    const auto devices = input ? juce::MidiInput::getAvailableDevices()
                               : juce::MidiOutput::getAvailableDevices();

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

void CascadeDeviceSelector::showVirtualDeviceMenu (juce::Component& target, Callback callback, bool input,
                                                    const juce::StringArray& excludedDeviceIdentifiers)
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

void CascadeDeviceSelector::showDriverMenu (juce::Component& target,
                                            const juce::StringArray& excludedDeviceIdentifiers, Callback callback)
{
    wjn::common::NexusPopupMenu menu;
    menu.addItem (1, text ("WASAPI（共享 / 非独占）"));
    menu.addItem (2, text ("WASAPI（独占）"));

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback), excludedDeviceIdentifiers] (int result) mutable
               {
                   if (result == 1 || result == 2)
                   {
                       const auto wasapiMode = result == 1 ? juce::WASAPIDeviceMode::shared
                                                           : juce::WASAPIDeviceMode::exclusive;
                       showStreamMenu (target, excludedDeviceIdentifiers, std::move (callback),
                                       "WASAPI", wasapiMode);
                   }
               });
}

void CascadeDeviceSelector::showStreamMenu (juce::Component& target,
                                            const juce::StringArray& excludedDeviceIdentifiers, Callback callback,
                                            juce::String driver, juce::WASAPIDeviceMode wasapiMode)
{
    wjn::common::NexusPopupMenu menu;
    menu.addItem (1, text ("播放"));
    menu.addItem (2, text ("录音"));

    showAsync (std::move (menu), target,
               [&target, callback = std::move (callback), driver, wasapiMode,
                excludedDeviceIdentifiers] (int result) mutable
               {
                   if (result == 1 || result == 2)
                       showDeviceMenu (target, std::move (callback), driver,
                                       result == 1 ? "Playback" : "Record", excludedDeviceIdentifiers,
                                       wasapiMode);
               });
}

void CascadeDeviceSelector::showDeviceMenu (juce::Component& target, Callback callback,
                                            juce::String driver, juce::String streamType,
                                            const juce::StringArray& excludedDeviceIdentifiers,
                                            juce::WASAPIDeviceMode wasapiMode)
{
    wjn::common::NexusPopupMenu menu;
    std::unique_ptr<juce::AudioIODeviceType> deviceType (
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (wasapiMode));
    if (deviceType != nullptr)
        deviceType->scanForDevices();

    const auto wantsInput = streamType == "Record";
    const auto devices = deviceType != nullptr ? deviceType->getDeviceNames (wantsInput)
                                               : juce::StringArray();
    juce::StringArray availableDevices;
    for (const auto& device : devices)
        if (! excludedDeviceIdentifiers.contains (device))
            availableDevices.add (device);

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
                       const int channels[] { detectWdmChannelCount (streamType, deviceIdentifier, wasapiMode),
                                              1, 2, 4, 6, 8 };
                       callback ({ driver, streamType, device, deviceIdentifier, channels[result - 1],
                                   false, wasapiMode });
                   }
               });
}

} // namespace wjn::adapter
