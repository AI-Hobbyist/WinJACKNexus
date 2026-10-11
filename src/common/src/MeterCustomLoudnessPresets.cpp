/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/MeterCustomLoudnessPresets.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QDebug>
#include <cmath>
#include <algorithm>

namespace WinJACKNexus::Common
{
namespace
{
QList<MeterCustomLoudnessPreset> presets;
bool loaded = false;
quint64 revision = 0;
QString directory()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/lufs_presets");
}
bool valid(const MeterCustomLoudnessPreset &p)
{
    return !p.displayName.trimmed().isEmpty() && std::isfinite(p.targetIntegratedLufs) &&
           p.targetIntegratedLufs >= -70 && p.targetIntegratedLufs <= 0 &&
           std::isfinite(p.maximumTruePeakDbtp) && p.maximumTruePeakDbtp >= -60 &&
           p.maximumTruePeakDbtp <= 12 &&
           (!p.alternateTargetHighLufs || (std::isfinite(*p.alternateTargetHighLufs) &&
                                           *p.alternateTargetHighLufs >= p.targetIntegratedLufs &&
                                           *p.alternateTargetHighLufs <= 0));
}
}
void refreshCustomMeterLoudnessPresets()
{
    presets.clear();
    loaded = true;
    ++revision;
    for (const auto &entry :
         QDir(directory()).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
            continue;
        const auto o = QJsonDocument::fromJson(file.readAll()).object();
        MeterCustomLoudnessPreset p;
        p.id = o.value("id").toString();
        p.displayName = o.value("name").toString();
        p.targetIntegratedLufs = o.value("targetIntegratedLufs").toDouble(1);
        p.maximumTruePeakDbtp = o.value("maximumTruePeakDbtp").toDouble(100);
        const auto upper = o.value("alternateTargetHighLufs");
        if (!upper.isNull() && !upper.isUndefined())
            p.alternateTargetHighLufs = upper.toDouble(1);
        if (o.value("format").toString() != QStringLiteral("WinJACKNexus.LufsPreset") ||
            o.value("version").toDouble(-1) != 1 || !p.id.startsWith(QStringLiteral("custom-")) ||
            !valid(p) || std::any_of(presets.begin(), presets.end(), [&](const auto &item) {
                return item.id == p.id;
            }))
        {
            qWarning().noquote() << "Invalid LUFS preset:" << entry.fileName();
            continue;
        }
        presets.push_back(p);
    }
}
quint64 customMeterLoudnessPresetRevision()
{
    if (!loaded)
        refreshCustomMeterLoudnessPresets();
    return revision;
}
QList<MeterCustomLoudnessPreset> customMeterLoudnessPresets()
{
    if (!loaded)
        refreshCustomMeterLoudnessPresets();
    return presets;
}
std::optional<MeterCustomLoudnessPreset> findCustomMeterLoudnessPreset(const QString &id)
{
    if (!loaded)
        refreshCustomMeterLoudnessPresets();
    for (const auto &p : presets)
        if (p.id == id)
            return p;
    return std::nullopt;
}
bool saveCustomMeterLoudnessPreset(MeterCustomLoudnessPreset &preset, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!valid(preset))
        return fail(QStringLiteral("预设名称或参考值无效"));
    const auto id = QStringLiteral("custom-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject o{{"format", QStringLiteral("WinJACKNexus.LufsPreset")},
                  {"version", 1},
                  {"id", id},
                  {"name", preset.displayName.trimmed()},
                  {"targetIntegratedLufs", preset.targetIntegratedLufs},
                  {"maximumTruePeakDbtp", preset.maximumTruePeakDbtp}};
    if (preset.alternateTargetHighLufs)
        o.insert("alternateTargetHighLufs", *preset.alternateTargetHighLufs);
    if (!QDir().mkpath(directory()))
        return fail(QStringLiteral("无法创建目录：%1").arg(directory()));
    QSaveFile file(directory() + QLatin1Char('/') + id + QStringLiteral(".json"));
    const auto bytes = QJsonDocument(o).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(file.errorString());
    preset.id = id;
    refreshCustomMeterLoudnessPresets();
    return true;
}
}
