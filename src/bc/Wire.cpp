// SPDX-License-Identifier: AGPL-3.0-only
#include "Wire.hpp"
#include <QMessageAuthenticationCode>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace WinJACKNexus::Broadcast
{
QByteArray pcm16(const float *samples, std::size_t count)
{
    QByteArray result(static_cast<qsizetype>(count * 2), Qt::Uninitialized);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto value =
            std::isfinite(samples[index]) ? std::clamp(samples[index], -1.0F, 32767.0F / 32768.0F) : 0;
        const auto quantized = static_cast<qint16>(std::lround(value * 32768));
        qToLittleEndian(quantized, result.data() + index * 2);
    }
    return result;
}

std::vector<QByteArray> datagrams(DatagramContext &context, const AudioBlock &block, QString *error)
{
    constexpr qsizetype payloadLimit = 1104;
    const auto count = (block.payload.size() + payloadLimit - 1) / payloadLimit;
    if (context.session.size() != 16 || context.key.size() != 32 || count < 1 || count > 32 ||
        (block.codec != 1 && block.codec != 2) || (block.codec == 1 && block.variant != 0) ||
        block.variant > 8 || block.handle == 0 || block.epoch == 0 || block.samples == 0 ||
        (block.codec == 2 && block.samples != 960) ||
        context.nextSequence > std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(count))
    {
        if (error)
            *error = QStringLiteral("Invalid audio block or authenticated UDP packet budget exceeded");
        return {};
    }
    std::vector<QByteArray> result;
    result.reserve(static_cast<std::size_t>(count));
    for (qsizetype index = 0; index < count; ++index)
    {
        const auto payload = block.payload.mid(index * payloadLimit, payloadLimit);
        QByteArray packet(64, '\0');
        packet.replace(0, 4, "WJBC");
        packet[4] = 1;
        packet[5] = static_cast<char>(block.codec);
        packet[6] = static_cast<char>(block.variant);
        packet.replace(8, 16, context.session);
        qToLittleEndian(block.handle, packet.data() + 24);
        qToLittleEndian(block.epoch, packet.data() + 28);
        qToLittleEndian(context.nextSequence + static_cast<std::uint64_t>(index), packet.data() + 32);
        qToLittleEndian(block.position, packet.data() + 40);
        qToLittleEndian(block.samples, packet.data() + 48);
        qToLittleEndian(static_cast<quint16>(index), packet.data() + 50);
        qToLittleEndian(static_cast<quint16>(count), packet.data() + 52);
        qToLittleEndian(static_cast<quint32>(payload.size()), packet.data() + 56);
        packet.append(payload);
        packet.append(QMessageAuthenticationCode::hash(packet, context.key, QCryptographicHash::Sha256));
        result.push_back(std::move(packet));
    }
    context.nextSequence += static_cast<std::uint64_t>(count);
    return result;
}
} // namespace WinJACKNexus::Broadcast
