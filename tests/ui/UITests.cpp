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
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>
#include <WinJACKNexus/UI/IconManager.hpp>
#include <WinJACKNexus/UI/MeterLevelBar.hpp>
#include <WinJACKNexus/UI/MeterWidget.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>
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

#include <cmath>
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

bool testMeterLevelBar()
{
    using namespace WinJACKNexus::UI;
    MeterLevelBar bar;
    bar.resize(96, 280);
    bar.setRange(-70.0F, 12.0F);
    bar.setThresholds(-18.0F, 0.0F);
    bar.setMarks({
        { -70.0F, QStringLiteral("-70") },
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
    passed &= check(bar.value().has_value() && std::abs(*bar.value() + 70.0F) < 0.01F,
                    "meter level bar clamps values below its range");
    bar.setValue(99.0F);
    passed &= check(bar.value().has_value() && std::abs(*bar.value() - 12.0F) < 0.01F,
                    "meter level bar clamps values above its range");

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
    chart.setValueRange(-70.0, 12.0);
    chart.setVerticalAxisLabel(QStringLiteral("dBFS"));
    chart.setHorizontalAxisLabel(QStringLiteral("Elapsed"));
    chart.setReferenceLines({
        { -18.0, QStringLiteral("Target"), QColor(QStringLiteral("#31c6a0")), Qt::DashLine, true }
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
        }
    });
    chart.show();
    QCoreApplication::processEvents();

    bool passed = true;
    passed &= check(chart.series().size() == 2 && chart.referenceLines().size() == 1
                        && chart.sizeHint().width() >= 320,
                    "history chart accepts generic time series and reference lines");
    QImage chartImage(chart.size(), QImage::Format_ARGB32_Premultiplied);
    chartImage.fill(Qt::transparent);
    chart.render(&chartImage);
    passed &= check(imageHasVisiblePixels(chartImage), "history chart renders populated series with a discontinuity");

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
    passed &= check(imageHasVisiblePixels(statisticsImage), "metric statistics renders populated entries");

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
    passed &= testMeterLevelBar();
    passed &= testMetricReadout();
    passed &= testHistoryChartAndLegend();
    passed &= testMetricStatisticsWidget();
    if (!passed)
        return 1;
    std::puts("UI tests passed");
    return 0;
}