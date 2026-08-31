#include <WinJACKNexus/UI/AnimatedWidget.hpp>
#include <WinJACKNexus/UI/IconManager.hpp>
#include <WinJACKNexus/UI/MeterWidget.hpp>
#include <WinJACKNexus/UI/NativeWindowUtils.hpp>
#include <WinJACKNexus/UI/PaintableWidget.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <Windows.h>

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QWidget>

#include <cstdio>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

class ProbePaintable final : public WinJACKNexus::UI::PaintableWidget {
public:
    using PaintableWidget::PaintableWidget;

    int paintCount = 0;

protected:
    void paintContent(QPainter&) override
    {
        ++paintCount;
    }
};

class ProbeMeter final : public WinJACKNexus::UI::MeterWidget {
public:
    using MeterWidget::MeterWidget;

protected:
    void paintContent(QPainter&) override
    {
    }
};

class ProbeAnimated final : public WinJACKNexus::UI::AnimatedWidget {
public:
    using AnimatedWidget::AnimatedWidget;

    int advanceCount = 0;

protected:
    void paintContent(QPainter&) override
    {
    }

    void advanceFrame() override
    {
        ++advanceCount;
    }
};

bool testThemePalette()
{
    using namespace WinJACKNexus::UI;
    auto dark = ThemePalette::darkRack();
    auto light = ThemePalette::lightConsole();
    bool passed = true;
    passed &= check(dark.isValid() && dark.name() == QStringLiteral("dark-rack"),
                    "dark palette has a stable name");
    passed &= check(light.color(QStringLiteral("window-bg")).isValid()
                        && light.color(QStringLiteral("window-bg")) != dark.color(QStringLiteral("window-bg")),
                    "built-in palettes provide distinct window colors");
    passed &= check(dark.resolveVariables(QStringLiteral("background: ${accent}; missing: ${unknown};"))
                        == QStringLiteral("background: #43c7a8; missing: ${unknown};"),
                    "palette resolves known variables and preserves unknown variables");
    passed &= check(dark.setColor(QStringLiteral("custom"), QColor(QStringLiteral("#123456")))
                        && dark.color(QStringLiteral("custom")) == QColor(QStringLiteral("#123456")),
                    "palette accepts custom colors");
    auto copy = dark;
    copy.setColor(QStringLiteral("accent"), QColor(QStringLiteral("#ffffff")));
    passed &= check(dark.color(QStringLiteral("accent")) != copy.color(QStringLiteral("accent")),
                    "palette copies do not share mutable colors");
    return passed;
}

bool testThemeManager(QApplication& application)
{
    using namespace WinJACKNexus::UI;
    ThemeManager manager;
    int signalCount = 0;
    int compatibilitySignalCount = 0;
    QObject::connect(&manager, &ThemeManager::themeChanged,
                     [&signalCount](const ThemePalette&) { ++signalCount; });
    QObject::connect(&manager, &ThemeManager::onThemeChanged,
                     [&compatibilitySignalCount](const ThemePalette&) { ++compatibilitySignalCount; });

    bool passed = true;
    passed &= check(manager.themeName() == QStringLiteral("dark-rack")
                        && manager.styleSheet().contains(QStringLiteral("#11181a")),
                    "theme manager loads the dark built-in stylesheet");
    passed &= check(manager.setTheme(QStringLiteral("light-console"))
                        && manager.themeName() == QStringLiteral("light-console")
                        && manager.styleSheet().contains(QStringLiteral("#f3f5f2")),
                    "theme manager switches to the light built-in stylesheet");
    passed &= check(signalCount == 1 && compatibilitySignalCount == 1,
                    "theme switching emits both supported signals");
    passed &= check(manager.setVariable(QStringLiteral("accent"), QStringLiteral("#123456"))
                        && manager.styleSheet().contains(QStringLiteral("#123456"))
                        && manager.palette().color(QStringLiteral("accent")) == QColor(QStringLiteral("#123456")),
                    "theme variables update both QSS and palette values");

    QWidget widget;
    passed &= check(manager.applyToWidget(&widget) && widget.styleSheet() == manager.styleSheet(),
                    "theme manager applies styles to a widget");
    passed &= check(manager.applyToApp(&application) && application.styleSheet() == manager.styleSheet(),
                    "theme manager applies styles to the application");
    passed &= check(!manager.setTheme(QStringLiteral("missing-theme")),
                    "theme manager rejects unknown built-in themes");

    QTemporaryFile themeFile;
    passed &= check(themeFile.open(), "temporary external stylesheet opens");
    if (themeFile.isOpen()) {
        themeFile.write("QWidget { color: ${accent}; }\n");
        themeFile.flush();
        passed &= check(manager.loadTheme(themeFile.fileName())
                            && manager.styleSheet().contains(QStringLiteral("#123456")),
                        "theme manager loads and resolves an external stylesheet");
    }

    QTemporaryDir settingsDirectory;
    passed &= check(settingsDirectory.isValid(), "temporary settings directory opens");
    if (settingsDirectory.isValid()) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
        manager.setTheme(QStringLiteral("dark-rack"));
        manager.setVariable(QStringLiteral("accent"), QStringLiteral("#654321"));
        passed &= check(manager.saveSettings(), "theme manager saves settings");
        ThemeManager restored;
        passed &= check(restored.loadSettings()
                            && restored.themeName() == QStringLiteral("dark-rack")
                            && restored.palette().color(QStringLiteral("accent")) == QColor(QStringLiteral("#654321")),
                        "theme manager restores theme and variable settings");
    }
    return passed;
}

