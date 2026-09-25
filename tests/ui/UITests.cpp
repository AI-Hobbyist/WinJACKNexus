/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/UI/AnimatedWidget.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/NativeFx/compressor/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>
#include <WinJACKNexus/UI/IconManager.hpp>
#include <WinJACKNexus/UI/MeterLevelBar.hpp>
#include <WinJACKNexus/UI/MeterWidget.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>
#include <WinJACKNexus/UI/NativeWindowUtils.hpp>
#include <WinJACKNexus/UI/PaintableWidget.hpp>
#include <WinJACKNexus/NativeFx/deesser/Ui.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#include <WinJACKNexus/NativeFx/parametric_eq/Ui.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <Windows.h>

#include <QApplication>
#include <QColorDialog>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QPainter>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QWidget>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <optional>

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

bool testApplicationSettings()
{
    using WinJACKNexus::UI::ApplicationSettings;
    auto& settings = ApplicationSettings::instance();
    const auto originalFrameRate = settings.frameRate();
    const auto selectedFrameRate = originalFrameRate == 120 ? 30 : 120;
    int signalCount = 0;
    QObject::connect(&settings, &ApplicationSettings::frameRateChanged,
                     [&signalCount](int) { ++signalCount; });

    settings.setFrameRate(selectedFrameRate);
    settings.setFrameRate(47);
    const auto passed = check(settings.frameRate() == 60 && signalCount == 2,
                              "application settings accept 30, 60 and 120 FPS while normalizing other values to 60 FPS");
    settings.setFrameRate(originalFrameRate);
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

bool imageHasVisiblePixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
        }
    }
    return false;
}

bool imageContainsColor(const QImage& image, const QColor& expected)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == expected)
                return true;
        }
    }
    return false;
}

bool imageContainsColorInRect(const QImage& image, const QRect& area, const QColor& expected, int tolerance = 8)
{
    const auto bounds = area.intersected(image.rect());
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const auto actual = image.pixelColor(x, y);
            if (std::abs(actual.red() - expected.red()) <= tolerance
                && std::abs(actual.green() - expected.green()) <= tolerance
                && std::abs(actual.blue() - expected.blue()) <= tolerance)
                return true;
        }
    }
    return false;
}

int firstColorPixelX(const QImage& image, const QColor& expected)
{
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixelColor(x, y) == expected)
                return x;
        }
    }
    return -1;
}

int lastColorPixelY(const QImage& image, const QColor& expected)
{
    for (int y = image.height() - 1; y >= 0; --y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == expected)
                return y;
        }
    }
    return -1;
}

