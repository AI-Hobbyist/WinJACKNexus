#include <WinJACKNexus/VstScanner/Scanner.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <filesystem>

#ifndef WJN_ENABLE_VST2
#    define WJN_ENABLE_VST2 0
#endif

namespace {

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const WinJACKNexus::VstScanner::Scanner scanner;
    QTemporaryDir temporaryDirectory;
    if (!check(temporaryDirectory.isValid(), "failed to create temporary directory"))
        return 1;

    const auto bundle = temporaryDirectory.path() + QStringLiteral("/Fixture.vst3");
    const auto binaryDirectory = bundle + QStringLiteral("/Contents/x86_64-win");
    const auto resourceDirectory = bundle + QStringLiteral("/Contents/Resources");
    if (!check(QDir().mkpath(binaryDirectory), "failed to create VST3 binary directory")
        || !check(QDir().mkpath(resourceDirectory), "failed to create VST3 resource directory"))
        return 1;

    const auto sourceBinary = QCoreApplication::applicationDirPath() + QStringLiteral("/common.dll");
    if (!check(QFile::copy(sourceBinary, binaryDirectory + QStringLiteral("/Fixture.vst3")),
               "failed to copy PE fixture"))
        return 1;

    const QByteArray moduleInfo = R"json({
  "Name": "Fixture Bundle",
  "Version": "1.0.0",
  "Factory Info": {
    "Vendor": "Fixture Vendor",
    "URL": "https://example.invalid",
    "E-Mail": "fixture@example.invalid",
    "Flags": {
      "Classes Discardable": false,
      "Component Non Discardable": false,
      "Unicode": true
    }
  },
  "Classes": [
    {
      "CID": "00112233445566778899aabbccddeeff",
      "Category": "Fx",
      "Name": "Fixture Delay",
      "Vendor": "Fixture Vendor",
      "Version": "1.0.0",
      "SDKVersion": "VST 3.7.0",
      "Sub Categories": ["Delay"],
      "Class Flags": 0,
      "Cardinality": 2147483647
    },
    {
      "CID": "ffeeddccbbaa99887766554433221100",
      "Category": "Instrument",
      "Name": "Fixture Synth",
      "Vendor": "Fixture Vendor",
      "Version": "1.0.0",
      "SDKVersion": "VST 3.7.0",
      "Sub Categories": ["Synth"],
      "Class Flags": 0,
      "Cardinality": 2147483647
    }
  ]
})json";
    if (!check(writeFile(resourceDirectory + QStringLiteral("/moduleinfo.json"), moduleInfo),
               "failed to write moduleinfo fixture"))
        return 1;

    const auto outputDirectory = temporaryDirectory.path() + QStringLiteral("/output");
    const auto expectedBundlePath = QString::fromStdWString(
        WinJACKNexus::Common::PathUtils::normalize(std::filesystem::path(bundle.toStdWString())).wstring());
    int progressTotal = -1;
    int finalProgress = -1;
    int progressUpdates = 0;
    int infoWriteCount = 0;
    bool infoFilesWereVisible = true;
    QString currentPluginPath;
    WinJACKNexus::VstScanner::ScanOptions options;
    options.searchPaths = { bundle };
    options.outputDirectory = outputDirectory;
    options.progressCallback = [&] (int completed, int total, const QString& currentPath) {
        progressTotal = total;
        finalProgress = completed;
        if (!currentPath.isEmpty())
            currentPluginPath = currentPath;
        ++progressUpdates;
    };
    options.infoWrittenCallback = [&] (const WinJACKNexus::VstScanner::PluginInfo& info) {
        ++infoWriteCount;
        infoFilesWereVisible = infoFilesWereVisible && QFileInfo::exists(info.infoFile);
    };
    const auto report = scanner.scan(options);
    if (!check(progressTotal == 1 && finalProgress == 1 && progressUpdates >= 3,
               "scanner progress did not report the complete candidate set")
        || !check(currentPluginPath == expectedBundlePath, "scanner progress did not report the current plugin"))
        return 1;
    if (!check(report.plugins.size() == 2, "expected two VST3 class records"))
        return 1;
    if (!check(infoWriteCount == 2 && infoFilesWereVisible,
               "VST3 info files were not visible after each write callback"))
        return 1;
    bool hasUnexpectedError = false;
    for (const auto& message : report.messages)
        hasUnexpectedError = hasUnexpectedError || message.error;
    if (!check(!hasUnexpectedError, "valid fixture produced an unexpected scan error"))
        return 1;

    int infoFileCount = 0;
    bool sawEffect = false;
    bool sawInstrument = false;
    QDirIterator outputIterator(outputDirectory,
                                { QStringLiteral("*.vst3info") },
                                QDir::Files,
                                QDirIterator::Subdirectories);
    while (outputIterator.hasNext()) {
        const auto infoFile = outputIterator.next();
        ++infoFileCount;
        const auto info = WinJACKNexus::VstScanner::Scanner::loadInfoFile(infoFile);
        if (!check(info.has_value(), "failed to load generated info file"))
            return 1;
        QFile rawInfoFile(infoFile);
        QJsonParseError infoParseError {};
        if (!check(rawInfoFile.open(QIODevice::ReadOnly), "failed to open generated info JSON"))
            return 1;
        const auto rawInfo = QJsonDocument::fromJson(rawInfoFile.readAll(), &infoParseError).object();
        if (!check(infoParseError.error == QJsonParseError::NoError
                       && !rawInfo.contains(QStringLiteral("audioIns"))
                       && !rawInfo.contains(QStringLiteral("audioOuts"))
                       && !rawInfo.contains(QStringLiteral("midiIn"))
                       && !rawInfo.contains(QStringLiteral("midiOut"))
                       && !rawInfo.contains(QStringLiteral("buses")),
                   "generated info contains removed port metadata"))
            return 1;
        if (info->name == QStringLiteral("Fixture Delay"))
            sawEffect = info->category == QStringLiteral("effect") && info->subcategory == QStringLiteral("Delay");
        if (info->name == QStringLiteral("Fixture Synth"))
            sawInstrument = info->isInstrument && info->category == QStringLiteral("instrument");
        if (!check(info->path == expectedBundlePath, "generated info path does not match bundle"))
            return 1;
    }

    if (!check(infoFileCount == 2, "expected two generated info files")
        || !check(sawEffect, "effect class was not categorized correctly")
        || !check(sawInstrument, "instrument class was not categorized correctly"))
        return 1;

    const auto missingPath = temporaryDirectory.path() + QStringLiteral("/missing");
    const auto missingReport = scanner.scan({ QStringList { missingPath }, outputDirectory });
    if (!check(missingReport.plugins.isEmpty(), "missing path produced a plugin record")
        || !check(missingReport.messages.size() == 1 && missingReport.messages.front().error,
                  "missing path did not produce an error message"))
        return 1;

    const auto singleFilePlugin = temporaryDirectory.path() + QStringLiteral("/Kontakt 8.vst3");
    if (!check(QFile::copy(sourceBinary, singleFilePlugin), "failed to create single-file VST3 fixture"))
        return 1;
    const auto singleFileOutput = temporaryDirectory.path() + QStringLiteral("/single-file-output");
    const auto singleFileReport = scanner.scan({ QStringList { singleFilePlugin }, singleFileOutput });
    if (!check(singleFileReport.plugins.size() == 1,
               "single-file VST3 fixture was not reported without loading the module")
        || !check(singleFileReport.plugins.front().name == QStringLiteral("Kontakt 8"),
                  "single-file VST3 fallback name is incorrect"))
        return 1;

    const auto kontaktPath = qEnvironmentVariable("WJN_TEST_KONTAKT_PATH");
    if (!kontaktPath.isEmpty() && QFileInfo::exists(kontaktPath)) {
        QStringList progressPaths;
        WinJACKNexus::VstScanner::ScanOptions kontaktOptions;
        kontaktOptions.searchPaths = { kontaktPath };
        kontaktOptions.outputDirectory = temporaryDirectory.path() + QStringLiteral("/kontakt-output");
        kontaktOptions.progressCallback = [&] (int, int, const QString& currentPath) {
            if (!currentPath.isEmpty())
                progressPaths.push_back(currentPath);
        };
        const auto kontaktReport = scanner.scan(kontaktOptions);
        const auto kontaktPlugin = std::find_if(kontaktReport.plugins.cbegin(),
                                                kontaktReport.plugins.cend(),
                                                [] (const WinJACKNexus::VstScanner::PluginInfo& info) {
                                                    return info.name.contains(QStringLiteral("Kontakt"),
                                                                              Qt::CaseInsensitive);
                                                });
        bool hasError = false;
        for (const auto& message : kontaktReport.messages)
            hasError = hasError || message.error;
        if (!check(kontaktPlugin != kontaktReport.plugins.cend(),
                   "Kontakt was not enumerated by the isolated VST3 factory probe")
            || !check(!hasError, "Kontakt scan produced an error message")
            || !check(std::any_of(progressPaths.cbegin(),
                                   progressPaths.cend(),
                                   [] (const QString& path) {
                                       return path.contains(QStringLiteral(" > "));
                                   }),
                      "Shell/class scan progress did not include a child plugin name"))
            return 1;
    }

    const auto wavesPath = qEnvironmentVariable("WJN_TEST_WAVES_PATH");
    if (!wavesPath.isEmpty() && QFileInfo::exists(wavesPath)) {
        int wavesTotal = -1;
        int wavesCompleted = -1;
        bool sawChildProgress = false;
        WinJACKNexus::VstScanner::ScanOptions wavesOptions;
        wavesOptions.searchPaths = { wavesPath };
        wavesOptions.outputDirectory = temporaryDirectory.path() + QStringLiteral("/waves-output");
        wavesOptions.progressCallback = [&] (int completed, int total, const QString& currentPath) {
            wavesCompleted = completed;
            wavesTotal = total;
            sawChildProgress = sawChildProgress || currentPath.contains(QStringLiteral(" > "));
        };
        const auto wavesReport = scanner.scan(wavesOptions);
        if (!check(wavesReport.plugins.size() > 1,
                   "Waves Shell did not return multiple VST3 class records")
            || !check(wavesTotal > 1 && wavesCompleted == wavesTotal,
                      "Waves Shell progress did not count every child plugin")
            || !check(sawChildProgress,
                      "Waves Shell progress did not include child plugin names"))
            return 1;
    }

