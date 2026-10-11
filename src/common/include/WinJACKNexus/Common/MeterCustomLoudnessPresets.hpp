/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/Export.hpp>
#include <QList>
#include <QString>
#include <optional>

namespace WinJACKNexus::Common
{
struct MeterCustomLoudnessPreset final
{
    QString id;
    QString displayName;
    double targetIntegratedLufs = -23.0;
    std::optional<double> alternateTargetHighLufs;
    double maximumTruePeakDbtp = -1.0;
};
WINJACK_COMMON_API QList<MeterCustomLoudnessPreset> customMeterLoudnessPresets();
WINJACK_COMMON_API std::optional<MeterCustomLoudnessPreset> findCustomMeterLoudnessPreset(
    const QString &id);
WINJACK_COMMON_API quint64 customMeterLoudnessPresetRevision();
WINJACK_COMMON_API void refreshCustomMeterLoudnessPresets();
WINJACK_COMMON_API bool saveCustomMeterLoudnessPreset(MeterCustomLoudnessPreset &preset,
                                                      QString *error = nullptr);
}