bool testPaintableWidget()
{
    ProbePaintable widget;
    widget.resize(120, 80);
    widget.setWjnRole(QStringLiteral("meter"));
    bool passed = true;
    passed &= check(widget.property("wjnRole").toString() == QStringLiteral("meter"),
                    "paintable widget exposes its QSS role property");
    widget.show();
    QCoreApplication::processEvents();
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    const auto activeCountBefore = widget.paintCount;
    widget.render(&image);
    passed &= check(widget.paintCount > activeCountBefore, "active paintable widget paints content");

    widget.setRenderingActive(false);
    const auto inactiveCount = widget.paintCount;
    widget.render(&image);
    passed &= check(widget.paintCount == inactiveCount, "inactive paintable widget skips painting");

    widget.setRenderingActive(true);
    widget.hide();
    widget.render(&image);
    passed &= check(widget.paintCount == inactiveCount, "hidden paintable widget skips painting");

    ProbeMeter meter;
    passed &= check(meter.renderingActive() && meter.isRenderingActive(),
                    "meter widget inherits rendering controls");
    return passed;
}

bool testAnimatedWidget()
{
    ProbeAnimated widget;
    widget.resize(120, 80);
    widget.setFrameRate(120);
    widget.show();
    QCoreApplication::processEvents();
    int signalCount = 0;
    QObject::connect(&widget, &ProbeAnimated::animationFrameChanged,
                     [&signalCount](qulonglong) { ++signalCount; });
    widget.startAnimation();

    for (int index = 0; index < 40; ++index) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(3);
    }
    bool passed = true;
    passed &= check(widget.animationRunning() && widget.frameNumber() != 0
                        && widget.advanceCount != 0 && signalCount != 0,
                    "animated widget advances frames while visible");

    widget.hide();
    QCoreApplication::processEvents();
    const auto hiddenFrame = widget.frameNumber();
    passed &= check(!widget.animationRunning(), "animated widget stops its timer when hidden");
    QThread::msleep(30);
    QCoreApplication::processEvents();
    passed &= check(widget.frameNumber() == hiddenFrame, "hidden animated widget does not advance frames");

    widget.show();
    QCoreApplication::processEvents();
    passed &= check(widget.animationRunning(), "animated widget resumes when shown");
    widget.stopAnimation();
    passed &= check(!widget.animationRunning(), "animated widget can be stopped explicitly");
    return passed;
}

bool testNativeWindowUtils()
{
    using namespace WinJACKNexus::UI::NativeWindowUtils;
    bool passed = true;
    passed &= check(obtainNativeHandle(nullptr) == nullptr, "native handle rejects a null widget");
    passed &= check(scaleFactor(nullptr) == 1.0, "DPI helper provides a neutral null-HWND scale");
    passed &= check(devicePixelRatio(nullptr) == 1.0, "DPI helper provides a neutral null-widget scale");
    passed &= check(logicalToNativeSize(nullptr, QSize(100, 20)) == QSize(100, 20),
                    "DPI helper preserves logical size at neutral scale");
    passed &= check(embedNativeWindow(nullptr) == nullptr, "native embedding rejects a null HWND");
    passed &= check(!detachNativeWindow(nullptr), "native detaching rejects a null container");

    QWidget parent;
    parent.resize(300, 200);
    parent.show();
    QCoreApplication::processEvents();
    const auto parentHandle = obtainNativeHandle(&parent);
    passed &= check(parentHandle != nullptr, "native handle obtains a QWidget HWND");
    passed &= check(devicePixelRatio(&parent) > 0.0 && scaleFactor(parentHandle) > 0.0,
                    "DPI helper reports positive widget and HWND scales");

    const auto nativeWindow = CreateWindowExW(0,
                                              L"STATIC",
                                              L"WinJACKNexus UI test",
                                              WS_OVERLAPPEDWINDOW,
                                              CW_USEDEFAULT,
                                              CW_USEDEFAULT,
                                              100,
                                              60,
                                              nullptr,
                                              nullptr,
                                              GetModuleHandleW(nullptr),
                                              nullptr);
    passed &= check(nativeWindow != nullptr, "test creates a native child window");
    if (nativeWindow != nullptr) {
        auto* container = embedNativeWindow(nativeWindow, &parent);
        passed &= check(container != nullptr, "native window embeds into a Qt container");
        if (container != nullptr) {
            container->show();
            QCoreApplication::processEvents();
            passed &= check(detachNativeWindow(container), "native window detaches from its Qt container");
            delete container;
        }
        DestroyWindow(nativeWindow);
    }
    return passed;
}

bool testIcons()
{
    using namespace WinJACKNexus::UI;
    bool passed = true;
    passed &= check(IconManager::hasIcon(QStringLiteral("plus")), "embedded plus icon is discoverable");
    passed &= check(!IconManager::icon(QStringLiteral("plus")).isNull(), "icon manager loads an embedded icon");
    const auto pixmap = IconManager::pixmap(QStringLiteral("plus"), QSize(24, 24), QColor(QStringLiteral("#ffffff")));
    passed &= check(!pixmap.isNull() && pixmap.size() == QSize(24, 24), "icon manager renders a tinted pixmap");
    passed &= check(!IconManager::hasIcon(QStringLiteral("missing")), "icon manager rejects an unknown icon");
    return passed;
}

}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    bool passed = true;
    passed &= testThemePalette();
    passed &= testThemeManager(application);
    passed &= testPaintableWidget();
    passed &= testAnimatedWidget();
    passed &= testNativeWindowUtils();
    passed &= testIcons();
    if (!passed)
        return 1;
    std::puts("UI tests passed");
    return 0;
}