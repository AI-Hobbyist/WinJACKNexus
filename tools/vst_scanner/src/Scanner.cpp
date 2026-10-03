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
#include <WinJACKNexus/VstScanner/Scanner.hpp>

#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>

#include "pluginterfaces/base/ipluginbase.h"
#include "public.sdk/source/vst/moduleinfo/moduleinfoparser.h"

#ifndef WJN_ENABLE_VST2
#    define WJN_ENABLE_VST2 0
#endif

#if WJN_ENABLE_VST2
#    include "pluginterfaces/vst2.x/aeffectx.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace WinJACKNexus::VstScanner {
namespace {

// Keep the core usable without QApplication or a UI dependency. Translation is
// scoped to this scan request, including nested scans and exception unwinding.
thread_local const ScanTextCallback* scanTextCallback = nullptr;
class ScanLanguageScope final {
public:
    explicit ScanLanguageScope(const ScanTextCallback& callback)
        : previous_(scanTextCallback) { scanTextCallback = &callback; }
    ~ScanLanguageScope() { scanTextCallback = previous_; }
    ScanLanguageScope(const ScanLanguageScope&) = delete;
    ScanLanguageScope& operator=(const ScanLanguageScope&) = delete;
private:
    const ScanTextCallback* previous_;
};
QString scannerText(const char* key, const char* english)
{
    return scanTextCallback && *scanTextCallback
        ? (*scanTextCallback)(key, english) : QString::fromUtf8(english);
}


namespace fs = std::filesystem;

constexpr int currentVst2ProbeVersion = 1;

QString pathString(const fs::path& path)
{
    return QString::fromStdWString(path.wstring());
}

fs::path filePath(const QString& path)
{
    return fs::path(path.toStdWString());
}

QString normalizedPath(const fs::path& path)
{
    return pathString(Common::PathUtils::normalize(path));
}

QString lowerExtension(const fs::path& path)
{
    return QString::fromStdWString(path.extension().wstring()).toLower();
}

QString safePathPart(QString value, const QString& fallback)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral(R"([\x00-\x1f])")), QStringLiteral("_"));
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' ')))
        value.chop(1);
    return value.isEmpty() ? fallback : value;
}

QString firstSubcategory(const std::vector<std::string>& categories)
{
    for (const auto& category : categories) {
        const auto value = QString::fromUtf8(category.c_str()).trimmed();
        if (!value.isEmpty())
            return value;
    }
    return {};
}

bool containsInstrumentWord(const QString& value)
{
    const auto lower = value.toLower();
    return lower.contains(QStringLiteral("instrument"))
        || lower.contains(QStringLiteral("synth"))
        || lower.contains(QStringLiteral("sampler"));
}

void setCategory(PluginInfo& info, const QString& category, const QString& subcategory)
{
    info.subcategory = subcategory.trimmed();
    info.isInstrument = containsInstrumentWord(category) || containsInstrumentWord(info.subcategory);
    info.category = info.isInstrument ? QStringLiteral("instrument") : QStringLiteral("effect");
}

PluginInfo basePluginInfo(const fs::path& path, const QString& type, int bits)
{
    PluginInfo info;
    const auto normalized = normalizedPath(path);
    info.name = QFileInfo(normalized).completeBaseName();
    info.vendor = {};
    info.version = {};
    info.type = type;
    info.bits = bits;
    info.path = normalized;
    info.category = QStringLiteral("effect");
    return info;
}

bool readAt(const std::vector<unsigned char>& data, std::size_t offset, void* destination, std::size_t size)
{
    if (offset > data.size() || size > data.size() - offset)
        return false;
    std::memcpy(destination, data.data() + offset, size);
    return true;
}

std::size_t rvaToOffset(const std::vector<unsigned char>& data,
                        std::size_t sectionOffset,
                        std::uint16_t sectionCount,
                        std::uint32_t sizeOfHeaders,
                        std::uint32_t rva)
{
    if (rva < sizeOfHeaders)
        return rva < data.size() ? rva : 0;

    for (std::uint16_t index = 0; index < sectionCount; ++index) {
        const auto offset = sectionOffset + static_cast<std::size_t>(index) * 40;
        std::uint32_t virtualSize {};
        std::uint32_t virtualAddress {};
        std::uint32_t rawSize {};
        std::uint32_t rawAddress {};
        if (!readAt(data, offset + 8, &virtualSize, sizeof(virtualSize))
            || !readAt(data, offset + 12, &virtualAddress, sizeof(virtualAddress))
            || !readAt(data, offset + 16, &rawSize, sizeof(rawSize))
            || !readAt(data, offset + 20, &rawAddress, sizeof(rawAddress)))
            return 0;

        const auto sectionSize = std::max(virtualSize, rawSize);
        if (rva >= virtualAddress && rva - virtualAddress < sectionSize) {
            const auto result = static_cast<std::size_t>(rawAddress) + (rva - virtualAddress);
            return result < data.size() ? result : 0;
        }
    }
    return 0;
}

struct PeInfo final {
    int bits {};
    bool valid {};
    bool hasVst2Export {};
};