bool testMeterLevelBar()
{
    using namespace WinJACKNexus::UI;
    MeterLevelBar bar;
    bar.resize(96, 280);
    bar.setRange(-72.0F, 12.0F);
    bar.setThresholds(-18.0F, 0.0F);
    bar.setMarks({
        { -72.0F, QStringLiteral("-72") },
        { -18.0F, QStringLiteral("-18") },
        { 0.0F, QStringLiteral("0") },
        { 12.0F, QStringLiteral("+12") }
    });
    bar.setUnit(QStringLiteral("dBFS"));
    bar.setShowScaleLabels(true);
    bar.show();
    QCoreApplication::processEvents();

    bool passed = true;
    passed &= check(!bar.value().has_value() && bar.sizeHint().width() >= 72 && bar.sizeHint().height() >= 220,
                    "meter level bar exposes an empty state with a stable labeled size hint");

    QImage emptyImage(bar.size(), QImage::Format_ARGB32_Premultiplied);
    emptyImage.fill(Qt::transparent);
    bar.render(&emptyImage);
    passed &= check(imageHasVisiblePixels(emptyImage), "meter level bar renders its empty state");

    bar.setValue(-120.0F);
    passed &= check(bar.value().has_value() && std::abs(*bar.value() + 72.0F) < 0.01F,
                    "meter level bar clamps values below its range");
    bar.setValue(99.0F);
    passed &= check(bar.value().has_value() && std::abs(*bar.value() - 12.0F) < 0.01F,
                    "meter level bar clamps values above its range");

    const auto renderLevel = [&bar] {
        QImage image(bar.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        bar.render(&image);
        return image;
    };
    const QColor green(QStringLiteral("#6fd08c"));
    const QColor yellow(QStringLiteral("#e0a04a"));
    const QColor red(QStringLiteral("#ee6b67"));
    bar.clearValue();
    bar.setValue(-20.0F);
    const auto greenImage = renderLevel();
    passed &= check(imageContainsColor(greenImage, green)
                        && !imageContainsColor(greenImage, yellow)
                        && !imageContainsColor(greenImage, red),
                    "meter level bar uses only a green segment below the warning threshold");
    bar.clearValue();
    bar.setValue(-10.0F);
    const auto yellowImage = renderLevel();
    passed &= check(imageContainsColor(yellowImage, green)
                        && imageContainsColor(yellowImage, yellow)
                        && !imageContainsColor(yellowImage, red),
                    "meter level bar adds a yellow segment between warning and zero dBFS");
    bar.clearValue();
    bar.setValue(6.0F);
    const auto redImage = renderLevel();
    passed &= check(imageContainsColor(redImage, green)
                        && imageContainsColor(redImage, yellow)
                        && imageContainsColor(redImage, red),
                    "meter level bar uses a red segment above zero dBFS without a gradient");

    bar.setThemePalette(ThemePalette::lightConsole());
    QImage scaledImage(bar.size() * 2, QImage::Format_ARGB32_Premultiplied);
    scaledImage.setDevicePixelRatio(2.0);
    scaledImage.fill(Qt::transparent);
    bar.render(&scaledImage);
    passed &= check(imageHasVisiblePixels(scaledImage), "meter level bar renders at a high device-pixel ratio");

    bar.clearValue();
    passed &= check(!bar.value().has_value() && !bar.peakValue().has_value(),
                    "meter level bar clears its current and peak values");
    return passed;
}

bool testMeterArrayFrameRate()
{
    using namespace WinJACKNexus::UI;
    MeterArrayWidget meters;
    meters.setChannelCount(3);
    meters.setFrameRate(30);
    meters.show();
    QCoreApplication::processEvents();

    const auto meterWidgets = meters.findChildren<MeterWidget*>();
    const auto initialRatePropagated = std::all_of(meterWidgets.cbegin(), meterWidgets.cend(), [](const auto* meter) {
        return meter->frameRate() == 30;
    });

    meters.setChannelCount(4);
    const auto expandedMeterWidgets = meters.findChildren<MeterWidget*>();
    const auto expandedRatePropagated = std::all_of(expandedMeterWidgets.cbegin(), expandedMeterWidgets.cend(), [](const auto* meter) {
        return meter->frameRate() == 30;
    });
    const auto childTimersStopped = std::all_of(expandedMeterWidgets.cbegin(), expandedMeterWidgets.cend(), [](const auto* meter) {
        return !meter->animationRunning();
    });
    return check(meters.frameRate() == 30 && meterWidgets.size() == 3 && initialRatePropagated
                     && expandedMeterWidgets.size() == 4 && expandedRatePropagated && childTimersStopped,
                 "meter arrays propagate their animation frame rate while sharing one timer across all channels");
}

bool testMetricReadout()
{
    using namespace WinJACKNexus::UI;
    MetricReadout readout;
    readout.resize(168, 88);
    readout.setLabel(QStringLiteral("Integrated"));
    readout.setUnit(QStringLiteral("LUFS"));
    readout.setPrecision(1);
    readout.setValue(-20.25);
    readout.show();
    QCoreApplication::processEvents();

    bool passed = true;
    passed &= check(readout.label() == QStringLiteral("Integrated")
                        && readout.value().has_value()
                        && readout.displayText() == QStringLiteral("-20.3")
                        && readout.sizeHint().width() >= 124,
                    "metric readout formats a numeric value with a stable compact size");

    QImage valueImage(readout.size(), QImage::Format_ARGB32_Premultiplied);
    valueImage.fill(Qt::transparent);
    readout.render(&valueImage);
    passed &= check(imageHasVisiblePixels(valueImage), "metric readout renders a numeric value");

    readout.resize(168, 160);
    readout.setShowSurface(false);
    readout.setAccentColor(QColor(QStringLiteral("#ff4060")));
    QImage tallValueImage(readout.size(), QImage::Format_ARGB32_Premultiplied);
    tallValueImage.fill(Qt::transparent);
    readout.render(&tallValueImage);
    passed &= check(lastColorPixelY(tallValueImage, QColor(QStringLiteral("#ff4060"))) < tallValueImage.height() * 3 / 4,
                    "metric readout keeps its value close to the label in a tall layout");

    readout.setValue(std::numeric_limits<double>::infinity());
    passed &= check(!readout.value().has_value() && readout.displayText() == QStringLiteral("--"),
                    "metric readout converts non-finite input to an empty value");
    readout.setAccentColor(QColor(QStringLiteral("#ff4060")));
    readout.setThemePalette(ThemePalette::lightConsole());
    QImage emptyImage(readout.size() * 2, QImage::Format_ARGB32_Premultiplied);
    emptyImage.setDevicePixelRatio(2.0);
    emptyImage.fill(Qt::transparent);
    readout.render(&emptyImage);
    passed &= check(imageHasVisiblePixels(emptyImage), "metric readout renders its empty state at a high device-pixel ratio");
    return passed;
}

bool testHistoryChartAndLegend()
{
    using namespace WinJACKNexus::UI;
    HistoryChartWidget chart;
    chart.resize(480, 260);
    chart.setTimeRange(0, 60000);
    chart.setTimeDisplayOffsetMilliseconds(1760000000000);
    chart.setValueRange(-70.0, 12.0);
    chart.setRightValueRange(0.0, 20.0);
    chart.setVerticalAxisLabel(QStringLiteral("dBFS"));
    chart.setRightVerticalAxisLabel(QStringLiteral("LU"));
    chart.setHorizontalAxisLabel(QStringLiteral("Elapsed"));
    chart.setTimeCursorMilliseconds(36000);
    chart.setReferenceLines({
        { -18.0, QStringLiteral("Target"), QColor(QStringLiteral("#31c6a0")), Qt::DashLine, true },
        { 11.0, QStringLiteral("LRA guide"), QColor(QStringLiteral("#e0a04a")), Qt::DotLine, true, HistoryChartAxis::right }
    });
    chart.setSeries({
        {
            QStringLiteral("peak"),
            QStringLiteral("Peak"),
            QColor(QStringLiteral("#ff5068")),
            true,
            {
                { 0, -8.0 },
                { 12000, -4.0 },
                { 24000, std::nullopt },
                { 36000, -10.0 },
                { 60000, 4.0 }
            }
        },
        {
            QStringLiteral("integrated"),
            QStringLiteral("Integrated"),
            QColor(QStringLiteral("#4a9cff")),
            true,
            {
                { 0, -24.0 },
                { 60000, -20.0 }
            }
        },
        {
            QStringLiteral("lra"),
            QStringLiteral("LRA"),
            QColor(QStringLiteral("#e0a04a")),
            true,
            {
                { 0, 5.0 },
                { 36000, 8.5 },
                { 60000, 6.2 }
            },
            HistoryChartAxis::right,
            true
        }
    });
    chart.show();
    QCoreApplication::processEvents();

    bool passed = true;
    passed &= check(chart.series().size() == 3 && chart.referenceLines().size() == 2
                        && chart.rightMinimumValue() == 0.0 && chart.rightMaximumValue() == 20.0
                        && chart.timeDisplayOffsetMilliseconds() == std::optional<std::int64_t> { 1760000000000 }
                        && chart.timeCursorMilliseconds() == std::optional<std::int64_t> { 36000 }
                        && chart.series().back().axis == HistoryChartAxis::right
                        && chart.series().back().preservePointOrder
                        && chart.sizeHint().width() >= 320,
                    "history chart accepts generic dual-axis series, references and a time cursor");
    chart.setTimeDisplayOffsetMilliseconds(std::nullopt);
    QImage relativeTimeAxisImage(chart.size(), QImage::Format_ARGB32_Premultiplied);
    relativeTimeAxisImage.fill(Qt::transparent);
    chart.render(&relativeTimeAxisImage);
    chart.setTimeDisplayOffsetMilliseconds(1760000000000);
    QImage chartImage(chart.size(), QImage::Format_ARGB32_Premultiplied);
    chartImage.fill(Qt::transparent);
    chart.render(&chartImage);
    passed &= check(imageHasVisiblePixels(chartImage)
                        && chartImage != relativeTimeAxisImage,
                    "history chart renders populated series and visible system-time X-axis labels");

    QMouseEvent hoverEvent(QEvent::MouseMove,
                           QPointF(280.0, 120.0),
                           Qt::NoButton,
                           Qt::NoButton,
                           Qt::NoModifier);
    QApplication::sendEvent(&chart, &hoverEvent);
    QCoreApplication::processEvents();
    QImage hoveredChartImage(chart.size(), QImage::Format_ARGB32_Premultiplied);
    hoveredChartImage.fill(Qt::transparent);
    chart.render(&hoveredChartImage);
    const auto peakHover = std::find_if(chart.hoverEntries().cbegin(), chart.hoverEntries().cend(), [](const HistoryChartHoverEntry& entry) {
        return entry.id == QStringLiteral("peak");
    });
    const auto integratedHover = std::find_if(chart.hoverEntries().cbegin(), chart.hoverEntries().cend(), [](const HistoryChartHoverEntry& entry) {
        return entry.id == QStringLiteral("integrated");
    });
    const auto lraHover = std::find_if(chart.hoverEntries().cbegin(), chart.hoverEntries().cend(), [](const HistoryChartHoverEntry& entry) {
        return entry.id == QStringLiteral("lra");
    });
    passed &= check(chart.hoverTimeMilliseconds() == std::optional<std::int64_t> { 36000 }
                        && chart.hoverEntries().size() == 3
                        && peakHover != chart.hoverEntries().cend() && peakHover->value == std::optional<double> { -10.0 }
                        && integratedHover != chart.hoverEntries().cend() && integratedHover->value.has_value()
                        && std::abs(*integratedHover->value + 21.6) < 0.01
                        && lraHover != chart.hoverEntries().cend() && lraHover->value == std::optional<double> { 8.5 },
                    "history chart hover snaps to a sample time and exposes matching dual-axis values");
    passed &= check(hoveredChartImage != chartImage,
                    "history chart renders a hover cursor and value panel");
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(&chart, &leaveEvent);
    passed &= check(!chart.hoverTimeMilliseconds().has_value() && chart.hoverEntries().empty(),
                    "history chart clears hover guidance when the pointer leaves");

    chart.setSeries({});
    chart.setThemePalette(ThemePalette::lightConsole());
    QImage emptyChartImage(chart.size() * 2, QImage::Format_ARGB32_Premultiplied);
    emptyChartImage.setDevicePixelRatio(2.0);
    emptyChartImage.fill(Qt::transparent);
    chart.render(&emptyChartImage);
    passed &= check(imageHasVisiblePixels(emptyChartImage), "history chart renders its empty state at a high device-pixel ratio");

    HistoryLegendWidget legend;
    legend.resize(340, 72);
    legend.setEntries({
        { QStringLiteral("peak"), QStringLiteral("Peak"), QColor(QStringLiteral("#ff5068")), true, true },
        { QStringLiteral("integrated"), QStringLiteral("Integrated"), QColor(QStringLiteral("#4a9cff")), true, true }
    });
    int visibilityChangeCount = 0;
    QObject::connect(&legend, &HistoryLegendWidget::entryVisibilityChanged,
                     [&visibilityChangeCount](const QString&, bool) { ++visibilityChangeCount; });
    legend.show();
    QCoreApplication::processEvents();
    passed &= check(legend.setEntryVisible(QStringLiteral("peak"), false)
                        && !legend.entries().front().visible
                        && visibilityChangeCount == 1,
                    "history legend updates and reports a series visibility change");
    QImage legendImage(legend.size(), QImage::Format_ARGB32_Premultiplied);
    legendImage.fill(Qt::transparent);
    legend.render(&legendImage);
    passed &= check(imageHasVisiblePixels(legendImage), "history legend renders generic series entries");
    return passed;
}

bool testHistoryCurveDoesNotFillReferenceLine()
{
    using namespace WinJACKNexus::UI;
    const auto referenceColor = QColor(QStringLiteral("#3dde72"));

    HistoryChartWidget chart;
    chart.resize(480, 260);
    chart.setTimeRange(0, 100);
    chart.setValueRange(0.0, 100.0);
    chart.setReferenceLines({ { 50.0, QStringLiteral("Reference"), referenceColor, Qt::SolidLine, true } });
    chart.setSeries({
        {
            QStringLiteral("triangle"),
            QStringLiteral("Triangle"),
            QColor(QStringLiteral("#ff5068")),
            true,
            {
                { 0, 0.0 },
                { 50, 100.0 },
                { 100, 0.0 }
            }
        }
    });
    chart.show();
    QCoreApplication::processEvents();

    QImage image(chart.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    chart.render(&image);
    const QRect centerProbe(image.width() * 2 / 5,
                            image.height() * 2 / 5,
                            image.width() / 5,
                            image.height() / 5);
    return check(imageContainsColorInRect(image, centerProbe, referenceColor),
                 "open history curves preserve reference lines inside their visual bounds");
}

bool testMetricStatisticsWidget()
{
    using namespace WinJACKNexus::UI;
    MetricStatisticsWidget statistics;
    statistics.resize(500, 106);
    statistics.setPrecision(2);
    statistics.setEntries({
        {
            QStringLiteral("integrated"),
            QStringLiteral("Integrated"),
            QStringLiteral("LUFS"),
            std::optional<double> { -20.25 },
            std::optional<double> { -26.65 },
            std::optional<double> { -17.50 },
            QColor(QStringLiteral("#31c6a0"))
        },
        {
            QStringLiteral("range"),
            QStringLiteral("Range"),
            QStringLiteral("LU"),
            std::nullopt,
            std::optional<double> { 0.0 },
            std::optional<double> { 18.0 },
            QColor(QStringLiteral("#4a9cff"))
        },
        {
            QStringLiteral("peak"),
            QStringLiteral("True Peak"),
            QStringLiteral("dBTP"),
            std::optional<double> { 4.0 },
            std::optional<double> { -12.0 },
            std::optional<double> { 6.0 },
            QColor(QStringLiteral("#ff5068"))
        }
    });
    statistics.show();
    QCoreApplication::processEvents();

    bool passed = true;
    passed &= check(statistics.entries().size() == 3 && statistics.precision() == 2
                        && statistics.sizeHint().width() >= 166,
                    "metric statistics accepts optional current, minimum and maximum values");
    QImage statisticsImage(statistics.size(), QImage::Format_ARGB32_Premultiplied);
    statisticsImage.fill(Qt::transparent);
    statistics.render(&statisticsImage);
    const auto integratedStart = firstColorPixelX(statisticsImage, QColor(QStringLiteral("#31c6a0")));
    const auto rangeStart = firstColorPixelX(statisticsImage, QColor(QStringLiteral("#4a9cff")));
    const auto peakStart = firstColorPixelX(statisticsImage, QColor(QStringLiteral("#ff5068")));
    passed &= check(imageHasVisiblePixels(statisticsImage)
                        && integratedStart >= 0 && rangeStart - integratedStart >= 150
                        && peakStart - rangeStart >= 150,
                    "metric statistics distributes populated entries evenly across a wide row");

    statistics.setEntries({
        {
            QStringLiteral("invalid"),
            QStringLiteral("Invalid"),
            QStringLiteral("dB"),
            std::optional<double> { std::numeric_limits<double>::infinity() },
            std::nullopt,
            std::nullopt,
            QColor()
        }
    });
    passed &= check(!statistics.entries().front().currentValue.has_value(),
                    "metric statistics treats non-finite values as unavailable");

    statistics.setEntries({});
    statistics.setThemePalette(ThemePalette::lightConsole());
    QImage emptyStatisticsImage(statistics.size() * 2, QImage::Format_ARGB32_Premultiplied);
    emptyStatisticsImage.setDevicePixelRatio(2.0);
    emptyStatisticsImage.fill(Qt::transparent);
    statistics.render(&emptyStatisticsImage);
    passed &= check(imageHasVisiblePixels(emptyStatisticsImage),
                    "metric statistics renders an empty high-DPI state");
    return passed;
}

bool testDeEsserWidget()
{
    using WinJACKNexus::UI::DeEsserWidget;
    DeEsserWidget widget;
    bool passed = check(widget.size() == QSize(768, 650),
                        "De-Esser window reserves the reference panel and channel-strip proportions");
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    passed &= check(imageHasVisiblePixels(image), "De-Esser control surface paints a visible frame");
    passed &= check(image.save(QStringLiteral("build/deesser-ui-review.png")),
                    "De-Esser reference-review screenshot is written to the existing build directory");
    return passed;
}

bool testCompressorGraphFill()
{
    using WinJACKNexus::UI::CompressorWidget;
    CompressorWidget widget;
    bool passed = check(widget.size() == QSize(770, 665),
                        "Compressor window width matches the reference control panel and removes side gaps");
    std::array<WinJACKNexus::UI::CompressorMeter, 8> meters {};
    widget.setMeters(meters);
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);

    const auto hasAmberTint = [&image](int x, int y) {
        const QColor pixel = image.pixelColor(x, y);
        return pixel.red() > pixel.green() + 4 && pixel.green() > pixel.blue();
    };
    passed &= check(!hasAmberTint(293, 361),
                    "Compressor has no moving intensity fill when All has no active signal");
    meters[0] = {-6.02F, -10.5F, true};
    meters[1] = {-18.0F, -19.0F, true};
    meters[2] = {0.0F, 0.0F, false};
    widget.setMeters(meters);
    widget.render(&image);
    passed &= check(hasAmberTint(293, 361),
                    "All computes active-channel input power and fills below the compressed curve");
    passed &= check(!hasAmberTint(354, 361),
                    "All excludes inactive channels when setting the moving fill extent");
    passed &= check(image.save(QStringLiteral("build/compressor-ui-review.png")),
                    "Compressor graph review screenshot is written to the existing build directory");
    return passed;
}

bool testFxSettingsLoadsNativeFx()
{
    using namespace WinJACKNexus::UI;
    FxSettingsWindow window(QStringLiteral("Line 3"), { QStringLiteral("L") });
    auto* chain = window.findChild<QListWidget*>();
    auto* chainAdd = window.findChild<QPushButton*>(QStringLiteral("fxAddBottom"));
    auto* chainScroll = window.findChild<QScrollArea*>(QStringLiteral("fxChainScroll"));
    auto* chainPower = dynamic_cast<Controls::ToggleButton*>(
        window.findChild<QWidget*>(QStringLiteral("fxChainToggle")));
    auto* modulePower = window.findChild<QPushButton*>(QStringLiteral("fxModulePower"));
    bool passed = check(chainPower && !chainPower->isChecked(),
                        "FX chain uses an initially off toggle control");
    passed &= check(chain && chain->count() == 1,
                        "FX window loads a default Native Fx module");
    passed &= check(chain && chain->item(0)->text().contains(QStringLiteral("Parametric EQ")),
                    "default FX is Parametric EQ");
    passed &= check(chain && !chain->item(0)->data(Qt::UserRole + 3).toBool(),
                    "default Parametric EQ starts disabled");
    bool embedded = false;
    ParametricEqWidget* editor = nullptr;
    for (auto* child : window.findChildren<QWidget*>())
        if (auto* value = dynamic_cast<ParametricEqWidget*>(child)) { embedded = true; editor = value; }
    passed &= check(embedded, "FX window embeds the Native Fx control widget");
    auto* modulePreset = window.findChild<QComboBox*>(QStringLiteral("fxModulePreset"));
    passed &= check(modulePreset && modulePreset->count() == 6
                        && modulePreset->currentText() == QStringLiteral("Vocal Clear"),
                    "module preset menu mirrors Parametric EQ's own presets");
    if (modulePreset && editor) {
        modulePreset->setCurrentIndex(1);
        passed &= check(editor->hostPresetIndex() == 1,
                        "host preset selection changes the embedded plugin preset");
        editor->chooseHostPreset(2);
        QEventLoop loop;
        QTimer::singleShot(120, &loop, &QEventLoop::quit);
        loop.exec();
        passed &= check(modulePreset->currentIndex() == 2,
                        "plugin preset selection updates the host preset menu");
    }
    QPushButton* left = nullptr;
    QPushButton* right = nullptr;
    for (auto* choice : window.findChildren<QPushButton*>()) {
        if (choice->text() == QStringLiteral("L")) left = choice;
        if (choice->text() == QStringLiteral("R")) right = choice;
    }
    passed &= check(left && !right, "mono mixer strip shows only its available channel");
    FxLevelMeterWidget* meter = nullptr;
    for (auto* child : window.findChildren<QWidget*>())
        if (auto* value = dynamic_cast<FxLevelMeterWidget*>(child)) meter = value;
    passed &= check(meter != nullptr, "one meter panel stacks horizontal input and output bars");
    if (meter) {
        const auto bar = meter->barRect(0);
        passed &= check(std::abs(meter->positionForDb(-60) - bar.left()) < 0.1
                            && std::abs(meter->positionForDb(12) - bar.right()) < 0.1
                            && std::abs(meter->positionForDb(-12) - (bar.left() + bar.width() * 2.0 / 3.0)) < 0.1,
                        "meter ticks use the exact bar geometry and -60 to +12 scale");
    }
    window.show();
    QCoreApplication::processEvents();
    for (int reopen = 0; reopen < 3; ++reopen) {
        window.hide();
        window.show();
        QCoreApplication::processEvents();
    }
    const int initialAddY = chainAdd ? chainAdd->y() : -1;
    passed &= check(chain && chainAdd && chainScroll && chainAdd->parentWidget() == chain->parentWidget()
                        && chainAdd->height() == 58
                        && chainAdd->y() > chain->geometry().bottom(),
                    "dashed add FX button sits directly after the last chain item");
    auto* editorArea = window.findChild<QScrollArea*>(QStringLiteral("fxEditorArea"));
    passed &= check(editor && editorArea && !editor->isWindow()
                        && editor->parentWidget() == editorArea->widget()
                        && editorArea->widget()->parentWidget() == editorArea->viewport(),
                    "embedded Native Fx remains clipped to its viewport after reopening");
    QImage preview(window.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    window.render(&preview);
    passed &= check(preview.save(QStringLiteral("build/fx-ui-review.png")),
                    "FX window review image is written to the existing build directory");
    window.resize(1920, 1150);
    QCoreApplication::processEvents();
    QImage widePreview(window.size(), QImage::Format_ARGB32_Premultiplied);
    widePreview.fill(Qt::transparent);
    window.render(&widePreview);
    passed &= check(widePreview.save(QStringLiteral("build/fx-ui-review-wide.png")),
                    "reopened FX window review image is written at reference resolution");
    QPushButton* addModule = nullptr;
    for (auto* choice : window.findChildren<QPushButton*>())
        if (choice->text().contains(QStringLiteral("添加模块"))) { addModule = choice; break; }
    int availableNativeEffects = 0;
    if (addModule) {
        QTimer::singleShot(0, &window, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                availableNativeEffects = menu->actions().size();
                if (!menu->actions().isEmpty()) menu->actions().first()->trigger();
                menu->close();
            }
        });
        addModule->click();
    }
    passed &= check(availableNativeEffects == 8, "FX add menu discovers all eight Native Fx modules");
    passed &= check(chain && chain->count() == 2 && chain->item(1)->text() == QStringLiteral("Gate"),
                    "FX add menu loads another Native Fx module into the chain");
    QCoreApplication::processEvents();
    passed &= check(chainAdd && chainAdd->y() > initialAddY
                        && chainAdd->y() > chain->geometry().bottom(),
                    "add FX button follows the last item as the chain grows");
    int footerMenuEntries = 0;
    if (chainAdd) {
        QTimer::singleShot(0, &window, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                footerMenuEntries = menu->actions().size();
                menu->close();
            }
        });
        chainAdd->click();
    }
    passed &= check(footerMenuEntries == 8,
                    "footer add FX button opens the same module menu");
    bool gateEmbedded = false;
    if (editorArea && editorArea->widget())
        for (auto* child : editorArea->widget()->findChildren<QWidget*>())
            gateEmbedded |= dynamic_cast<GateEffectWidget*>(child) != nullptr;
    passed &= check(gateEmbedded && editorArea->widget()->parentWidget() == editorArea->viewport(),
                    "second Native Fx editor is a clipped child widget");
    if (chainPower && chain && modulePower) {
        chainPower->click();
        passed &= check(!chain->item(0)->data(Qt::UserRole + 3).toBool()
                            && chain->item(1)->data(Qt::UserRole + 3).toBool(),
                        "turning on the FX chain restores each module's own state");
        chainPower->click();
        passed &= check(!chain->item(0)->data(Qt::UserRole + 3).toBool()
                            && !chain->item(1)->data(Qt::UserRole + 3).toBool()
                            && !modulePower->isChecked() && !modulePower->isEnabled(),
                        "turning off the FX chain turns off every visible module toggle");
        chainPower->click();
        passed &= check(chain->item(1)->data(Qt::UserRole + 3).toBool()
                            && modulePower->isChecked() && modulePower->isEnabled(),
                        "re-enabling the chain restores enabled modules");
    }
    if (chain) {
        chain->setCurrentRow(0);
        QCoreApplication::processEvents();
        bool reused = false;
        if (editorArea && editorArea->widget())
            for (auto* child : editorArea->widget()->findChildren<QWidget*>())
                reused |= child == editor;
        passed &= check(reused && !editor->isWindow(),
                        "switching modules reuses the embedded Parametric EQ without a top-level window");
    }
    for (int nativeIndex = 1; nativeIndex < 8 && addModule; ++nativeIndex) {
        QTimer::singleShot(0, &window, [&, nativeIndex] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                if (menu->actions().size() > nativeIndex)
                    menu->actions().at(nativeIndex)->trigger();
                menu->close();
            }
        });
        addModule->click();
        passed &= check(editorArea && editorArea->widget()
                            && !editorArea->widget()->isWindow()
                            && editorArea->widget()->parentWidget() == editorArea->viewport(),
                        "every Native Fx module opens as a clipped child editor");
    }
    const auto useRowMenu = [&](int row, const std::function<void(QMenu*)>& choose) {
        if (!chain || row >= chain->count()) return false;
        const QRect rect = chain->visualItemRect(chain->item(row));
        const QPoint local(rect.right() - 20, rect.center().y());
        const QPoint global = chain->viewport()->mapToGlobal(local);
        bool opened = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                opened = true;
                choose(menu);
                menu->close();
            }
        });
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(chain->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(chain->viewport(), &release);
        return opened;
    };
    const int beforeRowActions = chain ? chain->count() : 0;
    const bool cloneMenu = useRowMenu(1, [&](QMenu* menu) {
        passed &= check(menu->actions().size() == 3
                            && menu->actions()[0]->text() == QStringLiteral("克隆")
                            && menu->actions()[1]->text() == QStringLiteral("删除")
                            && menu->actions()[2]->text() == QStringLiteral("替换"),
                        "row menu offers clone, delete and replace");
        menu->actions()[0]->trigger();
    });
    passed &= check(cloneMenu && chain->count() == beforeRowActions + 1
                        && chain->item(2)->text() == QStringLiteral("Gate")
                        && chain->item(2)->data(Qt::UserRole + 3) == chain->item(1)->data(Qt::UserRole + 3),
                    "clone inserts the same effect and bypass state after its source");
    const bool replaceMenu = useRowMenu(2, [&](QMenu* menu) {
        auto* replacements = menu->actions()[2]->menu();
        passed &= check(replacements && replacements->actions().size() == 8,
                        "replace menu discovers all Native Fx modules");
        if (replacements) replacements->actions()[3]->trigger();
    });
    passed &= check(replaceMenu && chain->count() == beforeRowActions + 1
                        && chain->item(2)->text() == QStringLiteral("Compressor"),
                    "replace keeps the selected chain position");
    const bool deleteMenu = useRowMenu(2, [&](QMenu* menu) { menu->actions()[1]->trigger(); });
    passed &= check(deleteMenu && chain->count() == beforeRowActions
                        && chain->item(1)->text() == QStringLiteral("Gate"),
                    "delete removes only the chosen effect");
    window.resize(1100, 720);
    QCoreApplication::processEvents();
    if (chainScroll && chainAdd) {
        auto* scrollBar = chainScroll->verticalScrollBar();
        passed &= check(scrollBar->maximum() > 0,
                        "FX chain scrolls when the effect list exceeds the sidebar height");
        scrollBar->setValue(scrollBar->maximum());
        QCoreApplication::processEvents();
        const int buttonBottom = chainAdd->mapTo(chainScroll->viewport(),
                                                  QPoint(0, chainAdd->height())).y();
        passed &= check(buttonBottom > 0 && buttonBottom <= chainScroll->viewport()->height(),
                        "add FX button stays after the last effect at the end of the scroll area");
    }
    FxSettingsWindow stereo(QStringLiteral("Main L/R"), { QStringLiteral("L"), QStringLiteral("R") });
    QPushButton* stereoRight = nullptr;
    for (auto* choice : stereo.findChildren<QPushButton*>())
        if (choice->text() == QStringLiteral("R")) stereoRight = choice;
    passed &= check(stereoRight != nullptr, "stereo mixer strip shows both available channels");
    if (stereoRight) stereoRight->click();
    for (auto* child : stereo.findChildren<QWidget*>())
        if (auto* value = dynamic_cast<ParametricEqWidget*>(child))
            passed &= check(value->selectedChannel() == 1,
                            "chain channel selection reaches the embedded Native Fx editor");
    return passed;
}

