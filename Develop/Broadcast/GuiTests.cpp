// SPDX-License-Identifier: AGPL-3.0-only
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGlyphRun>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLibrary>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrlQuery>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <cstdio>
#include <stdexcept>

namespace Model = WinJACKNexus::Common::Broadcast;
namespace UI = WinJACKNexus::UI;
using Node = WinJACKNexus::Node::INode;
namespace
{
unsigned checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
    if (!value)
        throw std::runtime_error(message);
}
template <typename T> T *control(QWidget *window, const QString &name)
{
    auto *result = dynamic_cast<T *>(window->findChild<QWidget *>(name));
    check(result != nullptr, "production window control exists");
    return result;
}
class Shows final : public QObject
{
  public:
    unsigned count = 0;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
            if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->isWindow())
                ++count;
        return false;
    }
};
} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    Shows shows;
    application.installEventFilter(&shows);
    try
    {
        check(QGuiApplication::platformName() == QStringLiteral("windows"), "normal Windows Qt platform");
        QTemporaryDir temporary;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        UI::I18nOptions options;
        options.settingsFile = temporary.filePath(QStringLiteral("language.ini"));
        options.systemUiLanguages = {QStringLiteral("zh-CN")};
        UI::I18nManager language(options);
        QString error;
        check(language.install(&error), "isolated language manager installed");
        UI::ThemeCore theme;
        check(theme.isValid(), "shared theme core initialized");
        UI::ThemeManager::installThemeCore(&theme);
        UI::ThemeManager themeManager;
        check(themeManager.applyToApp(&application), "shared theme applied to real app");
        const auto modulePath = application.applicationDirPath() + "/nodes/bc/mod_broadcast.dll";
        QLibrary module(modulePath);
        check(module.load(), "actual broadcast DLL loaded");
        const auto metadata =
            reinterpret_cast<int (*)(WjnNodeMetadataV4 *)>(module.resolve("wjn_get_node_metadata_v4"));
        WjnNodeMetadataV4 info{sizeof(info)};
        check(metadata && metadata(&info) && info.capabilities == WJN_NODE_JACK_NODE &&
                  (info.features & WJN_NODE_HAS_GUI),
              "actual JACK-only DLL advertises standalone GUI");
        auto lease = language.registerLibrary(modulePath, UI::I18nManager::Extension::node, 0, &error);
        check(bool(lease), "production four-language catalog registered");
        const auto create = reinterpret_cast<Node *(*)(uint32_t)>(module.resolve("wjn_create_node_v4"));
        const auto destroy = reinterpret_cast<void (*)(Node *)>(module.resolve("wjn_destroy_node"));
        const auto get =
            reinterpret_cast<const char *(*)(Node *)>(module.resolve("wjn_get_node_settings_json"));
        const auto set =
            reinterpret_cast<int (*)(Node *, const char *)>(module.resolve("wjn_set_node_settings_json"));
        check(create && destroy && get && set, "node factories and state exports exist");
        auto *node = create(WJN_NODE_MODE_JACK);
        check(node != nullptr, "node created without a running JACK server");
        auto *window = QWidget::find(reinterpret_cast<WId>(node->createWindow(nullptr)));
        check(window && window->isWindow() && window->parentWidget() == nullptr,
              "independent top-level production window");
        check(QTest::qWaitForWindowExposed(window), "real Windows window exposed");
        QTest::qWait(250);
        check(IsWindowVisible(reinterpret_cast<HWND>(window->winId())), "native window genuinely visible");
        std::printf("System font: %s; logical DPI: %d; device pixel ratio: %.2f\n",
                    window->font().family().toUtf8().constData(), window->logicalDpiX(),
                    window->devicePixelRatioF());
        check(window->windowTitle() == QStringLiteral("广播推流节点"), "Chinese catalog displayed");
        auto state = [&]
        {
            Model::State value;
            check(Model::State::fromJson(QJsonDocument::fromJson(get(node)).object(), value),
                  "actual DLL state parsed");
            return value;
        };
        auto *name = control<QLineEdit>(window, "bcName");
        auto *parent = control<QComboBox>(window, "bcParent");
        auto *layout = control<QComboBox>(window, "bcLayout");
        auto click = [&](const char *id)
        {
            control<QPushButton>(window, QString::fromLatin1(id))->click();
            QTest::qWait(30);
        };
        name->setText(QStringLiteral("节目"));
        click("bcAddCategory");
        const auto root = state().categories.at(0).id;
        parent->setCurrentIndex(parent->findData(root));
        name->setText(QStringLiteral("新闻"));
        click("bcAddCategory");
        const auto child = state().categories.at(1).id;
        check(state().categoryPath(child) == QStringList{QStringLiteral("节目"), QStringLiteral("新闻")},
              "GUI creates nested categories");
        parent->setCurrentIndex(parent->findData(child));
        name->setText(QStringLiteral("主输出"));
        layout->setCurrentIndex(layout->findData(8u));
        click("bcAddStream");
        const auto first = state().streams.at(0).id;
        check(state().streams[0].layout == Model::Layout::surround71 && state().streams[0].category == child,
              "GUI adds a named 7.1 stream in a nested category");
        name->setText(QStringLiteral("备用"));
        parent->setCurrentIndex(0);
        layout->setCurrentIndex(layout->findData(2u));
        click("bcAddStream");
        check(state().streams.size() == 2, "GUI adds independent second stream");
        unsigned meters = 0;
        for (auto *widget : window->findChildren<QWidget *>())
            if (auto *meter = dynamic_cast<UI::FxLevelMeterWidget *>(widget))
            {
                ++meters;
                check(meter->property("wjn.singleChannel").toBool(),
                      "one overall meter per multichannel stream");
            }
        check(meters == 2, "exactly two overall meters for two streams");
        auto *monitor = control<QCheckBox>(window, "bcMonitor_" + first);
        check(!monitor->isChecked(), "new monitor defaults off");
        monitor->setChecked(true);
        control<QCheckBox>(window, "bcSolo_" + first)->setChecked(true);
        control<QCheckBox>(window, "bcMute_" + first)->setChecked(true);
        auto *gain = control<UI::Controls::ConsoleKnob>(window, "bcGain_" + first);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(gain, &key);
        check(state().streams[0].mute && state().streams[0].gainDb > 0,
              "shared M and gain controls commit to node");
        node->destroyWindow(reinterpret_cast<HWND>(window->winId()));
        check(!window->isVisible(), "closing GUI hides only the view");
        check(QWidget::find(reinterpret_cast<WId>(node->createWindow(nullptr))) == window,
              "reopening reuses independent window");
        check(QTest::qWaitForWindowExposed(window), "reopened real window exposed");
        check(monitor->isChecked() && control<QCheckBox>(window, "bcSolo_" + first)->isChecked(),
              "closing and reopening preserves live Listen and Solo");
        const auto saved = QByteArray(get(node));
        check(set(node, saved.constData()), "restore current project through real DLL");
        QTest::qWait(40);
        check(!monitor->isChecked() && !control<QCheckBox>(window, "bcSolo_" + first)->isChecked(),
              "project restore safely resets Listen and Solo");
        auto *tree = control<QTreeWidget>(window, "bcTree");
        auto select = [&](const QString &id)
        {
            for (QTreeWidgetItemIterator item(tree); *item; ++item)
                if ((*item)->data(0, Qt::UserRole).toString() == id)
                    tree->setCurrentItem(*item);
        };
        select(first);
        name->setText(QStringLiteral("改名后的主输出"));
        parent->setCurrentIndex(parent->findData(root));
        click("bcApplyItem");
        check(state().streams[0].id == first && state().streams[0].category == root &&
                  state().streams[0].name == QStringLiteral("改名后的主输出"),
              "rename and move retain stream identity");
        auto *channels = control<QComboBox>(window, "bcListenChannel");
        channels->setCurrentIndex(channels->findData(QStringLiteral("Rrb")));
        click("bcCopyUrl");
        const QUrl copied(QApplication::clipboard()->text());
        const QUrlQuery query(copied);
        check(copied.path() == "/bc/listen" && query.queryItemValue("stream") == first &&
                  query.queryItemValue("channel") == "Rrb" && query.queryItemValue("format") == "opus" &&
                  !copied.toString().contains("Bearer") && copied.userInfo().isEmpty(),
              "copy stable anonymous subchannel HTTP URL");
        const auto identity = QByteArray(get(node));
        name->setText(QStringLiteral("未提交草稿"));
        name->setFocus();
        for (const auto &locale : {"en-US", "ja-JP", "ko-KR", "zh-CN"})
        {
            check(language.setLanguage(QString::fromLatin1(locale), &error),
                  "switch production catalog language");
            QTest::qWait(35);
            check(name->text() == QStringLiteral("未提交草稿") && QByteArray(get(node)) == identity,
                  "language switch preserves draft, identities and audio state");
        }
        check(theme.setOverride("--wjn-theme-global-surface-window", "#182432", {}, &error),
              "change shared theme token");
        QTest::qWait(80);
        check(window->palette().color(QPalette::Window) == QColor("#182432") &&
                  QByteArray(get(node)) == identity && name->text() == QStringLiteral("未提交草稿"),
              "theme change reaches actual window without changing draft or model");
        auto *timer = window->findChild<QTimer *>(QStringLiteral("bcRefreshTimer"));
        check(timer != nullptr, "window has shared-FPS refresh timer");
        auto &settings = UI::ApplicationSettings::instance();
        for (const auto fps : {30, 60, 120})
        {
            QMetaObject::invokeMethod(&settings, "frameRateChanged", Qt::DirectConnection, Q_ARG(int, fps));
            check(timer->interval() == 1000 / fps,
                  "refresh follows global FPS signal without changing saved settings");
        }
        QMetaObject::invokeMethod(&settings, "frameRateChanged", Qt::DirectConnection,
                                  Q_ARG(int, settings.frameRate()));
        QTextLayout glyphs(QStringLiteral("广播推流节点总体峰值"), window->font());
        glyphs.beginLayout();
        glyphs.createLine().setLineWidth(600);
        glyphs.endLayout();
        const auto runs = glyphs.glyphRuns();
        check(!runs.isEmpty(), "system font fallback produced CJK glyph runs");
        for (const auto &run : runs)
            check(!run.glyphIndexes().contains(0), "CJK glyph indices contain no missing-glyph squares");
        QTest::qWait(150);
        const auto screenshot = window->screen()->grabWindow(window->winId());
        check(!screenshot.isNull() &&
                  screenshot.save(application.applicationDirPath() + "/../../bc-s4-window.png"),
              "captured actual stable visible Windows window");
        auto *other = create(WJN_NODE_MODE_JACK);
        auto *otherWindow = QWidget::find(reinterpret_cast<WId>(other->createWindow(nullptr)));
        check(otherWindow && otherWindow != window && QTest::qWaitForWindowExposed(otherWindow),
              "second node has independent real window");
        check(control<QLineEdit>(otherWindow, "bcServer")->text() ==
                  control<QLineEdit>(window, "bcServer")->text(),
              "both windows show the same process-global server configuration");
        control<QLineEdit>(window, "bcServer")->setText(QStringLiteral("http://127.0.0.1:9443/bc"));
        control<QLineEdit>(window, "bcPublisher")->setText(QStringLiteral("ui-test"));
        click("bcSaveServer");
        check(control<QLineEdit>(otherWindow, "bcServer")->text() ==
                      QStringLiteral("http://127.0.0.1:9443/bc") &&
                  control<QLineEdit>(otherWindow, "bcPublisher")->text() == QStringLiteral("ui-test"),
              "saving isolated server configuration synchronizes both real windows");
        destroy(other);
        select(child);
        parent->setCurrentIndex(0);
        click("bcApplyItem");
        check(state().categories[1].parent.isEmpty(), "GUI moves category to root");
        click("bcRemoveItem");
        check(state().categories.size() == 1, "GUI deletes empty category");
        select(first);
        click("bcRemoveItem");
        check(state().streams.size() == 1, "GUI deletes selected stream");
        destroy(node);
        check(shows.count <= 100, "real-window batch remains below 100 shows");
        std::printf("PASS %u checks; native window shows: %u; platform: windows; no JACK initialization\n",
                    checks, shows.count);
        UI::ThemeManager::installThemeCore(nullptr);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "GUI test failed: %s\n", error.what());
        UI::ThemeManager::installThemeCore(nullptr);
        return 1;
    }
}
