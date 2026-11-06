// SPDX-License-Identifier: AGPL-3.0-only
#include "Wire.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <cstdio>
#include <limits>

namespace BC = WinJACKNexus::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2)
        return 1;
    QFile file(QString::fromUtf8(argv[1]));
    if (!file.open(QIODevice::ReadOnly))
        return 1;
    const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
    int failures = 0;
    auto check = [&](bool value, const char *message)
    {
        std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
        failures += !value;
    };
    BC::DatagramContext context;
    context.session = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
    context.key = QByteArray::fromHex(fixture.value("sessionKeyHex").toString().toLatin1());
    context.nextSequence = 17;
    const float input[]{0.25F, 0.75F, -0.5F, -1.0F};
    BC::AudioBlock block{1, 0, 7, 1, 960, 2, BC::pcm16(input, 4)};
    const auto packets = BC::datagrams(context, block);
    check(packets.size() == 1 && packets[0].toHex() == fixture.value("udpPcmStereoHex").toString().toLatin1(),
          "C++ PCM/header/HMAC exactly matches frozen cross-language datagram");
    check(context.nextSequence == 18, "sequence advances once for every authenticated datagram");
    block.samples = 960;
    block.payload = QByteArray(15360, '\x12');
    const auto fragments = BC::datagrams(context, block);
    bool valid = fragments.size() == 14;
    QByteArray reassembled;
    for (unsigned index = 0; index < fragments.size(); ++index)
    {
        const auto &packet = fragments[index];
        valid &= packet.size() <= 1200 && qFromLittleEndian<quint16>(packet.constData() + 50) == index &&
                 qFromLittleEndian<quint16>(packet.constData() + 52) == 14;
        reassembled.append(packet.mid(64, packet.size() - 96));
    }
    check(valid && reassembled == block.payload,
          "7.1 20ms block fragments without IP fragmentation or byte loss");
    const float clipped[]{2, -2, std::numeric_limits<float>::quiet_NaN()};
    check(BC::pcm16(clipped, 3).toHex() == "ff7f00800000",
          "PCM conversion clips finite audio and silences NaN");
    const auto before = context.nextSequence;
    block.payload = QByteArray(1104 * 32 + 1, '\0');
    check(BC::datagrams(context, block).empty() && context.nextSequence == before,
          "oversize block is rejected before consuming a sequence");
    return failures ? 1 : 0;
}
