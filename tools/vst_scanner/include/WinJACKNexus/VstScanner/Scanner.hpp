/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <functional>
#include <optional>

#include <QString>
#include <QStringList>
#include <QVector>

namespace WinJACKNexus::VstScanner {

struct BusInfo final {
    QString name;
    QString type;
    int channels {};
    bool isInput {};
};

struct PluginInfo final {
    QString name;
    QString vendor;
    QString version;
    QString type;
    QString path;
    QString category;
    QString subcategory;
    int bits {};
    bool isInstrument {};
    int audioIns {};
    int audioOuts {};
    bool midiIn {};
    bool midiOut {};
    bool verified {};
    int vst2ProbeVersion {};
    QString internalPlugin;
    QString infoFile;
    QVector<BusInfo> buses;
};

using ScanProgressCallback = std::function<void(int completed, int total, const QString& currentPath)>;
using ScanInfoWrittenCallback = std::function<void(const PluginInfo& info)>;

struct ScanOptions final {
    QStringList searchPaths;
    QString outputDirectory;
    ScanProgressCallback progressCallback;
    ScanInfoWrittenCallback infoWrittenCallback;
    bool rescanVerifiedPlugins {};
};

struct ScanMessage final {
    QString path;
    QString text;
    bool error {};
};

struct ScanReport final {
    QVector<PluginInfo> plugins;
    QVector<ScanMessage> messages;
};

class Scanner final {
public:
    ScanReport scan(const ScanOptions& options) const;

    static std::optional<PluginInfo> loadInfoFile(const QString& infoFile);
};

}