PeInfo inspectPe(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};

    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length <= 0)
        return {};
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(data.data()), length);
    if (!input || data.size() < 64)
        return {};

    std::uint16_t mz {};
    std::int32_t peOffset {};
    if (!readAt(data, 0, &mz, sizeof(mz)) || !readAt(data, 0x3c, &peOffset, sizeof(peOffset))
        || mz != 0x5a4d || peOffset < 0)
        return {};

    const auto ntOffset = static_cast<std::size_t>(peOffset);
    std::uint32_t signature {};
    std::uint16_t machine {};
    std::uint16_t sectionCount {};
    std::uint16_t optionalSize {};
    if (!readAt(data, ntOffset, &signature, sizeof(signature))
        || !readAt(data, ntOffset + 4, &machine, sizeof(machine))
        || !readAt(data, ntOffset + 6, &sectionCount, sizeof(sectionCount))
        || !readAt(data, ntOffset + 20, &optionalSize, sizeof(optionalSize))
        || signature != 0x00004550)
        return {};

    PeInfo result;
    result.valid = true;
    result.bits = machine == 0x014c ? 32 : machine == 0x8664 ? 64 : 0;
    if (result.bits == 0)
        return result;

    const auto optionalOffset = ntOffset + 24;
    std::uint16_t optionalMagic {};
    std::uint32_t sizeOfHeaders {};
    std::uint32_t exportRva {};
    std::uint32_t exportSize {};
    const auto dataDirectoryOffset = result.bits == 32 ? 96u : 112u;
    if (!readAt(data, optionalOffset, &optionalMagic, sizeof(optionalMagic))
        || !readAt(data, optionalOffset + 60, &sizeOfHeaders, sizeof(sizeOfHeaders))
        || !readAt(data, optionalOffset + dataDirectoryOffset, &exportRva, sizeof(exportRva))
        || !readAt(data, optionalOffset + dataDirectoryOffset + 4, &exportSize, sizeof(exportSize)))
        return result;

    const auto expectedMagic = result.bits == 32 ? 0x10bu : 0x20bu;
    if (optionalMagic != expectedMagic || exportRva == 0 || exportSize == 0)
        return result;

    const auto sectionOffset = optionalOffset + optionalSize;
    const auto exportOffset = rvaToOffset(data, sectionOffset, sectionCount, sizeOfHeaders, exportRva);
    if (exportOffset == 0 || exportOffset + 40 > data.size())
        return result;

    std::uint32_t nameCount {};
    std::uint32_t namesRva {};
    if (!readAt(data, exportOffset + 24, &nameCount, sizeof(nameCount))
        || !readAt(data, exportOffset + 32, &namesRva, sizeof(namesRva)))
        return result;

    const auto namesOffset = rvaToOffset(data, sectionOffset, sectionCount, sizeOfHeaders, namesRva);
    if (namesOffset == 0)
        return result;
    for (std::uint32_t index = 0; index < nameCount; ++index) {
        std::uint32_t nameRva {};
        if (!readAt(data, namesOffset + static_cast<std::size_t>(index) * sizeof(nameRva),
                    &nameRva,
                    sizeof(nameRva)))
            break;
        const auto nameOffset = rvaToOffset(data, sectionOffset, sectionCount, sizeOfHeaders, nameRva);
        if (nameOffset == 0 || nameOffset >= data.size())
            continue;
        const auto* name = reinterpret_cast<const char*>(data.data() + nameOffset);
        const auto remaining = data.size() - nameOffset;
        const auto* terminator = static_cast<const char*>(std::memchr(name, '\0', remaining));
        if (terminator == nullptr)
            continue;
        if (std::strcmp(name, "VSTPluginMain") == 0 || std::strcmp(name, "main") == 0) {
            result.hasVst2Export = true;
            break;
        }
    }
    return result;
}

std::pair<fs::path, int> findVst3Binary(const fs::path& path)
{
    if (fs::is_regular_file(path)) {
        const auto pe = inspectPe(path);
        return { path, pe.bits };
    }

    if (!fs::is_directory(path))
        return {};

    const auto contents = path / L"Contents";
    const auto moduleName = path.filename();
    for (const auto& architecture : { L"x86_64-win", L"x86-win" }) {
        const auto architecturePath = contents / architecture;
        const auto preferred = architecturePath / moduleName;
        if (fs::is_regular_file(preferred)) {
            const auto pe = inspectPe(preferred);
            if (pe.bits != 0)
                return { preferred, pe.bits };
        }

        std::error_code error;
        if (!fs::is_directory(architecturePath, error))
            continue;
        for (fs::recursive_directory_iterator iterator(architecturePath, error), end;
             iterator != end && !error;
             iterator.increment(error)) {
            if (!iterator->is_regular_file(error))
                continue;
            const auto pe = inspectPe(iterator->path());
            if (pe.bits != 0)
                return { iterator->path(), pe.bits };
        }
    }

    return {};
}

fs::path findModuleInfoPath(const fs::path& path)
{
    if (!fs::is_directory(path))
        return {};
    const auto bundle = path;
    const auto standardPath = bundle / L"Contents" / L"Resources" / L"moduleinfo.json";
    if (fs::is_regular_file(standardPath))
        return standardPath;
    return {};
}

QString classCategory(const QString& category, const QString& subcategory)
{
    return containsInstrumentWord(category) || containsInstrumentWord(subcategory)
        ? QStringLiteral("instrument")
        : QStringLiteral("effect");
}

PluginInfo makeVst3Info(const fs::path& path,
                       int bits,
                       const QString& name,
                       const QString& vendor,
                       const QString& version,
                       const QString& category,
                       const QString& subcategory)
{
    auto info = basePluginInfo(path, QStringLiteral("vst3"), bits);
    if (!name.trimmed().isEmpty())
        info.name = name.trimmed();
    info.vendor = vendor.trimmed();
    info.version = version.trimmed();
    setCategory(info, category, subcategory);
    return info;
}

