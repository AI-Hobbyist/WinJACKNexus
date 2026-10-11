// SPDX-License-Identifier: AGPL-3.0-only
// Offline ABI/state and actual Windows editor checks; never initialize JACK.
#include <QApplication>
#include <QDir>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTest>
#include <QTextLayout>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Module
{
    const char *folder;
    const char *id;
    const char *name;
    const char *category;
};
constexpr Module modules[]{
    {"adapter", "wasapi_in", "WASAPI In", "Windows Native Driver"},
    {"adapter", "wasapi_out", "WASAPI Out", "Windows Native Driver"},
    {"adapter", "system_midi_in", "System MIDI In", "Windows Native Driver"},
    {"adapter", "system_midi_out", "System MIDI Out", "Windows Native Driver"},
    {"meter", "meter_mono", "Meter Mono", "Meter"},
    {"meter", "meter_stereo", "Meter Stereo", "Meter"},
    {"meter", "meter_5_1", "Meter 5.1", "Meter"},
    {"meter", "meter_7_1", "Meter 7.1", "Meter"},
    {"meter", "meter_custom", "Meter Custom", "Meter"}};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try
    {
        require(argc == 3 && app.platformName() == "windows", "Windows runtime and capture paths");
        const QString runtime = QString::fromUtf8(argv[1]);
        const QString captures = QString::fromUtf8(argv[2]);
        require(QDir().mkpath(captures), "capture directory");
        unsigned displays = 0;
        for (const auto &module : modules)
        {
            const QString path = runtime + "/nodes/" + module.folder + '/' + module.id + "/mod_" +
                                 module.id + ".dll";
            const HMODULE library = LoadLibraryW(reinterpret_cast<const wchar_t *>(path.utf16()));
            require(library, "independent node DLL load");
            const auto symbol = [library](const char *name) {
                const auto result = GetProcAddress(library, name);
                require(result, "required single-node export");
                return result;
            };
            const auto count =
                reinterpret_cast<decltype(&wjn_get_node_count)>(symbol("wjn_get_node_count"));
            const auto name =
                reinterpret_cast<decltype(&wjn_get_node_name)>(symbol("wjn_get_node_name"));
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
            const auto editor = reinterpret_cast<decltype(&wjn_create_settings_widget)>(
                symbol("wjn_create_settings_widget"));
            const auto remove = reinterpret_cast<decltype(&wjn_destroy_settings_widget)>(
                symbol("wjn_destroy_settings_widget"));
            require(count() == 1 && QString::fromUtf8(name(0)) == module.name &&
                        QString::fromUtf8(category(0)) == module.category && create(1) == nullptr,
                    "one type per DLL with correct identity");
            WinJACKNexus::UI::ThemeNamespaceRegistry::Lease lease;
            QString error;
            require(WinJACKNexus::UI::ThemeNamespaceRegistry::instance().registerNodeModule(
                        [library](const char *name) {
                            return reinterpret_cast<QFunctionPointer>(
                                GetProcAddress(library, name));
                        },
                        path, &lease, &error) &&
                        !lease.isEmpty(),
                    qPrintable(error));
            auto *node = create(0);
            require(node, "offline node creation");
            auto state = QJsonDocument::fromJson(get(node)).object();
            state["clientName"] = QStringLiteral("节点_日本語_한국어");
            const auto json = QJsonDocument(state).toJson(QJsonDocument::Compact);
            require(set(node, json.constData()),
                    "Unicode state restore without device or JACK init");
            QWidget window;
            window.setWindowTitle(QString::fromUtf8(module.name));
            window.resize(600, 760);
            auto *view = editor(node, &window);
            require(view, "production settings editor");
            view->setGeometry(window.rect());
            window.show();
            window.raise();
            window.activateWindow();
            ++displays;
            QTest::qWait(650);
            require(window.isVisible() && IsWindowVisible(reinterpret_cast<HWND>(window.winId())) &&
                        window.devicePixelRatioF() == 1.0,
                    "visible native 100% window");
            QTextLayout layout(QStringLiteral("节点_日本語_한국어"), app.font());
            layout.beginLayout();
            layout.createLine();
            layout.endLayout();
            for (const auto &run : layout.glyphRuns())
                for (const auto glyph : run.glyphIndexes())
                    require(glyph != 0, "system CJK font fallback");
            require(window.screen()
                        ->grabWindow(window.winId())
                        .save(captures + '/' + module.id + ".png"),
                    "actual visible window screenshot");
            window.close();
            remove(node, view);
            const QByteArray saved(get(node));
            node->shutdown();
            destroy(node);
            node = create(0);
            require(node && set(node, saved.constData()) &&
                        QJsonDocument::fromJson(get(node)) == QJsonDocument::fromJson(saved),
                    "state recreation round trip");
            node->shutdown();
            destroy(node);
            lease.reset();
            require(FreeLibrary(library), "release instances/editors/theme before DLL");
            std::cout << "PASS " << module.id << '\n';
        }
        std::cout << "PASS 9 independent DLLs; Windows 100%, systemFont="
                  << QFontInfo(app.font()).family().toStdString()
                  << ", top-level displays=" << displays
                  << "; total displays bounded below 100; no JACK/device initialization\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
