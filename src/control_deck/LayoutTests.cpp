/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../../Develop/Node/WindowShowCounter.hpp"
#include "Service.hpp"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QMouseEvent>
#include <QProcess>
#include <QScreen>
#include <QSharedMemory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <Windows.h>
#include <iostream>
#include <stdexcept>

namespace SS = WinJACKNexus::ControlDeck;
namespace DSP = WinJACKNexus::Common::ControlDeck;
namespace UI = WinJACKNexus::UI;
namespace C = UI::Controls;

namespace
{
unsigned checks{};
void check(bool passed, const char *description)
{
    std::cout << (passed ? "PASS " : "FAIL ") << ++checks << ' ' << description << std::endl;
    if (!passed)
        throw std::runtime_error(description);
}
void settle()
{
    QTest::qWait(160);
}

void menuCommand(QWidget *widget, const QString &key, std::function<void(QDialog *)> edit = {})
{
    bool found = false;
    bool answered = !bool(edit);
    QTimer::singleShot(40, [&, edit] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;
        auto *action = menu->findChild<QAction *>("controlDeck." + key);
        found = action && action->isEnabled() && IsWindowVisible(reinterpret_cast<HWND>(menu->winId()));
        if (found)
        {
            if (edit)
                QTimer::singleShot(40, [&, edit] {
                    if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                    {
                        edit(dialog);
                        answered = true;
                        dialog->accept();
                    }
                });
            action->trigger();
        }
        menu->close();
    });
    const auto position = widget->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, widget->mapToGlobal(position));
    QApplication::sendEvent(widget, &event);
    settle();
    check(found && answered, qPrintable("native menu command " + key));
}

void drag(QWidget *widget, QPoint start, QPoint delta)
{
    const auto global = widget->mapToGlobal(start + delta);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, start + delta, global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &move);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, start + delta);
    settle();
}

void nativeClick(QPoint point, bool alt = false)
{
    SetCursorPos(point.x(), point.y());
    if (alt)
    {
        INPUT key{};
        key.type = INPUT_KEYBOARD;
        key.ki.wVk = VK_MENU;
        check(SendInput(1, &key, sizeof(key)) == 1, "send Alt down");
        QTest::qWait(40);
    }
    INPUT mouse{};
    mouse.type = INPUT_MOUSE;
    mouse.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    check(SendInput(1, &mouse, sizeof(mouse)) == 1, "send native left down");
    QTest::qWait(40);
    mouse.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    check(SendInput(1, &mouse, sizeof(mouse)) == 1, "send native left up");
    if (alt)
    {
        INPUT key{};
        key.type = INPUT_KEYBOARD;
        key.ki.wVk = VK_MENU;
        key.ki.dwFlags = KEYEVENTF_KEYUP;
        check(SendInput(1, &key, sizeof(key)) == 1, "send Alt up");
    }
    settle();
}

class LowerWindow : public QWidget
{
  public:
    std::function<void()> clicked;

  protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && clicked)
            clicked();
    }
};
struct LowerState
{
    quint64 handle{};
    unsigned clicks{};
};

class LowerProcess
{
  public:
    explicit LowerProcess(QRect geometry)
        : memory_("wjn-switch-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces)), geometry_(geometry)
    {
        check(memory_.create(sizeof(LowerState)), "create observation channel for independent lower process");
        memory_.lock();
        *static_cast<LowerState *>(memory_.data()) = {};
        memory_.unlock();
        process_.setProcessChannelMode(QProcess::ForwardedChannels);
        process_.start(QCoreApplication::applicationFilePath(),
                       {"--lower-window", QString::number(geometry.x()), QString::number(geometry.y()),
                        QString::number(geometry.width()), QString::number(geometry.height()), memory_.key()});
        check(process_.waitForStarted(2000), "start visible lower-window test process");
        for (unsigned attempt = 0; attempt < 100 && !read().handle; ++attempt)
            QTest::qWait(20);
        check(read().handle != 0, "independent lower process reports real HWND");
        AllowSetForegroundWindow(DWORD(process_.processId()));
        SetForegroundWindow(handle());
        settle();
        if (GetForegroundWindow() != handle())
        {
            RECT frame{};
            check(GetWindowRect(handle(), &frame), "read independent window frame before activation");
            nativeClick({(frame.left + frame.right) / 2,
                         frame.top + GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION) / 2});
        }
        check(GetForegroundWindow() == handle(), "another process is the foreground application");
    }
    ~LowerProcess()
    {
        if (process_.state() != QProcess::NotRunning)
        {
            PostMessageW(handle(), WM_CLOSE, 0, 0);
            if (!process_.waitForFinished(1500))
            {
                process_.kill();
                process_.waitForFinished(1500);
            }
        }
    }
    LowerState read()
    {
        memory_.lock();
        const auto state = *static_cast<const LowerState *>(memory_.constData());
        memory_.unlock();
        return state;
    }
    HWND handle()
    {
        return reinterpret_cast<HWND>(quintptr(read().handle));
    }
    QRect geometry() const
    {
        return geometry_;
    }

