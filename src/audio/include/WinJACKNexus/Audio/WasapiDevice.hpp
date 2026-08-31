#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/WasapiFormat.hpp>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

constexpr std::size_t wasapiDeviceStringCapacity = 512;

struct WasapiDeviceInfo final {
    std::uint32_t index = 0;
    wchar_t id[wasapiDeviceStringCapacity] {};
    wchar_t name[wasapiDeviceStringCapacity] {};
    bool isDefault = false;
    bool isInput = false;
    bool isOutput = false;
};

using WasapiDeviceVisitor = bool (*)(const WasapiDeviceInfo& device, void* userData) noexcept;

class WINJACK_AUDIO_API WasapiDeviceEnumerator final {
public:
    WasapiDeviceEnumerator() noexcept = default;

    std::size_t enumerate(WasapiStreamDirection direction,
                          WasapiDeviceVisitor visitor,
                          void* userData) noexcept;
    bool defaultDevice(WasapiStreamDirection direction, WasapiDeviceInfo& result) noexcept;
    std::int32_t lastError() const noexcept;

private:
    std::int32_t lastError_ = 0;
};

}