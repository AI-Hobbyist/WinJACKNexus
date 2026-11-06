// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Wire.hpp"
#include <QJsonObject>
#include <WinJACKNexus/Common/BroadcastProcessor.hpp>
#include <memory>
#include <span>

namespace WinJACKNexus::Broadcast
{
// One stream/format epoch on the worker. Input is canonical post-gain/M float;
// PCM preserves the source clock, Opus full/mono variants share a 48kHz clock.
class Encoder final
{
  public:
    Encoder(Common::Broadcast::Layout layout, std::uint32_t rate, std::uint32_t handle = 7,
            std::uint32_t epoch = 1);
    ~Encoder();
    Encoder(const Encoder &) = delete;
    Encoder &operator=(const Encoder &) = delete;
    QJsonObject opusHeads() const;
    std::vector<AudioBlock> push(std::span<const float> interleaved, std::uint64_t position);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace WinJACKNexus::Broadcast