bool testMixerChannelControls()
{
    using namespace WinJACKNexus::UI;
    MixerWindow mixer;
    mixer.show();
    QCoreApplication::processEvents();
    auto* bar = mixer.findChild<QMenuBar*>(QStringLiteral("mixerMenuBar"));
    bool passed = check(bar && bar->actions().size() == 3,
                        "mixer exposes channel management, global and large-screen actions");
    if (!bar) return false;
    passed &= check(bar == mixer.menuBar() && bar->isVisible() && bar->geometry().height() > 0,
                    "mixer uses the same QMainWindow system menu bar as Patchbay");
    auto* channelMenu = bar->actions()[0]->menu();
    auto* globalMenu = bar->actions()[1]->menu();
    passed &= check(channelMenu && channelMenu->actions().size() == 2
                        && channelMenu->actions()[0]->menu()->actions().size() == 4
                        && channelMenu->actions()[1]->menu()->actions().size() == 4,
                    "input and AUX menus offer all four channel layouts");
    passed &= check(globalMenu && globalMenu->actions().size() == 1,
                    "global menu offers one action to clear solo and mute");

    const auto click = [](QWidget* widget, QPoint point) {
        const auto global = widget->mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), QPointF(global),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &release);
    };
    const auto openContext = [&](const QString& actionText, QPoint point = {50, 90}) {
        bool found = false;
        const auto global = mixer.mapToGlobal(point);
        QTimer::singleShot(0, &mixer, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                for (auto* action : menu->actions())
                    if (action->text() == actionText) { found = action->isEnabled(); action->trigger(); break; }
                menu->close();
            }
        });
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&mixer, &press);
        return found;
    };
    passed &= check(openContext(QStringLiteral("打开FX设置")),
                    "channel context menu opens FX settings");
    FxSettingsWindow* fx = nullptr;
    for (auto* child : mixer.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<FxSettingsWindow*>(child)) { fx = candidate; break; }
    auto* fxToggle = fx ? dynamic_cast<Controls::ToggleButton*>(
        fx->findChild<QWidget*>(QStringLiteral("fxChainToggle"))) : nullptr;
    passed &= check(fxToggle && !fxToggle->isChecked(), "FX window reads the mixer channel switch");
    click(&mixer, {25, 375});
    passed &= check(fxToggle && fxToggle->isChecked(), "mixer FX switch updates the FX window");
    if (fxToggle) fxToggle->click();
    click(&mixer, {25, 375});
    passed &= check(fxToggle && fxToggle->isChecked(), "FX window switch updates the mixer channel");

    passed &= check(openContext(QStringLiteral("打开AUX设置")),
                    "channel context menu opens AUX settings");
    AuxSettingsWindow* aux = nullptr;
    for (auto* child : mixer.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<AuxSettingsWindow*>(child)) { aux = candidate; break; }
    passed &= check(aux && !aux->chainEnabled(), "AUX window reads the mixer channel switch");
    click(&mixer, {25, 343});
    passed &= check(aux && aux->chainEnabled(), "mixer AUX switch updates the AUX window");
    if (aux) click(aux, {222, 30});
    click(&mixer, {25, 343});
    passed &= check(aux && aux->chainEnabled(), "AUX window switch updates the mixer channel");
    if (aux) {
        QImage auxPreview(aux->size(), QImage::Format_ARGB32_Premultiplied);
        auxPreview.fill(Qt::transparent);
        aux->render(&auxPreview);
        passed &= check(auxPreview.save(QStringLiteral("build/aux-ui-review.png")),
                        "AUX window review image is written to the existing build directory");
    }

    auto auxState = std::make_shared<WinJACKNexus::Common::AuxMixerState>();
    AuxSettingsWindow auxControls(auxState);
    const auto sourceKey = QStringLiteral("ui-test-input");
    auxControls.setSource(sourceKey, QStringLiteral("Line 3"), 0,
                          WinJACKNexus::Common::AuxChannelLayout::mono);
    auxControls.setChainEnabled(true);
    auxControls.show();
    QCoreApplication::processEvents();
    auto* firstBus = auxState->busForSource(sourceKey, 1);
    passed &= check(firstBus && firstBus->enabled, "first AUX send starts enabled");
    auxState->setBusAccent(1, QColor("#a12bcd"));
    passed &= check(auxState->bus(1)->accent == QColor("#a12bcd")
                        && firstBus && firstBus->accent == QColor("#a12bcd"),
                    "AUX channel color reaches the source-specific AUX list state");
    click(&auxControls, {48, 98});
    passed &= check(firstBus && !firstBus->enabled, "AUX row toggle disables its send");
    click(&auxControls, {48, 98});
    passed &= check(firstBus && firstBus->enabled, "AUX row toggle enables its send");
    const auto targetLayout = firstBus ? firstBus->targetLayout
                                       : WinJACKNexus::Common::AuxChannelLayout::stereo;
    int auxChanges = 0;
    auxControls.setStateChangedCallback([&] { ++auxChanges; });
    click(&auxControls, {1090, 122});
    passed &= check(firstBus && firstBus->targetLayout == targetLayout && auxChanges == 0
                        && QApplication::activePopupWidget() == nullptr,
                    "AUX target layout is read-only text");
    auxControls.setChainEnabled(false);
    click(&auxControls, {48, 98});
    passed &= check(firstBus && firstBus->enabled,
                    "master AUX switch prevents editing a disabled row toggle");
    const auto chooseAuxMenu = [&](QPoint point, const std::function<void(QMenu*)>& choose) {
        bool opened = false;
        QTimer::singleShot(0, &auxControls, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                opened = true;
                choose(menu);
                menu->close();
            }
        });
        click(&auxControls, point);
        return opened;
    };
    passed &= check(chooseAuxMenu({290, 30}, [](QMenu* menu) { menu->actions()[3]->trigger(); })
                        && auxState->buses().size() == 4
                        && auxState->buses().back().targetLayout
                               == WinJACKNexus::Common::AuxChannelLayout::surround71,
                    "AUX add button offers channel types and creates the selected type");
    passed &= check(chooseAuxMenu({345, 98}, [](QMenu* menu) { menu->actions()[0]->trigger(); })
                        && auxState->buses().size() == 5,
                    "AUX row gear clones a bus");
    passed &= check(chooseAuxMenu({345, 98}, [](QMenu* menu) {
                        menu->actions()[2]->menu()->actions()[0]->trigger();
                    }) && auxState->bus(1)->targetLayout == WinJACKNexus::Common::AuxChannelLayout::mono
                        && auxState->busForSource(sourceKey, 1)->targetLayout
                               == WinJACKNexus::Common::AuxChannelLayout::mono,
                    "AUX row gear changes the channel type for every source");
    passed &= check(chooseAuxMenu({345, 414}, [](QMenu* menu) { menu->actions()[1]->trigger(); })
                        && auxState->buses().size() == 4,
                    "AUX row gear deletes the selected bus");

    bar->actions()[2]->trigger();
    auto* selected = mixer.findChild<QLabel*>(QStringLiteral("selSelectedChannel"));
    passed &= check(selected && selected->text() == QStringLiteral("Mic 1"),
                    "large screen starts on the selected channel");
    auto* sel = mixer.findChild<QWidget*>(QStringLiteral("selLargeScreen"));
    auto* mainPage = mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
    auto* pages = mixer.findChild<QStackedWidget*>(QStringLiteral("selPages"));
    auto* channelName = mixer.findChild<QLineEdit*>(QStringLiteral("selChannelName"));
    auto* channelComment = mixer.findChild<QLineEdit*>(QStringLiteral("selChannelComment"));
    passed &= check(sel && mainPage && pages && pages->count() == 3 && channelName && channelComment,
                    "SEL contains the Main, AUX and FX pages and editable channel details");
    if (sel && mainPage) {
        QImage selPreview(sel->size(), QImage::Format_ARGB32_Premultiplied);
        selPreview.fill(Qt::transparent);
        sel->render(&selPreview);
        passed &= check(selPreview.save(QStringLiteral("build/sel-ui-review.png")),
                        "SEL review image is written to the existing build directory");
        const auto mainPoint=[mainPage](int x,int y){
            return QPoint(qRound(x*mainPage->width()/1394.0),qRound(y*mainPage->height()/813.0));
        };
        click(mainPage,mainPoint(1229,494)); // Split all send faders.
        click(mainPage,mainPoint(457,601));
        const auto splitLevels=mainPage->property("selSendLevels").toList();
        passed &= check(splitLevels.size()==6 && std::abs(splitLevels[0].toDouble()+12.0)<1.0
                            && std::abs(splitLevels[1].toDouble()+2.0)<.1,
                        "split SEL faders keep independent channel levels");
        click(mainPage,mainPoint(1229,494)); // Link all channels.
        click(mainPage,mainPoint(457,611));
        const auto linkedLevels=mainPage->property("selSendLevels").toList();
        passed &= check(linkedLevels.size()==6 && std::abs(linkedLevels[0].toDouble()+18.0)<1.0
                            && std::abs(linkedLevels[1].toDouble()+8.0)<1.0,
                        "linked SEL faders shift every channel by the same dB delta");
        click(mainPage,mainPoint(1310,494));
    }
    if (channelComment) {
        channelComment->setText(QStringLiteral("Lead vocal"));
        QMetaObject::invokeMethod(channelComment, "editingFinished", Qt::DirectConnection);
        passed &= check(channelComment->text() == QStringLiteral("Lead vocal"),
                        "SEL channel comment can be edited");
    }
    if (channelName) {
        channelName->setText(QStringLiteral("Mic One"));
        QMetaObject::invokeMethod(channelName, "editingFinished", Qt::DirectConnection);
        passed &= check(selected && selected->text() == QStringLiteral("Mic One"),
                        "SEL name edit updates the selected mixer channel");
        channelName->setText(QStringLiteral("Mic 1"));
        QMetaObject::invokeMethod(channelName, "editingFinished", Qt::DirectConnection);
    }
    bool customColorDialogOpened = false;
    QTimer::singleShot(0, &mixer, [&] {
        if (auto* dialog = dynamic_cast<QColorDialog*>(QApplication::activeModalWidget())) {
            customColorDialogOpened = true;
            dialog->setCurrentColor(QColor("#123abc"));
            dialog->accept();
        }
    });
    if (auto* color = mixer.findChild<QPushButton*>(QStringLiteral("selCustomColor"))) color->click();
    passed &= check(customColorDialogOpened, "SEL custom color button opens the editable Qt color wheel dialog");
    auto* selHeader = mixer.findChild<QWidget*>(QStringLiteral("selHeader"));
    passed &= check(selHeader && selHeader->styleSheet().contains(QStringLiteral("#123abc")),
                    "custom channel color updates the SEL title background");
    QTimer::singleShot(0, &mixer, [&] {
        if (auto* dialog = dynamic_cast<QColorDialog*>(QApplication::activeModalWidget())) {
            dialog->setCurrentColor(QColor("#ffdd55"));
            dialog->accept();
        }
    });
    if (auto* color = mixer.findChild<QPushButton*>(QStringLiteral("selCustomColor"))) color->click();
    passed &= check(selHeader && selHeader->styleSheet().contains(QStringLiteral("#ffdd55"))
                        && selHeader->styleSheet().contains(QStringLiteral("#102033")),
                    "light custom title color selects dark readable title text");
    if (auto* auxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"))) {
        auxTab->click();
        passed &= check(pages && pages->currentIndex() == 1 && aux && aux->parentWidget() == pages->widget(1),
                        "SEL AUX tab embeds the existing AUX settings interface");
    }
    if (auto* fxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabFX"))) {
        fxTab->click();
        bool embeddedFx = false;
        if (pages) for (auto* child : pages->widget(2)->findChildren<QWidget*>())
            if (dynamic_cast<FxSettingsWindow*>(child)) embeddedFx = true;
        passed &= check(pages && pages->currentIndex() == 2 && embeddedFx,
                        "SEL FX tab embeds the existing FX settings interface");
    }
    if (auto* mainTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabMain"))) mainTab->click();
    click(&mixer, {963, 422});
    passed &= check(selected && selected->text() == QStringLiteral("Main L/R"),
                    "Main strip opens its own SEL channel");
    auto* auxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"));
    passed &= check(auxTab && !auxTab->isVisible(), "Main SEL has only Main and FX tabs");
    if(auto* fxTab=mixer.findChild<QPushButton*>(QStringLiteral("selTabFX"))){
        fxTab->click();
        FxSettingsWindow* masterFx=nullptr;
        if(pages)for(auto* child:pages->widget(2)->findChildren<QWidget*>())
            if(auto* candidate=dynamic_cast<FxSettingsWindow*>(child))masterFx=candidate;
        auto* masterList=masterFx?masterFx->findChild<QListWidget*>():nullptr;
        passed &= check(masterList&&masterList->count()==3,
                        "Main FX chain includes parametric EQ, compressor and limiter by default");
    }
    if(auto* mainTab=mixer.findChild<QPushButton*>(QStringLiteral("selTabMain")))mainTab->click();
    click(&mixer, {60, 422});
    click(&mixer, {150, 420});
    passed &= check(selected && selected->text() == QStringLiteral("Mic 2"),
                    "SEL button switches the large-screen channel");
    channelMenu->actions()[0]->menu()->actions()[3]->trigger();
    channelMenu->actions()[1]->menu()->actions()[2]->trigger();
    passed &= check(openContext(QStringLiteral("打开SEL设置")),
                    "channel context menu has an active SEL entry");
    passed &= check(openContext(QStringLiteral("复制通道")),
                    "channel context menu copies an input strip");
    click(&mixer, {150, 420});
    passed &= check(selected && selected->text() == QStringLiteral("Mic 1 副本"),
                    "copied strip keeps its channel identity and SEL selection");
    passed &= check(openContext(QStringLiteral("打开FX设置"), {150, 90}),
                    "copied strip opens its own FX window");
    FxSettingsWindow* copiedFx = nullptr;
    for (auto* child : mixer.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<FxSettingsWindow*>(child);
            candidate && candidate->windowTitle().contains(QStringLiteral("副本"))) {
            copiedFx = candidate;
            break;
        }
    auto* copiedToggle = copiedFx ? dynamic_cast<Controls::ToggleButton*>(
        copiedFx->findChild<QWidget*>(QStringLiteral("fxChainToggle"))) : nullptr;
    auto* copiedList = copiedFx ? copiedFx->findChild<QListWidget*>() : nullptr;
    passed &= check(copiedToggle && copiedToggle->isChecked() && copiedList && copiedList->count() == 1,
                    "copy preserves the FX chain and total switch");
    passed &= check(openContext(QStringLiteral("删除通道"), {150, 90}),
                    "channel context menu removes the copied strip");
    passed &= check(selected && selected->text() == QStringLiteral("Mic 1"),
                    "deleting the selected strip moves SEL back to an existing channel");
    bool channelTypesFound = false;
    QTimer::singleShot(0, &mixer, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            auto* types = menu->actions()[2]->menu();
            channelTypesFound = types && types->actions().size() == 4
                && types->actions()[1]->isChecked();
            if (channelTypesFound)
                types->actions()[2]->trigger();
            menu->close();
        }
    });
    {
        const QPoint point {50, 90};
        const auto global = mixer.mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&mixer, &press);
    }
    passed &= check(channelTypesFound, "channel context menu lists four types and defaults to stereo");
    bool surroundSelected = false;
    QTimer::singleShot(0, &mixer, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            auto* types = menu->actions()[2]->menu();
            surroundSelected = types && types->actions()[2]->isChecked();
            menu->close();
        }
    });
    {
        const QPoint point {50, 90};
        const auto global = mixer.mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&mixer, &press);
    }
    passed &= check(surroundSelected, "channel type selection updates the input strip");
    bool fxHasCenterChannel = false;
    if (fx) {
        for (auto* choice : fx->findChildren<QPushButton*>())
            if (choice->text() == QStringLiteral("C")) fxHasCenterChannel = true;
    }
    passed &= check(fx && fxHasCenterChannel && fx->findChild<QListWidget*>()->count() == 1,
                    "changing channel type updates the open FX editor without losing its chain");
    QImage preview(mixer.size(), QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    mixer.render(&preview);
    passed &= check(preview.save(QStringLiteral("build/mixer-ui-review.png")),
                    "mixer review image is written to the existing build directory");
    for (int remainingInputs = 9; remainingInputs > 1; --remainingInputs)
        passed &= check(openContext(QStringLiteral("删除通道")),
                        "input strip can be removed while another input remains");
    passed &= check(!openContext(QStringLiteral("删除通道")),
                    "the last input strip cannot be removed");
    return passed;
}