  private:
    QSharedMemory memory_;
    QProcess process_;
    QRect geometry_;
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (argc == 7 && QByteArray(argv[1]) == "--lower-window")
    {
        app.setQuitOnLastWindowClosed(true);
        QSharedMemory memory(QString::fromLocal8Bit(argv[6]));
        if (!memory.attach())
            return 2;
        LowerWindow lower;
        lower.setGeometry(QString::fromLocal8Bit(argv[2]).toInt(), QString::fromLocal8Bit(argv[3]).toInt(),
                          QString::fromLocal8Bit(argv[4]).toInt(), QString::fromLocal8Bit(argv[5]).toInt());
        lower.clicked = [&] {
            memory.lock();
            ++static_cast<LowerState *>(memory.data())->clicks;
            memory.unlock();
            std::cout << "LOWER_PROCESS_CLICK" << std::endl;
        };
        lower.show();
        memory.lock();
        static_cast<LowerState *>(memory.data())->handle = quint64(lower.winId());
        memory.unlock();
        std::cout << "LOWER_PROCESS_VISIBLE" << std::endl;
        return app.exec();
    }
    WindowShowCounter shows;
    app.installEventFilter(&shows);
    try
    {
        check(app.platformName() == "windows", "real Windows Qt platform");
        app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
        QTemporaryDir temporary;
        UI::HostI18n host({temporary.filePath("language.ini"), {"en-US"}, false});
        auto &language = host.manager();
        QString error;
        check(host.initialize(QCoreApplication::applicationDirPath(), &error), "isolated i18n manager");
        UI::ThemeCore theme;
        UI::ThemeManager::installThemeCore(&theme);
        UI::ThemeManager adapter;
        adapter.bindThemeCore(&theme);
        adapter.applyToApp(&app);
        auto &service = SS::Service::instance();
        check(service.installUI(&error), qPrintable(error.isEmpty() ? "module UI registration" : error));
        DSP::Gate a, b, c;
        service.attach(&a, {"a", QString::fromUtf8("话筒"), "main", "Mono", &a, true});
        service.attach(&b, {"b", QString::fromUtf8("音楽"), "main/sub", "Stereo", &b, true});
        service.attach(&c, {"c", QString::fromUtf8("버튼"), "main/sub", "MIDI", &c, true});
        service.state.cards["a"].geometry = {20, 60, 160, 48};
        service.state.cards["b"].geometry = {260, 60, 160, 48};
        service.state.cards["c"].geometry = {500, 60, 160, 48};
        auto *editor = service.showEditor();
        editor->move(40, 160);
        editor->resize(900, 420);
        settle();
        check(editor->isVisible() && qAbs(editor->devicePixelRatioF() - 1) < .001, "visible editor at 100 percent");
        auto *first = editor->findChild<C::ActionButton *>("controlDeckButton.a");
        auto *second = editor->findChild<C::ActionButton *>("controlDeckButton.b");
        auto *third = editor->findChild<C::ActionButton *>("controlDeckButton.c");
        auto *canvas = editor->findChild<QWidget *>("controlDeckCanvas");
        check(first && second && third && canvas, "shared controls available");
        QTest::mouseClick(first, Qt::LeftButton, Qt::ControlModifier);
        QTest::mouseClick(second, Qt::LeftButton, Qt::ControlModifier);
        check(service.find("a")->desired && service.find("b")->desired, "Ctrl multiselection does not toggle signal");
        menuCommand(first, "size", [](QDialog *dialog) {
            dialog->findChild<C::IntegerField *>("controlDeck.width")->setValue(220);
            dialog->findChild<C::IntegerField *>("controlDeck.height")->setValue(60);
        });
        check(service.state.cards["a"].geometry.size() == QSize(220, 60) &&
                  service.state.cards["b"].geometry.size() == QSize(220, 60) &&
                  service.state.cards["c"].geometry.size() == QSize(160, 48),
              "batch dimensions affect selected buttons only");
        menuCommand(first, "exclusive", [](QDialog *) {});
        check(service.state.exclusive.size() == 1 && !service.find("a")->desired && !service.find("b")->desired,
              "creating exclusive relation explicitly resolves existing enabled members");
        QTest::mouseClick(first, Qt::LeftButton);
        QTest::mouseClick(second, Qt::LeftButton);
        check(!service.find("a")->desired && service.find("b")->desired && service.find("c")->desired,
              "exclusive clicks leave independent member unchanged");
        menuCommand(first, "newGroup", [](QDialog *dialog) {
            dialog->findChild<QLineEdit *>("controlDeck.textInput")->setText(QString::fromUtf8("演奏组"));
        });
        check(service.state.groups.size() == 1 && service.state.exclusive.size() == 1,
              "layout grouping preserves separate exclusive relation");
        const auto groupId = service.state.groups.firstKey();
        auto *group = editor->findChild<QGroupBox *>("controlDeckGroup." + groupId);
        check(group && group->isVisible(), "shared group card visible in editor");
        menuCommand(group, "renameGroup", [](QDialog *dialog) {
            dialog->findChild<QLineEdit *>("controlDeck.textInput")->setText(QString::fromUtf8("舞台 무대"));
        });
        check(service.state.groups[groupId].name == QString::fromUtf8("舞台 무대"), "group card name editable");
        const auto beforeA = service.state.cards["a"].geometry.topLeft();
        const auto beforeB = service.state.cards["b"].geometry.topLeft();
        drag(group, {group->width() / 2, 8}, {32, 40});
        check(service.state.cards["a"].geometry.topLeft() == beforeA + QPoint(32, 40) &&
                  service.state.cards["b"].geometry.topLeft() == beforeB + QPoint(32, 40),
              "group drag preserves member offsets");
        check(!service.find("a")->desired && service.find("b")->desired, "dragging does not toggle buttons");
        menuCommand(canvas, "gridSize", [](QDialog *dialog) {
            dialog->findChild<C::IntegerField *>("controlDeck.gridX")->setValue(20);
            dialog->findChild<C::IntegerField *>("controlDeck.gridY")->setValue(20);
        });
        menuCommand(canvas, "grid");
        const auto offset = service.state.cards["b"].geometry.topLeft() - service.state.cards["a"].geometry.topLeft();
        drag(group, {group->width() / 2, 8}, {13, 17});
        check(service.state.cards["a"].geometry.x() % 20 == 0 && service.state.cards["a"].geometry.y() % 20 == 0 &&
                  service.state.cards["b"].geometry.topLeft() - service.state.cards["a"].geometry.topLeft() == offset,
              "grid snaps group anchor without distorting member layout");
        menuCommand(first, "color", [](QDialog *dialog) {
            dialog->findChild<QLineEdit *>("controlDeck.textInput")->setText("#ff336699");
        });
        check(service.state.cards["a"].color == QColor("#ff336699") &&
                  first->property("surfaceColor").value<QColor>() == QColor("#ff336699"),
              "instance color reaches shared control");
        menuCommand(first, "themeColor");
        check(!service.state.cards["a"].color.isValid(), "theme inheritance deletes instance color override");
        menuCommand(canvas, "opacity", [](QDialog *dialog) {
            dialog->findChild<C::IntegerField *>("controlDeck.opacity")->setValue(70);
        });
        check(editor->windowOpacity() == 1 &&
                  qobject_cast<QGraphicsOpacityEffect *>(first->graphicsEffect())->opacity() == .7,
              "opacity applies to button only");
        const auto editorRect = editor->geometry();
        const auto firstRect = QRect(first->mapToGlobal(QPoint()), first->size());
        const auto secondRect = QRect(second->mapToGlobal(QPoint()), second->size());
        const auto allLayout = service.state.toJson().value("cards");
        auto *lock = service.quickMenu()->findChild<QAction *>("controlDeckLock");
        lock->trigger();
        settle();
        check(service.state.locked && !editor->isVisible(), "lock hides editor background");
        auto *firstOverlay = first->window();
        auto *secondOverlay = second->window();
        check(firstOverlay != editor && firstOverlay->isVisible() && secondOverlay->isVisible() && !group->isVisible(),
              "lock shows buttons only without group backgrounds");
        check(firstOverlay->geometry() == firstRect && secondOverlay->geometry() == secondRect,
              "lock retains exact arranged screen positions and dimensions");
        check(GetWindowLongPtrW(reinterpret_cast<HWND>(firstOverlay->winId()), GWL_EXSTYLE) & WS_EX_TOPMOST,
              "real native overlay has topmost style");
        LowerProcess lower(firstRect.united(secondRect).adjusted(-25, -25, 25, 25));
        const bool previous = service.find("a")->desired;
        nativeClick(firstRect.center());
        check(service.find("a")->desired != previous && lower.read().clicks == 0,
              "overlay remains operable above foreground ordinary window");
        auto *through = service.quickMenu()->findChild<QAction *>("controlDeckClickThrough");
        through->trigger();
        settle();
        check(service.state.window.clickThrough && through->isChecked(), "quick menu enables native click-through");
        const bool beforeThrough = service.find("a")->desired;
        nativeClick(firstRect.center());
        check(service.find("a")->desired == beforeThrough && lower.read().clicks == 1,
              "ordinary native click passes to lower real window");
        nativeClick(firstRect.center(), true);
        check(service.find("a")->desired != beforeThrough && lower.read().clicks == 1,
              "Alt-left toggles exactly one button without clicking lower window");
        const auto gap = QPoint((firstRect.right() + secondRect.left()) / 2, firstRect.center().y());
        nativeClick(gap);
        check(lower.read().clicks == 2, "gap between buttons does not intercept lower application");
        SS::State restored;
        check(SS::State::fromBase64(service.state.toBase64(), &restored) && restored.locked &&
                  restored.window.clickThrough && restored.window.opacity == 70 &&
                  restored.groups[groupId].name == QString::fromUtf8("舞台 무대"),
              "locked presentation and named groups persist as Base64");
        QDir().mkpath(QString::fromUtf8(SWITCH_SOURCE_DIR) + "/Docs/ControlDeck/S4");
        const auto lowerRect = lower.geometry();
        const auto capture =
            firstOverlay->screen()->grabWindow(0, lowerRect.x(), lowerRect.y(), lowerRect.width(), lowerRect.height());
        check(!capture.isNull() &&
                  capture.save(QString::fromUtf8(SWITCH_SOURCE_DIR) + "/Docs/ControlDeck/S4/overlays.png"),
              "capture real desktop composition with foreground test window");
        auto *open = service.quickMenu()->findChild<QAction *>("controlDeckOpenEditor");
        open->trigger();
        settle();
        check(!service.state.locked && editor->isVisible() && first->window() == editor,
              "open editor unlocks and removes overlay presentation");
        check(editor->geometry() == editorRect && service.state.toJson()["cards"] == allLayout,
              "unlock retains editor geometry and arranged button layout");
        check(shows.shows + 1 <= 100, "bounded visible-window batch including independent lower window");
        std::cout << "WINDOW_SHOWS " << shows.shows + 1 << std::endl;
        service.detach(&a);
        service.detach(&b);
        service.detach(&c);
        service.shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        std::cout << "WINDOW_SHOWS " << shows.shows << std::endl;
        SS::Service::instance().shutdownUI();
        UI::ThemeManager::installThemeCore(nullptr);
        return 1;
    }
}