// Accept only known project display keys. SDK/legacy errors remain opaque text.
QString localizedProbeError(const QJsonObject& object, const QString& field)
{
    const auto fallback = object.value(field).toString();
    const auto key = object.value(field + QStringLiteral("_key")).toString();
    const auto arguments = object.value(field + QStringLiteral("_args")).toArray();
    QString text;
    int expectedArguments = 0;
    if (key == QStringLiteral("probe.noComponent")) {
        text = scannerText("probe.noComponent", "VST3 factory returned no usable component class");
        expectedArguments = 0;
    }
    else if (key == QStringLiteral("probe.invalidClassId")) {
        text = scannerText("probe.invalidClassId", "Invalid VST3 class ID: %1");
        expectedArguments = 1;
    }
    else if (key == QStringLiteral("probe.missingClassId")) {
        text = scannerText("probe.missingClassId", "VST3 factory cannot find class ID: %1");
        expectedArguments = 1;
    }
    else if (key == QStringLiteral("probe.createComponent")) {
        text = scannerText("probe.createComponent", "Cannot create VST3 component class: %1");
        expectedArguments = 1;
    }
    else if (key == QStringLiteral("probe.initializeComponent")) {
        text = scannerText("probe.initializeComponent", "Cannot initialize VST3 component: %1");
        expectedArguments = 1;
    }
    else if (key == QStringLiteral("probe.loadLibrary")) {
        text = scannerText("probe.loadLibrary", "LoadLibraryExW failed");
        expectedArguments = 0;
    }
    else if (key == QStringLiteral("probe.missingEntry")) {
        text = scannerText("probe.missingEntry", "VST2 module has no VSTPluginMain/main entry point");
        expectedArguments = 0;
    }
    else if (key == QStringLiteral("probe.invalidEffect")) {
        text = scannerText("probe.invalidEffect", "VST2 module returned an invalid AEffect");
        expectedArguments = 0;
    }
    else if (key == QStringLiteral("probe.vst2Disabled")) {
        text = scannerText("probe.vst2Disabled", "VST2 probing is disabled");
        expectedArguments = 0;
    }
    else if (key == QStringLiteral("probe.failed")) {
        text = scannerText("probe.failed", "Plug-in probe failed");
        expectedArguments = 0;
    }
    else
        return fallback;
    if (fallback.isEmpty() || arguments.size() != expectedArguments)
        return fallback;
    for (const auto& argument : arguments) {
        if (!argument.isString())
            return fallback;
        text = text.arg(argument.toString());
    }
    return text;
}

