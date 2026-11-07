// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QString>
namespace WinJACKNexus::ControlDeck
{
inline QString moduleRelativeFolder(const QString &kind)
{
    if (kind == "sound")
        return "playback/sound";
    if (kind == "peak")
        return "meters/peak/mono";
    if (kind.startsWith("peak_"))
        return "meters/peak/" + kind.mid(5);
    if (kind.startsWith("loudness_"))
        return "meters/loudness/" + kind.mid(9);
    if (kind.startsWith("gain_"))
        return "gain/" + kind.mid(5);
    for (const auto *type : {"oscilloscope", "spectrogram", "polar", "spectrum_bars", "spectrum_curve"})
    {
        const auto prefix = QString::fromLatin1(type) + '_';
        if (kind.startsWith(prefix))
            return "analysis/" + QString::fromLatin1(type) + '/' + kind.mid(prefix.size());
    }
    return "routing/" + kind;
}
} // namespace WinJACKNexus::ControlDeck
