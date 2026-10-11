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
#include <WinJACKNexus/Audio/JackClient.hpp>

#include <cstddef>

namespace WinJACKNexus::Audio
{

using JackPortNameVisitor = bool (*)(const char *name, void *userData) noexcept;

class WINJACK_AUDIO_API JackPort final
{
  public:
    JackPort() noexcept = default;
    ~JackPort() noexcept;

    JackPort(const JackPort &) = delete;
    JackPort &operator=(const JackPort &) = delete;
    JackPort(JackPort &&other) noexcept;
    JackPort &operator=(JackPort &&other) noexcept;

    bool registerPort(JackClient &client, const char *portName, const char *portType,
                      unsigned long flags, unsigned long bufferSize = 0) noexcept;
    bool unregisterPort() noexcept;
    bool rename(const char *shortName) noexcept;

    bool isValid() const noexcept;
    jack_port_t *nativeHandle() const noexcept;
    void *buffer(jack_nframes_t frames) const noexcept;
    jack_default_audio_sample_t *audioBuffer(jack_nframes_t frames) const noexcept;
    void *midiBuffer(jack_nframes_t frames) const noexcept;
    const char *name() const noexcept;
    const char *shortName() const noexcept;
    const char *type() const noexcept;
    int flags() const noexcept;
    bool isInput() const noexcept;
    bool isOutput() const noexcept;
    std::size_t visitConnections(JackPortNameVisitor visitor, void *userData) const noexcept;

  private:
    JackClient *client_ = nullptr;
    jack_port_t *port_ = nullptr;
};

}