bool executeIsolatedProbe(const fs::path& path,
                          int bits,
                          const QStringList& arguments,
                          QJsonObject& result,
                          QString& error,
                          bool& skippable)
{
    skippable = false;
    QString probeName;
    if (bits == 64)
        probeName = QStringLiteral("Vst Scanner Probe64.exe");
    else if (bits == 32)
        probeName = QStringLiteral("Vst Scanner Probe32.exe");
    else {
        error = scannerText("message.unsupportedBits", "Unsupported plug-in bit depth: %1").arg(bits);
        return false;
    }

    const auto probePath = Common::PathUtils::moduleDirectory() / probeName.toStdWString();
    if (!fs::is_regular_file(probePath)) {
        error = scannerText("message.probeMissing", "Isolated probe not found: ") + pathString(probePath);
        return false;
    }

    const auto resultPath = QDir::tempPath()
        + QStringLiteral("/WinJACKNexus-VstProbe-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".json");
    QProcess process;
    process.setProgram(pathString(probePath));
    auto processArguments = arguments;
    processArguments.insert(1, pathString(path));
    processArguments.append({ QStringLiteral("--output"), resultPath });
    process.setArguments(processArguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(3000)) {
        error = scannerText("message.probeStart", "Cannot start isolated probe: ") + process.errorString();
        QFile::remove(resultPath);
        return false;
    }

    constexpr int probeTimeoutMs = 15000;
    if (!process.waitForFinished(probeTimeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        error = scannerText("message.probeTimeout", "Plug-in probe timed out; isolated process terminated");
        QFile::remove(resultPath);
        return false;
    }

    QFile resultFile(resultPath);
    if (!resultFile.open(QIODevice::ReadOnly)) {
        error = process.exitStatus() == QProcess::CrashExit
            ? scannerText("message.probeCrash", "Plug-in probe crashed; scanner kept running")
            : scannerText("message.probeNoResult", "Isolated probe produced no result: ") + process.errorString();
        QFile::remove(resultPath);
        return false;
    }

    QJsonParseError parseError {};
    const auto document = QJsonDocument::fromJson(resultFile.readAll(), &parseError);
    resultFile.close();
    QFile::remove(resultPath);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = scannerText("message.probeJson", "Isolated probe result is not valid JSON");
        return false;
    }

    result = document.object();
    if (!result.value(QStringLiteral("ok")).toBool()) {
        skippable = result.value(QStringLiteral("skippable")).toBool();
        error = localizedProbeError(result, QStringLiteral("error"));
        if (error.isEmpty())
            error = scannerText("message.probeFailed", "Isolated probe reported failure");
        return false;
    }
    return true;
}

void applyProbeInfo(PluginInfo& info, const QJsonObject& object, bool preserveName)
{
    const auto name = object.value(QStringLiteral("name")).toString().trimmed();
    if (!preserveName && !name.isEmpty())
        info.name = name;
    const auto vendor = object.value(QStringLiteral("vendor")).toString().trimmed();
    const auto version = object.value(QStringLiteral("version")).toString().trimmed();
    if (!vendor.isEmpty())
        info.vendor = vendor;
    if (!version.isEmpty())
        info.version = version;
    setCategory(info,
                object.value(QStringLiteral("category")).toString(),
                object.value(QStringLiteral("subcategory")).toString());
}

QString childProgressPath(const fs::path& path, const QString& childName)
{
    const auto fileName = QFileInfo(pathString(path)).completeBaseName();
    return childName.isEmpty() ? pathString(path) : fileName + QStringLiteral(" > ") + childName;
}

using WriteInfoCallback = std::function<bool(PluginInfo& info)>;

const PluginInfo* findExistingEntry(const QVector<PluginInfo>& entries, const PluginInfo& candidate);
QVector<PluginInfo> mergeVst3ProbeInfo(const QVector<PluginInfo>& moduleInfo,
                                                                             QVector<PluginInfo> probeInfo);

QVector<PluginInfo> runIsolatedProbe(const fs::path& path,
                                     int bits,
                                     const QString& type,
                                     const ScanProgressCallback& progressCallback,
                                                                         const WriteInfoCallback& writeInfoCallback,
                                     const QVector<PluginInfo>& moduleInfo,
                                     const QVector<PluginInfo>& existingEntries,
                                     bool rescanVerifiedPlugins,
                                     int& completed,
                                     int& total,
                                     bool& hasProbeClasses,
                                     QString& error,
                                     bool& skippable)
{
    skippable = false;
    QJsonObject factoryResult;
    if (!executeIsolatedProbe(path,
                              bits,
                              { type == QStringLiteral("vst3") ? QStringLiteral("--vst3")
                                                                  : QStringLiteral("--vst2") },
                              factoryResult,
                              error,
                              skippable))
        return {};

    const auto descriptors = factoryResult.value(QStringLiteral("classes")).toArray();
    const auto isShell = factoryResult.value(QStringLiteral("is_shell")).toBool();
    int childCount = 0;
    for (const auto& value : descriptors) {
        if (!value.toObject().value(QStringLiteral("name")).toString().trimmed().isEmpty())
            ++childCount;
    }
    const auto hasChildProgress = isShell
        || (type.compare(QStringLiteral("vst3"), Qt::CaseInsensitive) == 0 && childCount > 1);
    if (hasChildProgress) {
        total += childCount;
        if (progressCallback)
            progressCallback(completed, total, pathString(path));
    }

    QVector<PluginInfo> result;
    QStringList childErrors;
    for (const auto& value : descriptors) {
        const auto descriptor = value.toObject();
        auto info = basePluginInfo(path, type, bits);
        info.name = descriptor.value(QStringLiteral("name")).toString().trimmed();
        info.vendor = descriptor.value(QStringLiteral("vendor")).toString().trimmed();
        info.version = descriptor.value(QStringLiteral("version")).toString().trimmed();
        info.internalPlugin = descriptor.value(QStringLiteral("internal_plugin")).toString().trimmed();
        setCategory(info,
                    descriptor.value(QStringLiteral("category")).toString(),
                    descriptor.value(QStringLiteral("subcategory")).toString());
        if (info.name.isEmpty())
            continue;
        hasProbeClasses = true;

        if (!rescanVerifiedPlugins) {
            const auto* existing = findExistingEntry(existingEntries, info);
            const auto refreshVst2Classification = type.compare(QStringLiteral("vst2"), Qt::CaseInsensitive) == 0
                && !isShell
                && existing != nullptr
                && existing->verified
                && existing->isInstrument != info.isInstrument;
            const auto refreshVst2Midi = type.compare(QStringLiteral("vst2"), Qt::CaseInsensitive) == 0
                && existing != nullptr
                && existing->verified
                && existing->vst2ProbeVersion < currentVst2ProbeVersion;
            if (existing != nullptr && existing->verified
                && !refreshVst2Classification && !refreshVst2Midi) {
                if (hasChildProgress)
                    ++completed;
                continue;
            }
        }

        if (progressCallback)
            progressCallback(completed, total, childProgressPath(path, info.internalPlugin.isEmpty() ? info.name : info.internalPlugin));

        QJsonObject childResult;
        const auto selectedId = descriptor.value(QStringLiteral("class_id")).toString();
        const auto uniqueId = descriptor.value(QStringLiteral("unique_id")).toVariant().toLongLong();
        QStringList childArguments {
            type == QStringLiteral("vst3") ? QStringLiteral("--vst3") : QStringLiteral("--vst2")
        };
        if (type == QStringLiteral("vst3")) {
            childArguments.append({ QStringLiteral("--class-id"), selectedId });
        } else if (uniqueId != 0) {
            childArguments.append({ QStringLiteral("--internal-id"), QString::number(uniqueId) });
        }

        QString childError;
        bool childSkippable = false;
        bool verified = false;
        if (executeIsolatedProbe(path,
                     bits,
                     childArguments,
                     childResult,
                     childError,
                     childSkippable)) {
            const auto classes = childResult.value(QStringLiteral("classes")).toArray();
            if (!classes.isEmpty()) {
                applyProbeInfo(info, classes.at(0).toObject(), true);
                verified = childResult.value(QStringLiteral("probe_error")).toString().isEmpty();
            }
            const auto probeError = localizedProbeError(childResult, QStringLiteral("probe_error"));
            if (!probeError.isEmpty())
                childErrors.push_back(info.name + QStringLiteral(": ") + probeError);
        } else if (childSkippable) {
            if (hasChildProgress)
                ++completed;
            continue;
        } else if (!childError.isEmpty()) {
            childErrors.push_back(info.name + QStringLiteral(": ") + childError);
        }
        info.verified = verified;
        if (type.compare(QStringLiteral("vst3"), Qt::CaseInsensitive) == 0 && !moduleInfo.isEmpty()) {
            auto merged = mergeVst3ProbeInfo(moduleInfo, QVector<PluginInfo> { info });
            if (!merged.isEmpty())
                info = std::move(merged.front());
        }
        if (writeInfoCallback)
            writeInfoCallback(info);
        result.push_back(std::move(info));
        if (hasChildProgress)
            ++completed;
    }

    if (result.isEmpty() && !hasProbeClasses) {
        error = scannerText("message.probeNoClass", "Isolated probe returned no usable plug-in classes");
        return {};
    }
    error = childErrors.join(QStringLiteral("; "));
    return result;
}

QVector<PluginInfo> mergeVst3ProbeInfo(const QVector<PluginInfo>& moduleInfo,
                                       QVector<PluginInfo> probeInfo)
{
    for (auto& probed : probeInfo) {
        const auto match = std::find_if(moduleInfo.cbegin(),
                                        moduleInfo.cend(),
                                        [&] (const PluginInfo& described) {
                                            return described.name.compare(probed.name, Qt::CaseInsensitive) == 0;
                                        });
        if (match == moduleInfo.cend())
            continue;
        if (probed.vendor.isEmpty())
            probed.vendor = match->vendor;
        if (probed.version.isEmpty())
            probed.version = match->version;
        if (probed.subcategory.isEmpty())
            probed.subcategory = match->subcategory;
        if (probed.category.isEmpty())
            probed.category = match->category;
        probed.isInstrument = match->isInstrument || probed.isInstrument;
    }
    return probeInfo;
}

QVector<PluginInfo> readVst3ModuleInfo(const fs::path& path, int bits, QString& error)
{
    const auto moduleInfoPath = findModuleInfoPath(path);
    if (moduleInfoPath.empty())
        return {};

    QFile file(pathString(moduleInfoPath));
    if (!file.open(QIODevice::ReadOnly)) {
        error = scannerText("message.moduleRead", "Cannot read moduleinfo.json: ") + pathString(moduleInfoPath);
        return {};
    }

    const auto bytes = file.readAll();
    std::ostringstream parserErrors;
    const auto parsed = Steinberg::ModuleInfoLib::parseJson(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), &parserErrors);
    if (!parsed) {
        error = scannerText("message.moduleParse", "Cannot parse moduleinfo.json: ")
            + QString::fromStdString(parserErrors.str()).trimmed();
        return {};
    }

    QVector<PluginInfo> result;
    const auto moduleName = QString::fromUtf8(parsed->name.c_str());
    const auto moduleVersion = QString::fromUtf8(parsed->version.c_str());
    const auto factoryVendor = QString::fromUtf8(parsed->factoryInfo.vendor.c_str());
    for (const auto& classInfo : parsed->classes) {
        const auto category = QString::fromUtf8(classInfo.category.c_str());
        if (category.toLower().contains(QStringLiteral("controller"))
            || category.toLower().contains(QStringLiteral("service"))
            || category.toLower().contains(QStringLiteral("compatibility")))
            continue;
        const auto subcategory = firstSubcategory(classInfo.subCategories);
        const auto name = QString::fromUtf8(classInfo.name.c_str());
        const auto vendor = QString::fromUtf8(classInfo.vendor.c_str());
        const auto version = QString::fromUtf8(classInfo.version.c_str());
        result.push_back(makeVst3Info(path,
                                      bits,
                                      name.isEmpty() ? moduleName : name,
                                      vendor.isEmpty() ? factoryVendor : vendor,
                                      version.isEmpty() ? moduleVersion : version,
                                      category,
                                      subcategory));
    }

    if (result.isEmpty())
        result.push_back(makeVst3Info(path,
                                      bits,
                                      moduleName,
                                      factoryVendor,
                                      moduleVersion,
                                      {},
                                      {}));
    return result;
}

