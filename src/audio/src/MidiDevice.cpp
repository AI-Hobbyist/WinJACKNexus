#include <WinJACKNexus/Audio/MidiDevice.hpp>

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cwchar>

namespace WinJACKNexus::Audio {

namespace {

void copyName(wchar_t* destination, std::size_t capacity, const wchar_t* source) noexcept
{
    if (destination == nullptr || capacity == 0)
        return;
    destination[0] = L'\0';
    if (source != nullptr)
        wcsncpy_s(destination, capacity, source, _TRUNCATE);
}

}

std::size_t MidiDeviceEnumerator::enumerate(MidiDirection direction,
                                            MidiDeviceVisitor visitor,
                                            void* userData) noexcept
{
    lastError_ = MMSYSERR_NOERROR;
    const auto count = direction == MidiDirection::input ? midiInGetNumDevs() : midiOutGetNumDevs();

    std::size_t visited = 0;
    for (UINT deviceId = 0; deviceId < count; ++deviceId) {
        MidiDeviceInfo info;
        info.deviceId = deviceId;
        info.isInput = direction == MidiDirection::input;
        info.isOutput = direction == MidiDirection::output;
        MMRESULT result = MMSYSERR_NOERROR;
        if (direction == MidiDirection::input) {
            MIDIINCAPSW capabilities {};
            result = midiInGetDevCapsW(deviceId, &capabilities, sizeof(capabilities));
            if (result == MMSYSERR_NOERROR)
                copyName(info.name, std::size(info.name), capabilities.szPname);
        } else {
            MIDIOUTCAPSW capabilities {};
            result = midiOutGetDevCapsW(deviceId, &capabilities, sizeof(capabilities));
            if (result == MMSYSERR_NOERROR)
                copyName(info.name, std::size(info.name), capabilities.szPname);
        }
        if (result != MMSYSERR_NOERROR) {
            lastError_ = result;
            continue;
        }

        ++visited;
        if (visitor != nullptr && !visitor(info, userData))
            break;
    }
    return visited;
}

bool MidiDeviceEnumerator::firstDevice(MidiDirection direction, MidiDeviceInfo& result) noexcept
{
    result = {};
    struct Context final {
        MidiDeviceInfo* result;
    } context { &result };
    const auto visitor = [](const MidiDeviceInfo& device, void* userData) noexcept {
        auto* context = static_cast<Context*>(userData);
        if (context == nullptr || context->result == nullptr)
            return false;
        *context->result = device;
        return false;
    };
    return enumerate(direction, visitor, &context) != 0;
}

std::uint32_t MidiDeviceEnumerator::lastError() const noexcept
{
    return lastError_;
}

}