// Offline discovery/state/theme verification of the deployed directory tree.
// SPDX-License-Identifier: AGPL-3.0-only
#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        check(argc == 2 && app.platformName() == "windows", "Windows deployed root required");
        const auto root = QString::fromUtf8(argv[1]);
        QDirIterator files(root + "/nodes/midi_tools", {"*.dll"}, QDir::Files,
                           QDirIterator::Subdirectories);
        unsigned discovered = 0;
        while (files.hasNext())
        {
            const auto path = files.next();
            const auto library = LoadLibraryW(reinterpret_cast<const wchar_t *>(path.utf16()));
            check(library, "load recursively deployed DLL and dependencies");
            const auto symbol = [library](const char *name) {
                const auto result = GetProcAddress(library, name);
                check(result, "required host discovery/state export");
                return result;
            };
            const auto version = reinterpret_cast<decltype(&wjn_get_node_api_version)>(
                symbol("wjn_get_node_api_version"));
            const auto category =
                reinterpret_cast<decltype(&wjn_get_node_category)>(symbol("wjn_get_node_category"));
            const auto create =
                reinterpret_cast<decltype(&wjn_create_node)>(symbol("wjn_create_node"));
            const auto destroy =
                reinterpret_cast<decltype(&wjn_destroy_node)>(symbol("wjn_destroy_node"));
            const auto get = reinterpret_cast<decltype(&wjn_get_node_settings_json)>(
                symbol("wjn_get_node_settings_json"));
            const auto set = reinterpret_cast<decltype(&wjn_set_node_settings_json)>(
                symbol("wjn_set_node_settings_json"));
            check(std::string(version()) == "4" && std::string(category()) == "Midi Tools",
                  "host discovery identity");
            WinJACKNexus::UI::ThemeNamespaceRegistry::Lease lease;
            QString error;
            check(WinJACKNexus::UI::ThemeNamespaceRegistry::instance().registerNodeModule(
                      [library](const char *name) {
                          return reinterpret_cast<QFunctionPointer>(GetProcAddress(library, name));
                      },
                      path, &lease, &error),
                  qPrintable(error));
            check(!lease.isEmpty(), "each DLL owns a valid independent theme descriptor");
            auto *first = create();
            check(first, "create offline node without init");
            auto settings = QJsonDocument::fromJson(get(first)).object();
            settings["clientName"] = QStringLiteral("MIDI_恢复_日本語_한국어");
            const auto state = QJsonDocument(settings).toJson(QJsonDocument::Compact);
            check(set(first, state.constData()), "restore Unicode name and full node state");
            const QByteArray saved(get(first));
            destroy(first);
            auto *second = create();
            check(second && set(second, saved.constData()), "new instance restores saved state");
            check(QJsonDocument::fromJson(get(second)) == QJsonDocument::fromJson(saved),
                  "project payload round trip preserves all settings");
            check(!set(second, "{\"version\":999}") && QByteArray(get(second)) == saved,
                  "invalid project state leaves current settings intact");
            destroy(second);
            for (const auto language : {"en-US", "zh-CN", "ja-JP", "ko-KR"})
            {
                QFile catalog(QFileInfo(path).absolutePath() + "/locate/" + language + ".json");
                check(catalog.open(QIODevice::ReadOnly) &&
                          QJsonDocument::fromJson(catalog.readAll()).isObject(),
                      "deployed catalog");
            }
            lease.reset();
            check(FreeLibrary(library), "release node/theme before module unload");
            ++discovered;
        }
        check(discovered == 3, "all three recursive node deployment paths discovered");
        std::cout << "PASS deployed recursive discovery, three theme descriptors, four languages, "
                     "Unicode project-state recreation/rejection; no JACK init or GUI show\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