bool samePath(const QString& first, const QString& second)
{
    return QString::compare(QDir::cleanPath(first), QDir::cleanPath(second), Qt::CaseInsensitive) == 0;
}

QVector<PluginInfo> loadExistingEntries(const fs::path& outputDirectory, const QString& sourcePath)
{
    QVector<PluginInfo> result;
    std::error_code error;
    if (!fs::is_directory(outputDirectory, error))
        return result;
    for (fs::recursive_directory_iterator iterator(outputDirectory, error), end;
         iterator != end && !error;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error))
            continue;
        const auto extension = lowerExtension(iterator->path());
        if (extension != QStringLiteral(".vstinfo") && extension != QStringLiteral(".vst3info"))
            continue;
        const auto info = Scanner::loadInfoFile(pathString(iterator->path()));
        if (info && samePath(info->path, sourcePath))
            result.push_back(*info);
    }
    return result;
}

const PluginInfo* findExistingEntry(const QVector<PluginInfo>& entries, const PluginInfo& candidate)
{
    const auto match = std::find_if(entries.cbegin(),
                                    entries.cend(),
                                    [&] (const PluginInfo& existing) {
                                        if (!samePath(existing.path, candidate.path)
                                            || existing.type.compare(candidate.type, Qt::CaseInsensitive) != 0
                                            || existing.bits != candidate.bits)
                                            return false;
                                        if (!candidate.internalPlugin.isEmpty()
                                            || !existing.internalPlugin.isEmpty())
                                            return existing.internalPlugin.compare(candidate.internalPlugin,
                                                                                  Qt::CaseInsensitive) == 0;
                                        return existing.name.compare(candidate.name, Qt::CaseInsensitive) == 0;
                                    });
    return match == entries.cend() ? nullptr : &*match;
}