bool testFxMeterSegments()
{
    using WinJACKNexus::UI::FxLevelMeterWidget;
    FxLevelMeterWidget meter;
    meter.resize(320, 86);
    meter.setLevels(12, -6);
    QImage image(meter.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    meter.render(&image);
    const auto colorAt = [&image, &meter](double db, int row) {
        return image.pixelColor(qRound(meter.positionForDb(db)), qRound(meter.barRect(row).center().y()));
    };
    const QColor green = colorAt(-30, 0);
    const QColor yellow = colorAt(-6, 0);
    const QColor red = colorAt(6, 0);
    bool passed = check(green.green() > green.red() * 2 && green.green() > green.blue(),
                        "low meter zone is solid green");
    passed &= check(yellow.red() > yellow.blue() * 2 && yellow.green() > yellow.blue() * 2,
                    "middle meter zone is solid yellow");
    passed &= check(red.red() > red.green() * 2 && red.red() > red.blue() * 2,
                    "high meter zone is solid red");
    return passed;
}

bool testReusableFxControls()
{
    using namespace WinJACKNexus::UI::Controls;
    ControlRegistry registry;
    QWidget owner;
    bool passed = check(registry.create(QStringLiteral("button"), &owner) != nullptr
                        && registry.create(QStringLiteral("dropdown"), &owner) != nullptr
                        && registry.create(QStringLiteral("slider"), &owner) != nullptr
                        && registry.create(QStringLiteral("toggle"), &owner) != nullptr,
                        "shared control registry creates built-in widgets");
    passed &= check(registry.registerFactory(QStringLiteral("plugin.scope"), [](QWidget* parent) {
                        auto* widget = new QWidget(parent);
                        widget->setObjectName(QStringLiteral("pluginScope"));
                        return widget;
                    }), "plugin can register a custom widget factory");
    auto* custom = registry.create(QStringLiteral("plugin.scope"), &owner);
    passed &= check(custom && custom->objectName() == QStringLiteral("pluginScope")
                        && custom->parentWidget() == &owner,
                    "custom control is created under plugin-selected parent");
    QImage surface(480, 320, QImage::Format_ARGB32_Premultiplied);
    surface.fill(Qt::transparent);
    QPainter painter(&surface);
    Palette colors;
    colors.accent = QColor("#e560c0");
    drawButton(painter, {10,10,100,35}, QStringLiteral("Button"), true, colors);
    drawDropdown(painter, {120,10,150,35}, QStringLiteral("Preset"), colors);
    drawKnob(painter, {55,105}, .6, 28, colors);
    drawSlider(painter, {115,65,15,105}, .4, true, colors);
    drawToggle(painter, {155,75,55,30}, true, colors.accent, colors.border);
    drawChart(painter, {235,65,220,120}, 4, 3, colors);
    drawLevelMeter(painter, {20,205,220,20}, -6);
    drawCardLabel(painter, {255,205,200,55}, QStringLiteral("Graphic EQ"), QStringLiteral("▥"), colors);
    painter.end();
    passed &= check(imageHasVisiblePixels(surface), "shared controls paint with custom accent");
    return passed;
}

}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    bool passed = true;
    passed &= testThemePalette();
    passed &= testApplicationSettings();
    passed &= testThemeManager(application);
    passed &= testPaintableWidget();
    passed &= testAnimatedWidget();
    passed &= testNativeWindowUtils();
    passed &= testIcons();
    passed &= testMeterLevelBar();
    passed &= testMeterArrayFrameRate();
    passed &= testMetricReadout();
    passed &= testHistoryChartAndLegend();
    passed &= testHistoryCurveDoesNotFillReferenceLine();
    passed &= testMetricStatisticsWidget();
    passed &= testCompressorGraphFill();
    passed &= testDeEsserWidget();
    passed &= testFxSettingsLoadsNativeFx();
    passed &= testMixerChannelControls();
    passed &= testFxMeterSegments();
    passed &= testReusableFxControls();
    if (!passed)
        return 1;
    std::puts("UI tests passed");
    return 0;
}
