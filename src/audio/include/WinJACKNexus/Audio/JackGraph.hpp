/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
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