void removeExistingEntries(const fs::path& outputDirectory, const QString& sourcePath)
{
    std::error_code error;
    if (!fs::is_directory(outputDirectory, error))
        return;
    for (fs::recursive_directory_iterator iterator(outputDirectory, error), end;
         iterator != end && !error;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error))
            continue;
        const auto extension = lowerExtension(iterator->path());
        if (extension != QStringLiteral(".vstinfo") && extension != QStringLiteral(".vst3info"))
            continue;
        const auto info = Scanner::loadInfoFile(pathString(iterator->path()));
        if (info && samePath(info->path, sourcePath))
            QFile::remove(pathString(iterator->path()));
    }
}

void removeStaleVst2InfoFiles(const fs::path& outputDirectory, const PluginInfo& info)
{
    if (info.type.compare(QStringLiteral("vst2"), Qt::CaseInsensitive) != 0)
        return;

    std::error_code error;
    for (fs::recursive_directory_iterator iterator(outputDirectory, error), end;
         iterator != end && !error;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error)
            || lowerExtension(iterator->path()) != QStringLiteral(".vstinfo"))
            continue;
        const auto existing = Scanner::loadInfoFile(pathString(iterator->path()));
        if (!existing
            || !samePath(existing->path, info.path)
            || existing->type.compare(info.type, Qt::CaseInsensitive) != 0
            || existing->bits != info.bits
            || existing->internalPlugin.compare(info.internalPlugin, Qt::CaseInsensitive) != 0
            || samePath(existing->infoFile, info.infoFile))
            continue;
        QFile::remove(existing->infoFile);
    }
}

bool writeInfoFile(const fs::path& outputDirectory, PluginInfo& info, QString& error)
{
    const auto formatDirectory = info.type.compare(QStringLiteral("vst2"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("VST2")
        : QStringLiteral("VST3");
    const auto categoryDirectory = info.isInstrument ? QStringLiteral("Instruments") : QStringLiteral("Effects");
    auto directory = outputDirectory / formatDirectory.toStdWString() / categoryDirectory.toStdWString();
    const auto subcategory = safePathPart(info.subcategory, {});
    if (!subcategory.isEmpty())
        directory /= subcategory.toStdWString();
    std::error_code directoryError;
    fs::create_directories(directory, directoryError);
    if (directoryError) {
        error = scannerText("message.infoDirectory", "Cannot create info directory: ") + pathString(directory);
        return false;
    }

    const auto extension = info.type.compare(QStringLiteral("vst2"), Qt::CaseInsensitive) == 0
        ? QStringLiteral(".vstinfo")
        : QStringLiteral(".vst3info");
    const auto stem = safePathPart(info.name, QStringLiteral("plugin"));
    auto target = directory / (stem + extension).toStdWString();
    int suffix = 2;
    while (fs::exists(target)) {
        const auto existing = Scanner::loadInfoFile(pathString(target));
        if (existing && samePath(existing->path, info.path)
            && existing->internalPlugin == info.internalPlugin)
            break;
        target = directory / (stem + QStringLiteral("_") + QString::number(suffix++) + extension).toStdWString();
    }

    QJsonObject object {
        { QStringLiteral("name"), info.name },
        { QStringLiteral("vendor"), info.vendor },
        { QStringLiteral("version"), info.version },
        { QStringLiteral("type"), info.type },
        { QStringLiteral("bits"), info.bits },
        { QStringLiteral("path"), info.path },
        { QStringLiteral("category"), info.category },
        { QStringLiteral("subcategory"), info.subcategory },
        { QStringLiteral("isInstrument"), info.isInstrument },
        { QStringLiteral("verified"), info.verified }
    };
    if (info.type.compare(QStringLiteral("vst2"), Qt::CaseInsensitive) == 0) {
        info.vst2ProbeVersion = currentVst2ProbeVersion;
        object.insert(QStringLiteral("vst2ProbeVersion"), info.vst2ProbeVersion);
    }
    if (!info.internalPlugin.isEmpty())
        object.insert(QStringLiteral("internal_plugin"), info.internalPlugin);

    QSaveFile file(pathString(target));
    if (!file.open(QIODevice::WriteOnly)) {
        error = scannerText("message.infoWrite", "Cannot write info file: ") + pathString(target);
        return false;
    }
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        error = scannerText("message.infoCommit", "Cannot commit info file: ") + pathString(target);
        return false;
    }
    info.infoFile = pathString(target);
    removeStaleVst2InfoFiles(outputDirectory, info);
    return true;
}

void appendMessage(ScanReport& report, const QString& path, const QString& text, bool error)
{
    report.messages.push_back(ScanMessage { path, text, error });
    if (error)
        WJN_LOG_WARN("VST scanner: %s (%s)", qPrintable(text), qPrintable(path));
    else
        WJN_LOG_INFO("VST scanner: %s (%s)", qPrintable(text), qPrintable(path));
}

}

