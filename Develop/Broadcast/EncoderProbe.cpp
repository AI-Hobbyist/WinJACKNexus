// SPDX-License-Identifier: AGPL-3.0-only
#include "Encoder.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace BC = WinJACKNexus::Broadcast;
namespace Model = WinJACKNexus::Common::Broadcast;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3 && argc != 4)
        return 1;
    try
    {
        Model::Layout layout{};
        for (auto candidate : {Model::Layout::mono, Model::Layout::stereo, Model::Layout::surround51,
                               Model::Layout::surround71})
            if (Model::layoutName(candidate) == QString::fromUtf8(argv[1]))
                layout = candidate;
        const auto rate = QString::fromUtf8(argv[2]).toUInt();
        const double tone = argc == 4 ? QString::fromUtf8(argv[3]).toDouble() : 0;
        BC::Encoder encoder(layout, rate);
        const auto channels = Model::channelCount(layout);
        constexpr std::array<double, 8> frequencies{330, 440, 550, 60, 770, 880, 990, 1100};
        QJsonArray blocks;
        std::vector<float> input;
        for (unsigned position = 0; position < rate * 2 / 5;)
        {
            const auto frames = std::min(137u, rate * 2 / 5 - position);
            input.resize(frames * channels);
            for (unsigned frame = 0; frame < frames; ++frame)
                for (unsigned channel = 0; channel < channels; ++channel)
                {
                    const auto frequency = tone > 0 ? tone : channels == 1 ? 220 : frequencies[channel];
                    input[frame * channels + channel] = static_cast<float>(
                        0.1 * std::sin(2 * std::numbers::pi * frequency * (position + frame) / rate));
                }
            for (const auto &block : encoder.push(input, position))
                blocks.append(QJsonObject{{"codec", block.codec},
                                          {"variant", block.variant},
                                          {"position", static_cast<qint64>(block.position)},
                                          {"samples", block.samples},
                                          {"payload", QString::fromLatin1(block.payload.toBase64())},
                                          {"at", double(position + frames) / rate}});
            position += frames;
        }
        const QJsonObject output{{"opusHeads", encoder.opusHeads()}, {"blocks", blocks}};
        const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Compact);
        std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
