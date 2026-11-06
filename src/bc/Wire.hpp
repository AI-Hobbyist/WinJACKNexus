// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace WinJACKNexus::Broadcast
{
struct DatagramContext
{
    QByteArray session;
    QByteArray key;
    std::uint64_t nextSequence = 1;
};

struct AudioBlock
{
    std::uint8_t codec = 1;
    std::uint8_t variant = 0;
    std::uint32_t handle = 0;
    std::uint32_t epoch = 0;
    std::uint64_t position = 0;
    std::uint16_t samples = 0;
    QByteArray payload;
};

// Worker-side conversion/packetization only. Never call from the JACK callback.
QByteArray pcm16(const float *samples, std::size_t count);
std::vector<QByteArray> datagrams(DatagramContext &context, const AudioBlock &block,
                                  QString *error = nullptr);
} // namespace WinJACKNexus::Broadcast