std::optional<PluginInfo> Scanner::loadInfoFile(const QString& infoFile)
{
    QFile file(infoFile);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError parseError {};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {};

    const auto object = document.object();
    PluginInfo info;
    info.name = object.value(QStringLiteral("name")).toString();
    info.vendor = object.value(QStringLiteral("vendor")).toString();
    info.version = object.value(QStringLiteral("version")).toString();
    info.type = object.value(QStringLiteral("type")).toString();
    info.bits = object.value(QStringLiteral("bits")).toInt();
    info.path = object.value(QStringLiteral("path")).toString();
    info.category = object.value(QStringLiteral("category")).toString();
    info.subcategory = object.value(QStringLiteral("subcategory")).toString();
    info.isInstrument = object.value(QStringLiteral("isInstrument")).toBool(
        info.category.compare(QStringLiteral("instrument"), Qt::CaseInsensitive) == 0);
    info.verified = !object.contains(QStringLiteral("verified"))
        || object.value(QStringLiteral("verified")).toBool();
    info.vst2ProbeVersion = object.value(QStringLiteral("vst2ProbeVersion")).toInt();
    info.internalPlugin = object.value(QStringLiteral("internal_plugin")).toString();
    info.infoFile = infoFile;
    if (info.name.isEmpty() || info.path.isEmpty() || info.type.isEmpty() || (info.bits != 32 && info.bits != 64))
        return {};

    return info;
}

