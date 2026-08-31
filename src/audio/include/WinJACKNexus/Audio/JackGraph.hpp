#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>

#include <cstddef>

namespace WinJACKNexus::Audio {

class WINJACK_AUDIO_API JackGraph final {
public:
    static std::size_t visitPortNames(JackClient& client,
                                      const char* portNamePattern,
                                      const char* typeNamePattern,
                                      unsigned long flags,
                                      JackPortNameVisitor visitor,
                                      void* userData) noexcept;
    static bool connect(JackClient& client,
                        const char* sourcePort,
                        const char* destinationPort) noexcept;
    static bool disconnect(JackClient& client,
                           const char* sourcePort,
                           const char* destinationPort) noexcept;
    static bool portExists(JackClient& client, const char* portName) noexcept;
};

}