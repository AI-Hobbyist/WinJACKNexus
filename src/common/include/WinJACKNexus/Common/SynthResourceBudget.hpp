// Global loader policy; never read settings on the audio thread.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QSettings>
#include <algorithm>
#include <cstdint>

namespace WinJACKNexus::Common::MidiTools
{
inline constexpr int defaultResourceBudgetMiB = 64;
inline constexpr int maximumResourceBudgetMiB = 4096;
inline constexpr uint64_t sf2PreparedExpansion = 8;

inline int resourceBudgetMiB()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    bool valid = false;
    const int value =
        settings.value(QStringLiteral("sf2LoaderResourceBudgetMiB"), defaultResourceBudgetMiB)
            .toInt(&valid);
    return valid ? std::clamp(value, 1, maximumResourceBudgetMiB) : defaultResourceBudgetMiB;
}

inline void setResourceBudgetMiB(int value)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("WinJACKNexus"), QStringLiteral("Application"));
    settings.setValue(QStringLiteral("sf2LoaderResourceBudgetMiB"),
                      std::clamp(value, 1, maximumResourceBudgetMiB));
    settings.sync();
}

inline uint64_t configuredPreparedSampleBytes()
{
    return uint64_t(resourceBudgetMiB()) * 1024 * 1024 * sf2PreparedExpansion;
}
} // namespace WinJACKNexus::Common::MidiTools