#if WJN_ENABLE_VST2
    const auto shellFixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/Vst2 Shell Fixture.dll");
    if (QFileInfo::exists(shellFixture)) {
        const auto shellOutput = temporaryDirectory.path() + QStringLiteral("/shell-output");
        int firstWriteCount = 0;
        int firstMaximumTotal = 0;
        bool firstWritesWereVisible = true;
        QStringList firstProgressPaths;
        WinJACKNexus::VstScanner::ScanOptions firstShellOptions;
        firstShellOptions.searchPaths = { shellFixture };
        firstShellOptions.outputDirectory = shellOutput;
        firstShellOptions.progressCallback = [&] (int, int total, const QString& currentPath) {
            firstMaximumTotal = std::max(firstMaximumTotal, total);
            if (!currentPath.isEmpty())
                firstProgressPaths.push_back(currentPath);
        };
        firstShellOptions.infoWrittenCallback = [&] (const WinJACKNexus::VstScanner::PluginInfo& info) {
            ++firstWriteCount;
            firstWritesWereVisible = firstWritesWereVisible && QFileInfo::exists(info.infoFile);
        };
        const auto firstShellReport = scanner.scan(firstShellOptions);
        bool firstShellHasError = false;
        for (const auto& message : firstShellReport.messages)
            firstShellHasError = firstShellHasError || message.error;
        if (!check(firstShellReport.plugins.size() == 2,
                   "VST2 Shell fixture did not return both child plugins")
            || !check(firstWriteCount == 2 && firstWritesWereVisible,
                      "VST2 Shell info files were not written after each child probe")
            || !check(firstMaximumTotal == 3,
                      "VST2 Shell child count was not added to scan progress")
            || !check(std::any_of(firstProgressPaths.cbegin(),
                                   firstProgressPaths.cend(),
                                   [] (const QString& path) {
                                       return path.contains(QStringLiteral("Vst2 Shell Fixture > Fixture Child"));
                                   }),
                      "VST2 Shell progress did not include child plugin names")
            || !check(!firstShellHasError,
                      "VST2 Shell fixture produced an unexpected scan error"))
            return 1;
        for (const auto& plugin : firstShellReport.plugins) {
            if (!check(plugin.verified, "VST2 Shell child was not marked verified")
                || !check(!plugin.internalPlugin.isEmpty(),
                           "VST2 Shell child did not preserve internal plugin identity")
                || !check(plugin.name != QStringLiteral("Fixture Child Two")
                              || (plugin.isInstrument && plugin.category == QStringLiteral("instrument")),
                           "VST2 synth was not recognized as an instrument"))
                return 1;
        }

        int incrementalWriteCount = 0;
        auto incrementalOptions = firstShellOptions;
        incrementalOptions.progressCallback = {};
        incrementalOptions.infoWrittenCallback = [&] (const WinJACKNexus::VstScanner::PluginInfo&) {
            ++incrementalWriteCount;
        };
        const auto incrementalShellReport = scanner.scan(incrementalOptions);
        int incrementalInfoFileCount = 0;
        QDirIterator incrementalInfoIterator(shellOutput,
                                             { QStringLiteral("*.vstinfo") },
                                             QDir::Files,
                                             QDirIterator::Subdirectories);
        while (incrementalInfoIterator.hasNext()) {
            incrementalInfoIterator.next();
            ++incrementalInfoFileCount;
        }
        if (!check(incrementalShellReport.plugins.isEmpty(),
                   "incremental Shell scan reported skipped records as new writes")
            || !check(incrementalWriteCount == 0,
                      "incremental Shell scan rewrote verified child records")
            || !check(incrementalInfoFileCount == 2,
                      "incremental Shell scan removed verified child info files"))
            return 1;

        int rescanWriteCount = 0;
        auto rescanOptions = firstShellOptions;
        rescanOptions.rescanVerifiedPlugins = true;
        rescanOptions.progressCallback = {};
        rescanOptions.infoWrittenCallback = [&] (const WinJACKNexus::VstScanner::PluginInfo& info) {
            ++rescanWriteCount;
            firstWritesWereVisible = firstWritesWereVisible && QFileInfo::exists(info.infoFile);
        };
        const auto rescanShellReport = scanner.scan(rescanOptions);
        if (!check(rescanShellReport.plugins.size() == 2,
                   "forced Shell rescan did not return both child plugins")
            || !check(rescanWriteCount == 2,
                      "forced Shell rescan did not rewrite verified child records"))
            return 1;
    }

    const auto invalidFixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/Vst2 Invalid Fixture.dll");
    if (QFileInfo::exists(invalidFixture)) {
        const auto invalidOutput = temporaryDirectory.path() + QStringLiteral("/invalid-vst2-output");
        const auto invalidReport = scanner.scan({ QStringList { invalidFixture }, invalidOutput });
        bool invalidHasError = false;
        bool sawSkipMessage = false;
        for (const auto& message : invalidReport.messages) {
            invalidHasError = invalidHasError || message.error;
            sawSkipMessage = sawSkipMessage
                || message.text.startsWith(QStringLiteral("跳过无效 VST DLL"));
        }
        QDirIterator invalidInfoIterator(invalidOutput,
                                         { QStringLiteral("*.vstinfo") },
                                         QDir::Files,
                                         QDirIterator::Subdirectories);
        if (!check(invalidReport.plugins.isEmpty(),
                   "invalid VST2 fixture produced a plugin record")
            || !check(!invalidHasError,
                      "invalid VST2 fixture produced an error message")
            || !check(sawSkipMessage,
                      "invalid VST2 fixture was not reported as skipped")
            || !check(!invalidInfoIterator.hasNext(),
                      "invalid VST2 fixture wrote an info file"))
            return 1;
    }

    const auto synthFixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/Vst2 Synth Fixture.dll");
    if (QFileInfo::exists(synthFixture)) {
        const auto synthOutput = temporaryDirectory.path() + QStringLiteral("/synth-vst2-output");
        const auto firstSynthReport = scanner.scan({ QStringList { synthFixture }, synthOutput });
        if (!check(firstSynthReport.plugins.size() == 1,
                   "VST2 synth fixture did not produce a plugin record")
             || !check(firstSynthReport.plugins.front().isInstrument,
                 "VST2 synth fixture was not recognized as an instrument")
             || !check(firstSynthReport.plugins.front().name == QStringLiteral("Vst2 Synth Fixture"),
                 "VST2 synth fixture did not fall back to the DLL file name"))
            return 1;

        QDirIterator synthInfoIterator(synthOutput,
                                       { QStringLiteral("*.vstinfo") },
                                       QDir::Files,
                                       QDirIterator::Subdirectories);
        if (!check(synthInfoIterator.hasNext(),
                   "VST2 synth fixture did not write an info file"))
            return 1;
        const auto synthInfoFile = synthInfoIterator.next();
        QFile synthInfo(synthInfoFile);
        if (!check(synthInfo.open(QIODevice::ReadOnly),
                   "failed to open VST2 synth info file"))
            return 1;
        auto synthObject = QJsonDocument::fromJson(synthInfo.readAll()).object();
        synthInfo.close();
        const auto staleDirectory = synthOutput + QStringLiteral("/VST2/Effects");
        const auto staleInfoFile = staleDirectory + QStringLiteral("/Vst2 Synth Fixture.vstinfo");
        if (!check(QDir().mkpath(staleDirectory)
                       && QFile::rename(synthInfoFile, staleInfoFile),
                   "failed to move VST2 synth info into the stale Effect directory"))
            return 1;
        synthObject[QStringLiteral("category")] = QStringLiteral("instrument");
        synthObject[QStringLiteral("isInstrument")] = true;
        synthObject[QStringLiteral("vst2ProbeVersion")] = 0;
        QFile staleSynthInfo(staleInfoFile);
        if (!check(staleSynthInfo.open(QIODevice::WriteOnly | QIODevice::Truncate)
                       && staleSynthInfo.write(QJsonDocument(synthObject).toJson()) > 0,
                   "failed to create stale VST2 synth info"))
            return 1;
        staleSynthInfo.close();

        int refreshedWriteCount = 0;
        WinJACKNexus::VstScanner::ScanOptions refreshOptions;
        refreshOptions.searchPaths = { synthFixture };
        refreshOptions.outputDirectory = synthOutput;
        refreshOptions.infoWrittenCallback = [&] (const WinJACKNexus::VstScanner::PluginInfo&) {
            ++refreshedWriteCount;
        };
        const auto refreshedSynthReport = scanner.scan(refreshOptions);
        if (!check(refreshedSynthReport.plugins.size() == 1,
                   "stale VST2 synth info was not refreshed")
            || !check(refreshedSynthReport.plugins.front().isInstrument,
                      "refreshed VST2 synth info remained an effect")
            || !check(refreshedWriteCount == 1,
                 "stale VST2 synth info was not rewritten"))
            return 1;
         int refreshedInfoFileCount = 0;
         QDirIterator refreshedInfoIterator(synthOutput,
                             { QStringLiteral("*.vstinfo") },
                             QDir::Files,
                             QDirIterator::Subdirectories);
         while (refreshedInfoIterator.hasNext()) {
             const auto infoFile = refreshedInfoIterator.next();
             ++refreshedInfoFileCount;
             const auto refreshedInfo = WinJACKNexus::VstScanner::Scanner::loadInfoFile(infoFile);
               if (!check(refreshedInfo.has_value() && refreshedInfo->isInstrument
                          && refreshedInfo->vst2ProbeVersion > 0,
                   "refreshed VST2 synth info has stale persisted probe data"))
              return 1;
         }
         if (!check(refreshedInfoFileCount == 1,
                 "stale VST2 Effect info file was not removed"))
             return 1;
    }
#endif
    return 0;
}