ScanReport Scanner::scan(const ScanOptions& options) const
{
    const ScanLanguageScope language(options.textCallback);
    ScanReport report;
    const auto outputDirectory = options.outputDirectory.trimmed().isEmpty()
        ? Common::PathUtils::moduleDirectory() / L"nodes" / L"VSTPlugins"
        : filePath(options.outputDirectory);
    const auto normalizedOutputDirectory = Common::PathUtils::normalize(outputDirectory);
    std::error_code outputError;
    fs::create_directories(normalizedOutputDirectory, outputError);
    if (outputError) {
        appendMessage(report,
                      pathString(normalizedOutputDirectory),
                      scannerText("message.outputCreate", "Cannot create output directory"),
                      true);
        return report;
    }

    std::vector<fs::path> candidates;
    QSet<QString> candidateKeys;
    const auto addCandidate = [&] (const fs::path& path) {
        const auto key = normalizedPath(path).toLower();
        if (candidateKeys.contains(key))
            return;
        candidateKeys.insert(key);
        candidates.push_back(path);
    };

    std::function<void(const fs::path&)> collect;
    collect = [&] (const fs::path& path) {
        std::error_code error;
        if (fs::is_directory(path, error)) {
            if (lowerExtension(path) == QStringLiteral(".vst3")) {
                addCandidate(path);
                return;
            }
            for (fs::directory_iterator iterator(path, error), end;
                 iterator != end && !error;
                 iterator.increment(error))
                collect(iterator->path());
            return;
        }
        if (!fs::is_regular_file(path, error))
            return;
        const auto extension = lowerExtension(path);
        if (extension == QStringLiteral(".dll") || extension == QStringLiteral(".vst3"))
            addCandidate(path);
    };

    for (const auto& searchPath : options.searchPaths) {
        const auto path = Common::PathUtils::normalize(filePath(searchPath));
        std::error_code error;
        if (!fs::exists(path, error)) {
            appendMessage(report, pathString(path), scannerText("message.pathMissing", "Scan path does not exist"), true);
            continue;
        }
        collect(path);
    }

    int total = static_cast<int>(candidates.size());
    if (options.progressCallback)
        options.progressCallback(0, total, {});

    QSet<QString> seen;
    int completed = 0;
    const auto writeInfoCallback = [&] (PluginInfo& info) -> bool {
        QString writeError;
        if (writeInfoFile(normalizedOutputDirectory, info, writeError)) {
            if (options.infoWrittenCallback)
                options.infoWrittenCallback(info);
            return true;
        }
        appendMessage(report, info.path, writeError, true);
        return false;
    };
    const auto savePlugin = [&] (PluginInfo info) {
        if (info.infoFile.isEmpty() && !writeInfoCallback(info))
            return;
        report.plugins.push_back(std::move(info));
    };

    const auto scanVst3 = [&] (const fs::path& path) {
        const auto key = normalizedPath(path).toLower();
        if (seen.contains(key))
            return;
        seen.insert(key);
        const auto [binary, bits] = findVst3Binary(path);
        if (binary.empty() || (bits != 32 && bits != 64)) {
            appendMessage(report, pathString(path), scannerText("message.vst3Binary", "No VST3 binary with a recognized bit depth found"), true);
            return;
        }
        const auto existingEntries = loadExistingEntries(normalizedOutputDirectory, normalizedPath(path));
        if (options.rescanVerifiedPlugins)
            removeExistingEntries(normalizedOutputDirectory, normalizedPath(path));
        QString moduleInfoError;
        auto infos = readVst3ModuleInfo(path, bits, moduleInfoError);
        if (!options.rescanVerifiedPlugins && !infos.isEmpty()
            && existingEntries.size() == infos.size()
            && std::all_of(infos.cbegin(), infos.cend(), [&] (const PluginInfo& info) {
                   const auto* existing = findExistingEntry(existingEntries, info);
                   return existing != nullptr && existing->verified;
               })) {
            appendMessage(report,
                          pathString(path),
                          scannerText("message.vst3Skipped", "Skipped verified VST3; using existing info entries"),
                          false);
            return;
        }
        QString probeError;
        bool hasProbeClasses = false;
        bool probeSkippable = false;
        const auto probedInfos = runIsolatedProbe(path,
                                                  bits,
                                                  QStringLiteral("vst3"),
                                                  options.progressCallback,
                                                  writeInfoCallback,
                                                  infos,
                                                  existingEntries,
                                                  options.rescanVerifiedPlugins,
                                                  completed,
                                                  total,
                                                  hasProbeClasses,
                                                  probeError,
                                                  probeSkippable);
        if (!probedInfos.isEmpty()) {
            infos = probedInfos;
            appendMessage(report,
                          pathString(path),
                          probeError.isEmpty()
                              ? scannerText("message.vst3Loaded", "Loaded VST3 in isolation and obtained class information")
                              : scannerText("message.vst3Partial", "VST3 classes probed in isolation; some failed to load, entries kept: ") + probeError,
                          false);
            for (auto& info : infos) {
                if (!info.infoFile.isEmpty())
                    report.plugins.push_back(std::move(info));
            }
        } else if (hasProbeClasses) {
            appendMessage(report,
                          pathString(path),
                          scannerText("message.vst3ClassSkipped", "VST3 classes already verified; skipping probe"),
                          false);
            return;
        } else if (infos.isEmpty()) {
            auto fallback = basePluginInfo(path, QStringLiteral("vst3"), bits);
            savePlugin(std::move(fallback));
            appendMessage(report,
                          pathString(path),
                          moduleInfoError.isEmpty() ? probeError : moduleInfoError,
                          true);
            return;
        } else {
            appendMessage(report,
                          pathString(path),
                          scannerText("message.vst3Static", "Isolated load returned no classes; keeping static moduleinfo data: ") + probeError,
                          false);
        }
        if (probedInfos.isEmpty()) {
            for (auto& info : infos)
                savePlugin(std::move(info));
        }
    };

    const auto scanVst2 = [&] (const fs::path& path) {
        const auto key = normalizedPath(path).toLower();
        if (seen.contains(key))
            return;
        seen.insert(key);
        const auto pe = inspectPe(path);
        if (!pe.valid || (pe.bits != 32 && pe.bits != 64) || !pe.hasVst2Export)
            return;
        const auto existingEntries = loadExistingEntries(normalizedOutputDirectory, normalizedPath(path));
#if !WJN_ENABLE_VST2
        if (!options.rescanVerifiedPlugins) {
            auto candidate = basePluginInfo(path, QStringLiteral("vst2"), pe.bits);
            if (const auto* existing = findExistingEntry(existingEntries, candidate); existing != nullptr
                && existing->verified) {
                appendMessage(report,
                              pathString(path),
                              scannerText("message.vst2Skipped", "Skipped verified VST2; using existing info entries"),
                              false);
                return;
            }
        }
#endif
        if (options.rescanVerifiedPlugins)
            removeExistingEntries(normalizedOutputDirectory, normalizedPath(path));
        QString probeError;
    #if WJN_ENABLE_VST2
        bool hasProbeClasses = false;
        bool probeSkippable = false;
        auto infos = runIsolatedProbe(path,
                          pe.bits,
                          QStringLiteral("vst2"),
                          options.progressCallback,
                          writeInfoCallback,
                          QVector<PluginInfo> {},
                          existingEntries,
                          options.rescanVerifiedPlugins,
                          completed,
                          total,
                          hasProbeClasses,
                          probeError,
                          probeSkippable);
        if (infos.isEmpty()) {
            if (probeSkippable) {
                appendMessage(report,
                              pathString(path),
                              scannerText("message.invalidDll", "Skipped invalid VST DLL")
                                  + (probeError.isEmpty() ? QString() : QStringLiteral(": ") + probeError),
                              false);
                return;
            }
            if (!hasProbeClasses)
                savePlugin(basePluginInfo(path, QStringLiteral("vst2"), pe.bits));
        } else {
            for (auto& info : infos) {
                if (!info.infoFile.isEmpty())
                    report.plugins.push_back(std::move(info));
            }
        }
#else
        auto fallback = basePluginInfo(path, QStringLiteral("vst2"), pe.bits);
        fallback.verified = true;
        savePlugin(std::move(fallback));
#endif
        if (!probeError.isEmpty())
            appendMessage(report, pathString(path), probeError, true);
    };

    for (const auto& path : candidates) {
        if (options.progressCallback)
            options.progressCallback(completed, total, pathString(path));

        std::error_code error;
        const auto extension = lowerExtension(path);
        if (fs::is_directory(path, error) && extension == QStringLiteral(".vst3"))
            scanVst3(path);
        else if (fs::is_regular_file(path, error) && extension == QStringLiteral(".dll"))
            scanVst2(path);
        else if (fs::is_regular_file(path, error) && extension == QStringLiteral(".vst3"))
            scanVst3(path);
        ++completed;
    }

    if (options.progressCallback)
        options.progressCallback(completed, total, {});
    return report;
}

}