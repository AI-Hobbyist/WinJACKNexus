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
#include <WinJACKNexus/UI/MixerPolarPlot.hpp>
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
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
#include <QTabBar>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QStackedWidget>
#include <QPainter>
#include <QPainterPath>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMainWindow>
#include <QComboBox>
#include <QCompleter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
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
#include <string_view>

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
    int paintCount = 0;
    void advanceForTest() { advanceFrame(); }

protected:
    void paintContent(QPainter&) override
    {
        ++paintCount;
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
    const auto originalPeakRefresh = settings.peakValueRefreshMilliseconds();
    const auto originalPeakHold = settings.peakHoldSeconds();
    const auto selectedFrameRate = originalFrameRate == 120 ? 30 : 120;
    int signalCount = 0;
    QObject timerOwner;
    QTimer refreshTimer;
    WinJACKNexus::UI::bindRefreshTimer(refreshTimer, &timerOwner);
    bool timerFollowsSetting = refreshTimer.interval() == 1000 / originalFrameRate;
    QObject::connect(&settings, &ApplicationSettings::frameRateChanged, &timerOwner,
                     [&signalCount](int) { ++signalCount; });

    settings.setFrameRate(selectedFrameRate);
    timerFollowsSetting &= refreshTimer.interval() == 1000 / selectedFrameRate;
    settings.setFrameRate(47);
    timerFollowsSetting &= refreshTimer.interval() == 1000 / 60;
    const auto passed = check(settings.frameRate() == 60 && signalCount == 2,
                              "application settings accept 30, 60 and 120 FPS while normalizing other values to 60 FPS")
        && check(timerFollowsSetting && refreshTimer.timerType() == Qt::PreciseTimer,
                 "refresh timer tracks application FPS changes");
    settings.setPeakValueRefreshMilliseconds(75);
    settings.setPeakHoldSeconds(2.5);
    const auto peakSettingsPassed = check(settings.peakValueRefreshMilliseconds() == 75
                                          && settings.peakHoldSeconds() == 2.5,
                                          "global peak value refresh and hold settings can be changed");
    settings.setPeakValueRefreshMilliseconds(originalPeakRefresh);
    settings.setPeakHoldSeconds(originalPeakHold);
    settings.setFrameRate(originalFrameRate);
    return passed && peakSettingsPassed;
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
    auto& settings = WinJACKNexus::UI::ApplicationSettings::instance();
    const auto originalFrameRate = settings.frameRate();
    ProbeAnimated widget;
    widget.resize(120, 80);
    widget.setFrameRate(120);
    const bool globalRateSelected = settings.frameRate() == 120 && widget.frameRate() == 120;
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
    passed &= check(globalRateSelected, "animated widget frame-rate setter updates the application frame rate");
    settings.setFrameRate(originalFrameRate);
    return passed;
}

bool testSettingsControlThemePreview()
{
    using namespace WinJACKNexus::UI;
    QWidget preview;
    preview.resize(360, 470);
    Controls::TextField textField(&preview);
    textField.setGeometry(20, 20, 320, 30);
    textField.setText(QStringLiteral("Settings text"));
    Controls::SearchField searchField(&preview);
    searchField.setGeometry(20, 60, 320, 30);
    searchField.setPlaceholderText(QStringLiteral("Search modules"));
    Controls::ChoiceField choiceField(&preview);
    choiceField.setGeometry(20, 100, 320, 30);
    choiceField.addItems({QStringLiteral("Default"), QStringLiteral("Custom")});
    Controls::IntegerField integerField(&preview);
    integerField.setGeometry(20, 140, 150, 30);
    integerField.setRange(30, 120);
    integerField.setValue(60);
    Controls::DecimalField decimalField(&preview);
    decimalField.setGeometry(190, 140, 150, 30);
    decimalField.setValue(1.5);
    Controls::ActionButton actionButton(QStringLiteral("Apply"), &preview);
    actionButton.setGeometry(20, 185, 320, 34);
    Controls::ToggleField toggleField(QStringLiteral("Enable effect"), &preview);
    toggleField.setGeometry(20, 230, 150, 28);
    toggleField.setChecked(true);
    Controls::RangeSlider rangeSlider(Qt::Horizontal, &preview);
    rangeSlider.setGeometry(190, 230, 150, 28);
    rangeSlider.setRange(0, 100);
    rangeSlider.setValue(60);
    Controls::IconActionButton iconButton(&preview);
    iconButton.setGeometry(20, 275, 320, 38);
    iconButton.setIcon(IconManager::icon(QStringLiteral("plus")));
    iconButton.setText(QStringLiteral("Add module"));
    iconButton.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    Controls::DragTreeWidget tree(&preview);
    tree.setGeometry(20, 330, 320, 120);
    tree.setHeaderHidden(true);
    new QTreeWidgetItem(&tree, {QStringLiteral("Native Fx")});
    ThemeManager manager;
    bool passed = true;
    passed &= check(dynamic_cast<Controls::ScrollBarField*>(tree.verticalScrollBar())
                        && dynamic_cast<Controls::ScrollBarField*>(tree.horizontalScrollBar()),
                    "node library tree uses themed scrollbars");
    passed &= check(!choiceField.manualFilterEnabled() && !choiceField.isEditable(),
                    "dropdown manual filtering is disabled by default");
    choiceField.setManualFilterEnabled(true);
    passed &= check(choiceField.isEditable() && choiceField.completer()
                    && choiceField.completer()->filterMode() == Qt::MatchContains
                    && choiceField.completer()->caseSensitivity() == Qt::CaseInsensitive,
                    "dropdown can filter choices by manually entered text");
    choiceField.completer()->setCompletionPrefix(QStringLiteral("ustom"));
    passed &= check(choiceField.completer()->completionCount() == 1
                    && choiceField.completer()->currentCompletion() == QStringLiteral("Custom"),
                    "dropdown manual filtering matches a substring");
    choiceField.setManualFilterEnabled(false);
    passed &= check(toggleField.property("wjnRole").toString() == QStringLiteral("toggle")
                    && rangeSlider.property("wjnRole").toString() == QStringLiteral("range"),
                    "shared toggle and slider carry theme roles");
    for (const auto& theme : {QStringLiteral("dark-rack"), QStringLiteral("light-console")}) {
        passed &= check(manager.setTheme(theme) && manager.applyToWidget(&preview),
                        "shared settings control theme can be applied");
        preview.show();
        QCoreApplication::processEvents();
        passed &= check(toggleField.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent"))
                        && rangeSlider.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent"))
                        && choiceField.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent"))
                        && integerField.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent"))
                        && decimalField.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent"))
                        && actionButton.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent"))
                        && iconButton.property("accentColor").value<QColor>()
                            == manager.palette().color(QStringLiteral("accent")),
                        "self-drawn settings controls receive the active theme colors");
        QImage image(preview.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        preview.render(&image);
        passed &= check(image.save(QStringLiteral("build/shared-settings-%1.png").arg(theme)),
                        "shared settings control theme preview is written to existing build directory");
    }
    QWidget choicePreview;
    choicePreview.resize(370, 88);
    Controls::ChoiceField editable(&choicePreview);
    editable.setGeometry(8, 8, 354, 32);
    editable.setEditable(true);
    editable.addItem(QStringLiteral("Speaker 2 - External Audio Device"));
    editable.setEditText(QStringLiteral("Speaker 2 - External Audio Device"));
    Controls::ChoiceField filtered(&choicePreview);
    filtered.setGeometry(8, 48, 354, 32);
    filtered.addItem(QStringLiteral("Speaker 2 - External Audio Device"));
    filtered.setManualFilterEnabled(true);
    filtered.setEditText(QStringLiteral("External"));
    manager.setTheme(QStringLiteral("dark-rack"));
    manager.applyToWidget(&choicePreview);
    choicePreview.show();
    QCoreApplication::processEvents();
    QImage choiceImage(choicePreview.size(), QImage::Format_ARGB32_Premultiplied);
    choicePreview.render(&choiceImage);
    passed &= check(choiceImage.save(QStringLiteral("build/shared-editable-choices.png")),
                    "editable and filtered dropdown preview is written to the existing build directory");
    return passed;
}

bool testSharedContainerControls()
{
    using namespace WinJACKNexus::UI::Controls;
    bool passed = true;
    DialogButtonBar actions(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    int accepted = 0;
    int rejected = 0;
    QObject::connect(&actions, &QDialogButtonBox::accepted, &actions,
                     [&accepted] { ++accepted; });
    QObject::connect(&actions, &QDialogButtonBox::rejected, &actions,
                     [&rejected] { ++rejected; });
    for (auto* button : actions.buttons()) {
        passed &= check(qobject_cast<ActionButton*>(button) != nullptr,
                        "dialog action uses a shared self-drawn button");
        button->click();
    }
    passed &= check(accepted == 1 && rejected == 1,
                    "shared dialog actions preserve accept and reject signals");
    TabField tabs;
    tabs.resize(320, 180);
    tabs.addTab(new QWidget(&tabs), QStringLiteral("Main"));
    tabs.setTabsClosable(true);
    tabs.addTab(new QWidget(&tabs), QStringLiteral("AUX"));
    tabs.show();
    QCoreApplication::processEvents();
    int closedIndex = -1;
    QObject::connect(&tabs, &QTabWidget::tabCloseRequested, &tabs,
                     [&closedIndex](int index) { closedIndex = index; });
    auto* close = qobject_cast<IconActionButton*>(
        tabs.tabBar()->tabButton(0, QTabBar::RightSide));
    passed &= check(close != nullptr, "tab close control comes from shared UI API");
    if (close) close->click();
    passed &= check(closedIndex == 0, "shared tab close control preserves close signal");

    LogField log;
    log.resize(320, 90);
    log.setReadOnly(true);
    log.show();
    QCoreApplication::processEvents();
    QImage before(log.size(), QImage::Format_ARGB32_Premultiplied);
    before.fill(Qt::transparent);
    log.render(&before);
    log.appendPlainText(QStringLiteral("Scanner log entry"));
    QCoreApplication::processEvents();
    QImage after(log.size(), QImage::Format_ARGB32_Premultiplied);
    after.fill(Qt::transparent);
    log.render(&after);
    passed &= check(before != after, "shared log renders newly appended text");

    QMainWindow main;
    main.resize(980, 480);
    main.setCentralWidget(new QWidget(&main));
    auto* left = new DockPanel(QStringLiteral("Nodes"), Qt::LeftDockWidgetArea, &main);
    left->setWidget(new QWidget(left));
    main.addDockWidget(Qt::LeftDockWidgetArea, left);
    auto* right = new DockPanel(QStringLiteral("Properties"), Qt::RightDockWidgetArea, &main);
    right->setWidget(new QWidget(right));
    main.addDockWidget(Qt::RightDockWidgetArea, right);
    WinJACKNexus::UI::ThemeManager dockTheme;
    dockTheme.setTheme(QStringLiteral("dark-rack"));
    dockTheme.applyToWidget(&main);
    main.show();
    QCoreApplication::processEvents();
    main.resizeDocks({left, right}, {250, 250}, Qt::Horizontal);
    QCoreApplication::processEvents();
    QImage titleImage(left->titleBarWidget()->size(), QImage::Format_ARGB32_Premultiplied);
    titleImage.fill(Qt::transparent);
    left->titleBarWidget()->render(&titleImage);
    int titlePixels = 0;
    for (int y = 1; y < titleImage.height() - 1; ++y)
        for (int x = 8; x < std::min(80, titleImage.width() - 1); ++x) {
            const auto color = titleImage.pixelColor(x, y);
            if (color.red() > 140 && color.green() > 140 && color.blue() > 140)
                ++titlePixels;
        }
    passed &= check(left->titleBarWidget()->height() == sidebarControlHeight
                    && titlePixels > 10, "shared dock paints its title at the common control height");
    auto* grip = left->findChild<QWidget*>(QStringLiteral("dockResizeGrip"));
    passed &= check(grip != nullptr, "shared dock exposes a painted width grip");
    if (grip) {
        const int originalWidth = left->width();
        const QPointF local(4, 50);
        const QPointF global = grip->mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(grip, &press);
        QMouseEvent move(QEvent::MouseMove, local + QPointF(70, 0), global + QPointF(70, 0),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(grip, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, local + QPointF(70, 0),
                            global + QPointF(70, 0), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(grip, &release);
        QCoreApplication::processEvents();
        passed &= check(left->width() >= originalWidth + 40,
                        "dragging the shared dock grip changes sidebar width");
    }
    auto* collapse = left->findChild<IconActionButton*>(QStringLiteral("dockCollapseButton"));
    passed &= check(collapse != nullptr, "shared dock exposes a collapse arrow");
    passed &= check(left->titleBarWidget()->findChildren<QToolButton*>().empty()
                    && right->titleBarWidget()->findChildren<QToolButton*>().empty(),
                    "dock headers omit floating and close controls");
    if (collapse) {
        const QPoint arrowTop = collapse->mapTo(left, QPoint(0, 0));
        passed &= check(arrowTop.x() >= left->width() - 24
                        && std::abs(arrowTop.y() + collapse->height() / 2 - left->height() / 2) <= 1,
                        "left dock collapse arrow sits at the canvas-facing edge midpoint");
        auto* rightArrow = right->findChild<IconActionButton*>(QStringLiteral("dockCollapseButton"));
        const QPoint rightTop = rightArrow ? rightArrow->mapTo(right, QPoint(0, 0)) : QPoint(-1, -1);
        passed &= check(rightArrow && rightTop.x() <= 2
                        && std::abs(rightTop.y() + rightArrow->height() / 2 - right->height() / 2) <= 1,
                        "right dock collapse arrow sits at the canvas-facing edge midpoint");
        collapse->click();
        QCoreApplication::processEvents();
        passed &= check(left->isCollapsed() && left->width() <= 40,
                        "dock arrow collapses the sidebar to a narrow rail");
        const QPoint collapsedArrowTop = collapse->mapTo(left, QPoint(0, 0));
        passed &= check(collapsedArrowTop.x() >= left->width() - 24
                            && std::abs(collapsedArrowTop.y() - arrowTop.y()) <= 1,
                        "left dock arrow keeps its edge and vertical position when collapsed");
        collapse->click();
        QCoreApplication::processEvents();
        passed &= check(!left->isCollapsed() && left->width() >= 160,
                        "dock arrow restores the sidebar width");
        if (rightArrow) {
            rightArrow->click();
            QCoreApplication::processEvents();
            const QPoint collapsedRightTop = rightArrow->mapTo(right, QPoint(0, 0));
            passed &= check(right->isCollapsed() && right->width() <= 40
                                && collapsedRightTop.x() <= 2
                                && std::abs(collapsedRightTop.y() - rightTop.y()) <= 1,
                            "right dock arrow keeps its edge and vertical position when collapsed");
            rightArrow->click();
            QCoreApplication::processEvents();
        }
    }
    QImage dockImage(main.size(), QImage::Format_ARGB32_Premultiplied);
    main.render(&dockImage);
    passed &= check(dockImage.save(QStringLiteral("build/shared-dock-panels.png")),
                    "shared resizable dock preview is written to the existing build directory");
    return passed;
}

bool testMixerEmptyGroupHeaders()
{
    using namespace WinJACKNexus;
    auto render = [](Common::DigitalMixerState& state) {
        UI::MixerWindow mixer(&state, [](const Common::DigitalMixerState&, QString*) {
            return true;
        });
        mixer.resize(1024, 768);
        mixer.show();
        QCoreApplication::processEvents();
        QImage image(mixer.size(), QImage::Format_ARGB32_Premultiplied);
        mixer.render(&image);
        return image;
    };
    Common::DigitalMixerState empty;
    const auto emptyImage = render(empty);
    Common::DigitalMixerState populated;
    populated.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    populated.add(Common::MixerStripKind::aux, Common::MixerLayout::stereo);
    const auto populatedImage = render(populated);
    Common::DigitalMixerState inputOnly;
    inputOnly.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    const auto inputOnlyImage = render(inputOnly);
    Common::DigitalMixerState auxOnly;
    auxOnly.add(Common::MixerStripKind::aux, Common::MixerLayout::stereo);
    const auto auxOnlyImage = render(auxOnly);
    bool passed = true;
    for (const QPoint point : {QPoint(915, 47), QPoint(1010, 47),
                               QPoint(915, 75), QPoint(1010, 75)})
        passed &= check(emptyImage.pixelColor(point) == populatedImage.pixelColor(point)
                        && inputOnlyImage.pixelColor(point) == populatedImage.pixelColor(point)
                        && auxOnlyImage.pixelColor(point) == populatedImage.pixelColor(point),
                        "missing input or AUX groups do not paint over the Main Output header");
    for (const QPoint point : {QPoint(553, 47), QPoint(899, 47)})
        passed &= check(auxOnlyImage.pixelColor(point) == populatedImage.pixelColor(point),
                        "missing input group does not paint over the AUX header");
    passed &= check(emptyImage.save(QStringLiteral("build/mixer-empty-groups.png")),
                    "empty-group mixer preview is written to the existing build directory");
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
    bar.setPeakValue(std::nullopt);
    passed &= check(bar.peakValue().has_value(),
                    "meter level bar keeps the default peak marker without a true-peak sample");

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

    auto& settings = ApplicationSettings::instance();
    const auto previousHold = settings.peakHoldSeconds();
    const auto previousFrameRate = settings.frameRate();
    settings.setFrameRate(30);
    settings.setPeakHoldSeconds(0.15);
    bar.clearValue();
    bar.setValue(-30.0F);
    bar.setPeakValue(6.0F);
    bar.setPeakValue(-30.0F);
    QEventLoop holdLoop;
    QTimer::singleShot(60, &holdLoop, &QEventLoop::quit);
    holdLoop.exec();
    settings.setFrameRate(120);
    bar.advanceAnimation();
    const bool held = bar.peakValue().has_value() && std::abs(*bar.peakValue() - 6.0F) < 0.01F;
    QEventLoop decayLoop;
    QTimer::singleShot(130, &decayLoop, &QEventLoop::quit);
    decayLoop.exec();
    bar.advanceAnimation();
    passed &= check(held && bar.peakValue().has_value()
                    && *bar.peakValue() < 6.0F && *bar.peakValue() > -30.0F,
                    "meter peak holds for real elapsed seconds across an FPS change, then decays");
    settings.setPeakHoldSeconds(previousHold);
    settings.setFrameRate(previousFrameRate);

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
    auto& settings = ApplicationSettings::instance();
    const auto originalFrameRate = settings.frameRate();
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
    const bool arrayUsesGlobalRate = settings.frameRate() == 30 && meters.frameRate() == 30;
    expandedMeterWidgets.front()->setFrameRate(120);
    const bool childUsesGlobalRate = settings.frameRate() == 120 && meters.frameRate() == 120
        && std::all_of(expandedMeterWidgets.cbegin(), expandedMeterWidgets.cend(), [](const auto* meter) {
            return meter->frameRate() == 120;
        });
    settings.setFrameRate(originalFrameRate);
    return check(arrayUsesGlobalRate && meterWidgets.size() == 3 && initialRatePropagated
                     && expandedMeterWidgets.size() == 4 && expandedRatePropagated
                     && childTimersStopped && childUsesGlobalRate,
                 "meter arrays and child meters follow the application frame rate with one array timer");
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

    auto& settings = ApplicationSettings::instance();
    const auto previousPeakRefresh = settings.peakValueRefreshMilliseconds();
    settings.setPeakValueRefreshMilliseconds(1000);
    readout.setPeakValueRefreshEnabled(true);
    readout.setValue(1.0);
    readout.setValue(2.0);
    const bool throttled = readout.peakValueRefreshEnabled() && readout.value() == 1.0;
    settings.setPeakValueRefreshMilliseconds(8);
    QEventLoop peakRefreshWait;
    QTimer::singleShot(15, &peakRefreshWait, &QEventLoop::quit);
    peakRefreshWait.exec();
    readout.setValue(3.0);
    passed &= check(throttled && readout.value() == 3.0,
                    "peak metric readout follows the global numeric refresh interval");
    settings.setPeakValueRefreshMilliseconds(previousPeakRefresh);

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

bool testFxArchiveImportFailurePreservesState()
{
    using namespace WinJACKNexus;
    UI::FxSettingsWindow window(QStringLiteral("Archive"), {QStringLiteral("L"), QStringLiteral("R")});
    Common::MixerEffectState damaged;
    damaged.moduleId = QStringLiteral("parametric-eq");
    damaged.enabled = false;
    damaged.preset = 2;
    damaged.parameters.insert(QStringLiteral("snapshot"), QStringLiteral("%%%"));
    std::vector<Common::MixerEffectState> published;
    window.setChainCommitHandler([&](const auto& chain, bool) {
        published = chain;
        return true;
    });
    window.loadChain({damaged}, false);
    window.setChainEnabled(true);
    bool passed = check(published.size() == 1
                     && published[0].moduleId == damaged.moduleId
                     && published[0].preset == damaged.preset
                     && published[0].parameters == damaged.parameters,
                 "FX chain keeps an unreadable Native Fx snapshot during later edits");
    UI::FxSettingsWindow copied(QStringLiteral("Copy"), {QStringLiteral("L"), QStringLiteral("R")});
    std::vector<Common::MixerEffectState> copiedChain;
    copied.setChainCommitHandler([&](const auto& chain, bool) {
        copiedChain = chain;
        return true;
    });
    copied.cloneChainFrom(window);
    passed &= check(copiedChain.size() == 1
                        && copiedChain[0].moduleId == damaged.moduleId
                        && copiedChain[0].parameters == damaged.parameters,
                    "copying an FX chain preserves unreadable module snapshots");
    return passed;
}

bool testFxSettingsLoadsNativeFx()
{
    using namespace WinJACKNexus::UI;
    FxSettingsWindow window(QStringLiteral("Line 3"), { QStringLiteral("M") });
    auto* chain = window.findChild<QListWidget*>();
    auto* chainAdd = window.findChild<QPushButton*>(QStringLiteral("fxAddBottom"));
    auto* chainScroll = window.findChild<QScrollArea*>(QStringLiteral("fxChainScroll"));
    auto* outputChip = window.findChild<QLabel*>(QStringLiteral("fxChainOut"));
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
                        && modulePreset->currentText() == QStringLiteral("Default"),
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
    auto* allChannel=window.findChild<QPushButton*>(QStringLiteral("fxChannelAll"));
    passed &= check(allChannel && !left && !right
                        && !window.findChild<QPushButton*>(QStringLiteral("fxChannelM")),
                    "mono FX control offers only All without a per-channel selector");
    FxLevelMeterWidget* meter = nullptr;
    for (auto* child : window.findChildren<QWidget*>())
        if (auto* value = dynamic_cast<FxLevelMeterWidget*>(child)) meter = value;
    passed &= check(meter != nullptr, "one meter panel stacks horizontal input and output bars");
    if (meter) {
        const auto bar = meter->barRect(0);
        passed &= check(std::abs(meter->positionForDb(-72) - bar.left()) < 0.1
                            && std::abs(meter->positionForDb(12) - bar.right()) < 0.1
                            && std::abs(meter->positionForDb(-12) - (bar.left() + bar.width() * 5.0 / 7.0)) < 0.1,
                        "FX meter ticks use the exact bar geometry and -72 to +12 scale");
        meter->setLevels(-100,20);
        passed &= check(meter->inputDb()==-72 && meter->outputDb()==12,
                        "FX input and output meter values clamp to -72 through +12 dB");
        meter->setLevels(-24,-26);
    }
    window.show();
    QCoreApplication::processEvents();
    for (int reopen = 0; reopen < 3; ++reopen) {
        window.hide();
        window.show();
        QCoreApplication::processEvents();
    }
    passed &= check(chain && !chainAdd && chainScroll && outputChip
                        && outputChip->parentWidget() == chain->parentWidget()
                        && outputChip->x() > chain->geometry().right()
                        && chain->width() > chain->height(),
                    "FX footer chain has input, modules and output without an inline add button");
    passed &= check(chainPower && chainPower->parentWidget()
                        && chainPower->parentWidget()->mapTo(&window,QPoint()).y() < chainScroll->mapTo(&window,QPoint()).y(),
                    "FX master switch sits in the top header");
    auto* headerChannel=window.findChild<QPushButton*>(QStringLiteral("fxChannelAll"));
    auto* windowPreset=window.findChild<QComboBox*>(QStringLiteral("fxWindowPreset"));
    auto* channelControls=window.findChild<QWidget*>(QStringLiteral("fxChannelControls"));
    auto* fxHeader=channelControls?channelControls->parentWidget():nullptr;
    passed &= check(headerChannel && windowPreset && fxHeader && channelControls
                        && headerChannel->parentWidget()==channelControls
                        && channelControls->parentWidget()==fxHeader
                        && headerChannel->mapTo(&window,QPoint()).x() < windowPreset->mapTo(&window,QPoint()).x(),
                    "FX channel selector sits in the top header before presets");
    passed &= check(windowPreset && windowPreset->currentText() == QStringLiteral("Default"),
                    "FX chain preset starts with the shared Default name");
    const auto channelCentered=[&] {
        return fxHeader && channelControls
            && std::abs(channelControls->mapTo(fxHeader,QPoint()).x()
                        +channelControls->width()/2.0-fxHeader->width()/2.0)<=2.0;
    };
    passed &= check(channelCentered(),"FX channel selector group is centered in the header");
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
    passed &= check(channelCentered(),"FX channel selector stays centered at reference resolution");
    QImage widePreview(window.size(), QImage::Format_ARGB32_Premultiplied);
    widePreview.fill(Qt::transparent);
    window.render(&widePreview);
    passed &= check(widePreview.save(QStringLiteral("build/fx-ui-review-wide.png")),
                    "reopened FX window review image is written at reference resolution");
    window.setSourceChannels({QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),
                              QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),
                              QStringLiteral("Lb"),QStringLiteral("Rb")});
    QCoreApplication::processEvents();
    passed &= check(channelCentered()
                        && window.findChild<QPushButton*>(QStringLiteral("fxChannelRb")),
                    "FX channel group remains centered when all eight channels are available");
    QPushButton* addModule = nullptr;
    for (auto* choice : window.findChildren<QPushButton*>())
        if (choice->text().contains(QStringLiteral("添加模块"))) { addModule = choice; break; }
    int availableNativeEffects = 0;
    bool addMenuWithinWindow = false;
    if (addModule) {
        QTimer::singleShot(0, &window, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                availableNativeEffects = menu->actions().size();
                addMenuWithinWindow = QRect(window.mapToGlobal(QPoint()), window.size())
                    .contains(menu->frameGeometry());
                if (!menu->actions().isEmpty()) menu->actions().first()->trigger();
                menu->close();
            }
        });
        addModule->click();
    }
    passed &= check(availableNativeEffects == 8, "FX add menu discovers all eight Native Fx modules");
    passed &= check(addMenuWithinWindow, "FX add menu stays inside the FX window");
    passed &= check(chain && chain->count() == 2 && chain->item(1)->text() == QStringLiteral("Gate"),
                    "FX add menu loads another Native Fx module into the chain");
    QCoreApplication::processEvents();
    passed &= check(chain && chain->visualItemRect(chain->item(0)).width()
                            > chain->visualItemRect(chain->item(1)).width(),
                    "FX chain cards adapt their widths to module name length");
    passed &= check(outputChip && outputChip->x() > chain->geometry().right(),
                    "FX output follows the last item as the horizontal chain grows");
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
        const QRect card=chain->visualItemRect(chain->item(1));
        const auto clickCard=[chain](QPoint local){
            auto* viewport=chain->viewport();
            const QPoint global=viewport->mapToGlobal(local);
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(local),QPointF(global),
                              Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(viewport,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(local),QPointF(global),
                                Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(viewport,&release);
        };
        clickCard(card.center());
        passed &= check(!chain->item(1)->data(Qt::UserRole + 3).toBool(),
                        "clicking a lit effect card extinguishes it");
        clickCard(card.center());
        passed &= check(chain->item(1)->data(Qt::UserRole + 3).toBool(),
                        "clicking an unlit effect card lights it");
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
    bool compactMenuWithinWindow = false;
    if (addModule) {
        QTimer::singleShot(0, &window, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                compactMenuWithinWindow = QRect(window.mapToGlobal(QPoint()), window.size())
                    .contains(menu->frameGeometry());
                menu->close();
            }
        });
        addModule->click();
    }
    passed &= check(compactMenuWithinWindow,
                    "FX add menu stays inside the FX window at its compact size");
    if (chainScroll && outputChip) {
        auto* scrollBar = chainScroll->horizontalScrollBar();
        passed &= check(scrollBar->maximum() > 0 && chainScroll->verticalScrollBar()->maximum() == 0,
                        "FX chain shows a horizontal scrollbar when modules exceed the footer width");
        scrollBar->setValue(scrollBar->maximum());
        QCoreApplication::processEvents();
        const int outputRight = outputChip->mapTo(chainScroll->viewport(),
                                                   QPoint(outputChip->width(), 0)).x();
        passed &= check(outputRight > 0 && outputRight <= chainScroll->viewport()->width(),
                        "FX output stays reachable after the last effect at the end of the horizontal scroll area");
        QImage overflowPreview(window.size(),QImage::Format_ARGB32_Premultiplied);
        window.render(&overflowPreview);
        passed &= check(overflowPreview.save(QStringLiteral("build/fx-chain-overflow-review.png")),
                        "overflowed FX chain review image is written to the existing build directory");
    }
    FxSettingsWindow stereo(QStringLiteral("Main L/R"), { QStringLiteral("L"), QStringLiteral("R") });
    auto* stereoRight=stereo.findChild<QPushButton*>(QStringLiteral("fxChannelR"));
    passed &= check(stereoRight != nullptr, "stereo mixer strip shows both available channels");
    if (stereoRight) stereoRight->click();
    auto* stereoMeterTitle=stereo.findChild<QLabel*>(QStringLiteral("fxMeterTitle"));
    auto* stereoModuleTitle=stereo.findChild<QLabel*>(QStringLiteral("fxModuleTitle"));
    passed &= check(!stereo.findChild<QComboBox*>(QStringLiteral("fxMeterChannel"))
                        && stereoMeterTitle && stereoMeterTitle->text()==QStringLiteral("输入/输出(R)")
                        && stereoModuleTitle && stereoModuleTitle->text().endsWith(QStringLiteral("· R")),
                    "per-channel FX mode names the selected channel without a meter dropdown");
    stereo.show();
    QCoreApplication::processEvents();
    QImage channelPreview(stereo.size(),QImage::Format_ARGB32_Premultiplied);
    stereo.render(&channelPreview);
    passed &= check(channelPreview.save(QStringLiteral("build/fx-channel-review.png")),
                    "per-channel FX review image is written to the existing build directory");
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
    const auto drag = [](QWidget* widget, QPoint from, QPoint to) {
        const auto globalFrom = widget->mapToGlobal(from);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(globalFrom),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &press);
        const auto globalTo = widget->mapToGlobal(to);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(globalTo),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), QPointF(globalTo),
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
    passed &= check(!openContext(QStringLiteral("打开FX设置"))
                         && !openContext(QStringLiteral("打开AUX设置")),
                     "mixer context menu routes AUX and FX through SEL only");
    const auto fullMainLevel = mixer.mainInputLevelDb();
    passed &= check(mixer.channelMixToMain(0) && mixer.channelMixToMain(8)
                        && !mixer.channelMixToMain(11),
                    "input and AUX MIX start enabled while MAIN has no MIX send");
    click(&mixer, {87, 418});
    passed &= check(!mixer.channelMixToMain(0) && mixer.mainInputLevelDb() < fullMainLevel,
                    "input MIX excludes its channel from MAIN");
    const auto excludedMainLevel = mixer.mainInputLevelDb();
    click(&mixer, {87, 380});
    passed &= check(std::abs(mixer.mainInputLevelDb() - excludedMainLevel) < .001,
                    "solo on an excluded channel does not interrupt MAIN");
    click(&mixer, {87, 380});
    click(&mixer, {87, 418});
    passed &= check(mixer.channelMixToMain(0)
                        && std::abs(mixer.mainInputLevelDb() - fullMainLevel) < .001,
                    "input MIX restores its MAIN contribution");
    click(&mixer, {626, 418});
    passed &= check(!mixer.channelMixToMain(8) && mixer.mainInputLevelDb() < fullMainLevel,
                    "AUX MIX excludes its channel from MAIN");
    click(&mixer, {626, 418});
    passed &= check(mixer.channelMixToMain(8), "AUX MIX restores its MAIN contribution");
    click(&mixer, {40, 418});
    auto* selected = mixer.findChild<QLabel*>(QStringLiteral("selSelectedChannel"));
    auto* sel = mixer.findChild<QWidget*>(QStringLiteral("selLargeScreen"));
    auto* pages = mixer.findChild<QStackedWidget*>(QStringLiteral("selPages"));
    passed &= check(sel && sel->isVisible() && selected && selected->text() == QStringLiteral("Mic 1"),
                    "channel SEL button automatically opens the selected large screen");

    if (auto* fxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabFX"))) fxTab->click();
    FxSettingsWindow* fx = nullptr;
    if (pages) for (auto* child : pages->widget(2)->findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<FxSettingsWindow*>(child)) { fx = candidate; break; }
    auto* fxToggle = fx ? dynamic_cast<Controls::ToggleButton*>(
        fx->findChild<QWidget*>(QStringLiteral("fxChainToggle"))) : nullptr;
    passed &= check(fxToggle && !fxToggle->isChecked(), "SEL FX page reads the mixer channel switch");
    if (fxToggle) fxToggle->click();
    passed &= check(fxToggle && fxToggle->isChecked(), "SEL FX page updates the channel switch");

    if (auto* auxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"))) auxTab->click();
    AuxSettingsWindow* aux = nullptr;
    if (pages) for (auto* child : pages->widget(1)->findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<AuxSettingsWindow*>(child)) { aux = candidate; break; }
    passed &= check(aux && !aux->chainEnabled(), "SEL AUX page reads the mixer channel switch");
    if (aux) {
        const auto scale=std::min(aux->width()/1360.0,aux->height()/813.0);
        click(aux,{qRound((aux->width()-1360*scale)/2+222*scale),
                   qRound((aux->height()-813*scale)/2+30*scale)});
    }
    passed &= check(aux && aux->chainEnabled(), "SEL AUX page updates the channel switch");
    if (aux && sel) {
        QImage auxPreview(aux->size(), QImage::Format_ARGB32_Premultiplied);
        auxPreview.fill(Qt::transparent);
        aux->render(&auxPreview);
        passed &= check(auxPreview.save(QStringLiteral("build/aux-ui-review.png")),
                        "AUX window review image is written to the existing build directory");
        const auto originalSize=sel->size();
        sel->resize(1900,1000);
        QCoreApplication::processEvents();
        QImage wideAuxPreview(aux->size(),QImage::Format_ARGB32_Premultiplied);
        wideAuxPreview.fill(Qt::transparent);
        aux->render(&wideAuxPreview);
        passed &= check(wideAuxPreview.save(QStringLiteral("build/aux-ui-review-wide.png")),
                        "wide AUX detail review image is written to the existing build directory");
        sel->resize(originalSize);
        QCoreApplication::processEvents();
        const auto mainPanBefore=mixer.findChild<QWidget*>(QStringLiteral("selMainPage"))->property("selClassicPan").toDouble();
        const auto auxPoint=[aux](int x,int y){
            const auto scale=std::min(aux->width()/1360.0,aux->height()/813.0);
            return QPoint(qRound((aux->width()-1360*scale)/2+x*scale),
                          qRound((aux->height()-813*scale)/2+y*scale));
        };
        drag(aux,auxPoint(686,319),auxPoint(686,279));
        passed &= check(std::abs(aux->property("auxClassicPan").toDouble()-.4)<.04
                            && std::abs(mixer.findChild<QWidget*>(QStringLiteral("selMainPage"))->property("selClassicPan").toDouble()-mainPanBefore)<.001,
                        "AUX classic Pan value changes independently from Main");
    }

    auto auxState = std::make_shared<WinJACKNexus::Common::AuxMixerState>();
    AuxSettingsWindow auxControls(auxState);
    const auto sourceKey = QStringLiteral("ui-test-input");
    auxControls.setSource(sourceKey, QStringLiteral("Line 3"), 0,
                          WinJACKNexus::Common::AuxChannelLayout::mono,QStringLiteral("CH 3"));
    auxControls.setChainEnabled(true);
    auxControls.show();
    QCoreApplication::processEvents();
    auto* firstBus = auxState->busForSource(sourceKey, 1);
    passed &= check(firstBus && firstBus->enabled, "first AUX send starts enabled");
    passed &= check(firstBus && firstBus->panMode==WinJACKNexus::Common::AuxPanMode::classic
                        && std::abs(firstBus->classicPan)<.001 && !firstBus->linked,
                    "mono-to-stereo AUX send defaults to independent centered classic Pan");
    drag(&auxControls,{686,319},{686,269});
    passed &= check(firstBus && std::abs(firstBus->classicPan-.5)<.03,
                    "AUX classic Pan knob adjusts its own pan value");
    const auto auxWidthBeforeDisabled=firstBus?firstBus->width:0.0;
    drag(&auxControls,{504,694},{504,654});
    passed &= check(firstBus && firstBus->width==auxWidthBeforeDisabled,
                    "AUX spatial parameters are disabled in classic Pan mode");
    click(&auxControls,{488,69});
    passed &= check(firstBus && firstBus->panMode==WinJACKNexus::Common::AuxPanMode::spatial
                        && std::abs(firstBus->classicPan-.5)<.03,
                    "AUX can switch to spatial panning without losing its classic Pan value");
    click(&auxControls,{504,760});
    auto* auxEditor=auxControls.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    passed &= check(auxEditor&&auxEditor->isVisible(),
                    "AUX spatial parameter readout opens the shared numeric editor");
    if(auxEditor) {
        auxEditor->setText(QStringLiteral("85"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(auxEditor,&enter);
        passed &= check(firstBus&&std::abs(firstBus->width-.85)<.001,
                        "AUX exact width value commits to the selected bus");
    }
    QMouseEvent auxDouble(QEvent::MouseButtonDblClick,QPointF(504,694),QPointF(504,694),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&auxControls,&auxDouble);
    passed &= check(firstBus&&std::abs(firstBus->width-1.0)<.001,
                    "AUX width knob double-click restores its default");
    const auto monoPointBefore=firstBus?firstBus->adjustablePoints[0]:QPointF();
    passed &= check(firstBus && firstBus->showPolarCurves, "AUX polar curves start visible");
    click(&auxControls,{872,68});
    passed &= check(firstBus && !firstBus->showPolarCurves
                        && firstBus->adjustablePoints[0]==monoPointBefore
                        && firstBus->width==auxWidthBeforeDisabled && firstBus->enabled,
                    "clicking the drawn AUX polar switch hides curves without changing the send");
    click(&auxControls,{872,68});
    passed &= check(firstBus && firstBus->showPolarCurves, "AUX polar curves can be shown again");
    click(&auxControls,{455,520});
    passed &= check(firstBus && !firstBus->showInputCurve && firstBus->showOutputCurve
                        && !auxControls.property("auxInputCurveVisible").toBool(),
                    "AUX input contour has its own visibility state");
    click(&auxControls,{530,520});
    passed &= check(firstBus && !firstBus->showInputCurve && !firstBus->showOutputCurve,
                    "AUX output contour visibility is independent");
    click(&auxControls,{455,520});
    click(&auxControls,{530,520});
    drag(&auxControls,{702,205},{790,205});
    passed &= check(firstBus && firstBus->pointInfluenceRadii[0]>.6
                        && firstBus->adjustablePoints[0]==monoPointBefore
                        && auxControls.property("auxPannerInfluence").toDouble()>.6,
                    "AUX spatial point outer ring adjusts nearby speaker influence without moving the point");
    auxState->setBusAccent(1, QColor("#a12bcd"));
    passed &= check(auxState->bus(1)->accent == QColor("#a12bcd")
                        && firstBus && firstBus->accent == QColor("#a12bcd"),
                    "AUX channel color reaches the source-specific AUX list state");
    click(&auxControls, {48, 98});
    passed &= check(firstBus && !firstBus->enabled, "AUX row toggle disables its send");
    click(&auxControls, {48, 98});
    passed &= check(firstBus && firstBus->enabled, "AUX row toggle enables its send");
    int auxChanges = 0;
    auxControls.setStateChangedCallback([&] { ++auxChanges; });
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
    passed &= check(chooseAuxMenu({345, 98}, [](QMenu* menu) {
                        menu->actions()[2]->menu()->actions()[2]->trigger();
                    })
                        && firstBus && firstBus->targetLayout == WinJACKNexus::Common::AuxChannelLayout::surround51
                        && auxChanges > 0,
                    "AUX detail mode selects the actual bus channel layout");
    click(&auxControls,{635,69});
    passed &= check(firstBus && firstBus->panMode==WinJACKNexus::Common::AuxPanMode::spatial,
                    "AUX surround sends cannot switch to classic Pan");
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
    firstBus=auxState->busForSource(sourceKey,1);
    click(&auxControls,{85,98});
    click(&auxControls,{635,69});
    passed &= check(firstBus && firstBus->panMode==WinJACKNexus::Common::AuxPanMode::spatial
                        && firstBus->sends.size()==1 && firstBus->sends[0].label==QStringLiteral("M"),
                    "mono-to-mono AUX has an M control and cannot enter classic Pan");
    const auto auxChangesBeforeDisabledClick=auxChanges;
    click(&auxControls,{1062,304});
    passed &= check(auxState->busForSource(sourceKey,1)->sends.size()==1
                        && auxChanges==auxChangesBeforeDisabledClick,
                    "AUX keeps eight visible knob rows but ignores inactive channel controls");
    passed &= check(chooseAuxMenu({345, 505}, [](QMenu* menu) { menu->actions()[1]->trigger(); })
                        && auxState->buses().size() == 4,
                    "AUX row gear deletes the selected bus");

    auto routedState = std::make_shared<WinJACKNexus::Common::AuxMixerState>();
    AuxSettingsWindow routedAux(routedState);
    routedAux.setSource(QStringLiteral("aux:1"), QStringLiteral("AUX 1"), 1,
                        WinJACKNexus::Common::AuxChannelLayout::stereo);
    routedAux.show();
    QCoreApplication::processEvents();
    passed &= check(routedAux.selectedBusId() == 2, "AUX source excludes itself from the target list");
    routedAux.selectBus(1);
    passed &= check(routedAux.selectedBusId() == 2, "self AUX cannot be selected programmatically");
    routedAux.setChainEnabled(true);
    routedAux.setStateCommitCallback([] { return false; });
    click(&routedAux, {48, 98});
    passed &= check(routedState->busForSource(QStringLiteral("aux:1"), 2, 1)->enabled,
                    "rejected AUX edit restores the prior route state");
    routedAux.setChainCommitHandler([](bool) { return false; });
    click(&routedAux, {222, 30});
    passed &= check(routedAux.chainEnabled(), "rejected AUX master toggle preserves its prior state");

    auto surroundAuxState=std::make_shared<WinJACKNexus::Common::AuxMixerState>();
    surroundAuxState->setBusLayout(1,WinJACKNexus::Common::AuxChannelLayout::surround51);
    AuxSettingsWindow surroundAux(surroundAuxState);
    surroundAux.setSource(QStringLiteral("ui-test-surround"),QStringLiteral("Surround 1"),0,
                          WinJACKNexus::Common::AuxChannelLayout::surround51,QStringLiteral("CH 9"));
    surroundAux.show();
    QCoreApplication::processEvents();
    QImage auxSurroundPreview(surroundAux.size(),QImage::Format_ARGB32_Premultiplied);
    auxSurroundPreview.fill(Qt::transparent);
    surroundAux.render(&auxSurroundPreview);
    passed &= check(auxSurroundPreview.save(QStringLiteral("build/aux-surround-ui-review.png")),
                    "surround AUX review image is written to the existing build directory");
    auto* surroundSend=surroundAuxState->busForSource(QStringLiteral("ui-test-surround"),1);
    click(&surroundAux,{1105,69}); // Split send levels before adjusting LFE.
    const auto lfeBefore=surroundSend?surroundSend->sends[3].levelDb:0.0;
    const auto centerBefore=surroundSend?surroundSend->sends[2].levelDb:0.0;
    drag(&surroundAux,{1062,390},{1062,440});
    passed &= check(surroundSend && surroundSend->sends[3].levelDb<lfeBefore-9.0
                        && surroundSend->sends[2].levelDb==centerBefore,
                    "surround AUX LFE intensity remains independently adjustable in its send card");

    bar->actions()[2]->trigger();
    passed &= check(selected && selected->text() == QStringLiteral("Mic 1"),
                     "large screen starts on the selected channel");
    auto* mainPage = mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
    auto* sidebarMode = mixer.findChild<QPushButton*>(QStringLiteral("selChannelMode"));
    auto* modeArrow = mixer.findChild<QLabel*>(QStringLiteral("selChannelModeArrow"));
    auto* channelName = mixer.findChild<QLineEdit*>(QStringLiteral("selChannelName"));
    auto* channelComment = mixer.findChild<QLineEdit*>(QStringLiteral("selChannelComment"));
    passed &= check(sel && mainPage && pages && pages->count() == 3 && channelName && channelComment && sidebarMode,
                    "SEL contains the Main, AUX and FX pages and editable channel details");
    if (sel && mainPage) {
        const auto originalSize=sel->size();
        sel->resize(1200,900);
        QCoreApplication::processEvents();
        const auto uniformScale=std::min(mainPage->width()/1394.0,mainPage->height()/813.0);
        passed &= check(channelName && channelComment
                            && std::abs(channelName->width()-254*uniformScale)<=2
                            && std::abs(channelName->height()-34*uniformScale)<=2
                            && std::abs(channelComment->height()-34*uniformScale)<=2
                            && sidebarMode && std::abs(sidebarMode->height()-34*uniformScale)<=2,
                        "SEL Main keeps one scale for both axes when the window aspect ratio changes");
        sel->resize(1900,1000);
        QCoreApplication::processEvents();
        const auto wideScale=std::min(mainPage->width()/1394.0,mainPage->height()/813.0);
        const auto leftGap=pages?pages->x()-313*wideScale:0.0;
        const auto rightGap=pages?mainPage->width()-pages->geometry().right()-1:0.0;
        passed &= check(channelName && pages && std::abs(channelName->x()-29*wideScale)<=2
                            && std::abs(leftGap-rightGap)<=3,
                        "SEL sidebar stays at the left edge while right details stay centered");
        sel->resize(originalSize);
        QCoreApplication::processEvents();
        if (auto* mainTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabMain"))) mainTab->click();
        auto* customColor = mixer.findChild<QPushButton*>(QStringLiteral("selCustomColor"));
        passed &= check(sidebarMode && sidebarMode->isVisible()
                            && sidebarMode->parentWidget() == mainPage
                            && sidebarMode->text().contains(QStringLiteral("立体声"))
                            && sidebarMode->geometry().top() > channelComment->geometry().bottom()
                            && customColor && sidebarMode->geometry().bottom() < customColor->geometry().top()
                            && sidebarMode->height() == customColor->height()
                            && modeArrow && modeArrow->parentWidget() == sidebarMode
                            && modeArrow->geometry().right() >= sidebarMode->width()-10,
                        "SEL mode uses a separate sidebar label, equal-height fields and a right-aligned arrow");
        QImage selPreview(sel->size(), QImage::Format_ARGB32_Premultiplied);
        selPreview.fill(Qt::transparent);
        sel->render(&selPreview);
        passed &= check(selPreview.save(QStringLiteral("build/sel-ui-review.png")),
                        "SEL review image is written to the existing build directory");
        const auto mainPoint=[mainPage](int x,int y){
            const auto scale=std::min(mainPage->width()/1394.0,mainPage->height()/813.0);
            const auto right=313*scale+(mainPage->width()-1377*scale)/2;
            return QPoint(qRound(x*scale+(x>=313?right-313*scale:0)),
                          qRound((mainPage->height()-813*scale)/2+y*scale));
        };
        const auto togglePixel=[mainPage](QPoint point){
            QImage image(mainPage->size(),QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            mainPage->render(&image);
            return image.pixelColor(point);
        };
        passed &= check(mainPage->property("selPanMode").toInt()
                            ==static_cast<int>(WinJACKNexus::Common::AuxPanMode::classic),
                        "stereo SEL Main defaults to classic Pan");
        drag(mainPage,mainPoint(638,319),mainPoint(638,269));
        passed &= check(std::abs(mainPage->property("selClassicPan").toDouble()-.5)<.03,
                        "SEL classic Pan knob adjusts its own pan value");
        const auto disabledWidth=mainPage->property("selPannerWidth").toDouble();
        drag(mainPage,mainPoint(427,694),mainPoint(427,654));
        passed &= check(mainPage->property("selPannerWidth").toDouble()==disabledWidth,
                        "SEL spatial parameters are disabled while classic Pan is active");
        click(mainPage,mainPoint(390,69));
        passed &= check(mainPage->property("selPanMode").toInt()
                            ==static_cast<int>(WinJACKNexus::Common::AuxPanMode::spatial)
                            && std::abs(mainPage->property("selClassicPan").toDouble()-.5)<.03,
                        "SEL can switch to spatial panning while keeping its classic Pan value");
        passed &= check(mainPage->property("selPolarCurvesVisible").toBool(),
                        "SEL polar curves start visible");
        click(mainPage,mainPoint(780,68));
        passed &= check(!mainPage->property("selPolarCurvesVisible").toBool()
                            && mainPage->property("selPannerWidth").toDouble()==disabledWidth,
                        "SEL polar curve visibility changes independently of panning");
        click(mainPage,mainPoint(780,68));
        passed &= check(mainPage->property("selPolarCurvesVisible").toBool(),
                        "SEL polar curves can be shown again");
        click(mainPage,mainPoint(838,68));
        passed &= check(!mainPage->property("selInputCurveVisible").toBool()
                            && mainPage->property("selOutputCurveVisible").toBool(),
                        "SEL input contour visibility is independent");
        click(mainPage,mainPoint(901,68));
        passed &= check(!mainPage->property("selOutputCurveVisible").toBool(),
                        "SEL output contour visibility is independent");
        click(mainPage,mainPoint(838,68));
        click(mainPage,mainPoint(901,68));
        drag(mainPage,mainPoint(592,212),mainPoint(670,212));
        passed &= check(mainPage->property("selPannerInfluence").toDouble()>.5,
                        "SEL spatial point outer ring adjusts nearby speaker influence");
        const auto mutePixel=togglePixel(mainPoint(40,413));
        click(&mixer,{40,380});
        passed &= check(togglePixel(mainPoint(40,413))!=mutePixel,
                        "mixer MUTE button changes the selected channel state in SEL");
        click(&mixer,{40,380});
        const auto soloPixel=togglePixel(mainPoint(175,413));
        click(&mixer,{87,380});
        passed &= check(togglePixel(mainPoint(175,413))!=soloPixel,
                        "mixer SOLO button changes the selected channel state in SEL");
        click(&mixer,{87,380});
        drag(mainPage, mainPoint(93,375), mainPoint(93,345));
        passed &= check(std::abs(mainPage->property("selInputGainDb").toDouble()-6.0)<.5,
                        "SEL input gain knob raises the selected channel gain");
        drag(mainPage, mainPoint(93,375), mainPoint(93,255));
        passed &= check(mainPage->property("selInputGainDb").toDouble()==12.0,
                        "SEL input gain is limited to +12 dB");
        drag(mainPage, mainPoint(93,375), mainPoint(93,495));
        passed &= check(mainPage->property("selInputGainDb").toDouble()==-12.0,
                        "SEL input gain is limited to -12 dB");
        drag(mainPage, mainPoint(93,375), mainPoint(93,315));
        passed &= check(std::abs(mainPage->property("selInputGainDb").toDouble())<.5,
                        "SEL input gain can return to 0 dB");
        drag(mainPage, mainPoint(93,375), mainPoint(93,345));
        click(&mixer, {150,418});
        const bool secondChannelStartsFlat=std::abs(mainPage->property("selInputGainDb").toDouble())<.5;
        click(&mixer, {40,418});
        passed &= check(secondChannelStartsFlat
                            && std::abs(mainPage->property("selInputGainDb").toDouble()-6.0)<.5,
                        "SEL input gain stays with its own channel when selection changes");
        drag(mainPage, mainPoint(93,375), mainPoint(93,405));
        const auto sendMuteBefore=togglePixel(mainPoint(1214,151));
        click(mainPage,mainPoint(1236,159));
        passed &= check(togglePixel(mainPoint(1214,151))!=sendMuteBefore,
                        "SEL send MUTE responds below its horizontal meter");
        click(mainPage,mainPoint(1236,159));
        const auto sendSoloBefore=togglePixel(mainPoint(1295,151));
        click(mainPage,mainPoint(1307,159));
        passed &= check(togglePixel(mainPoint(1295,151))!=sendSoloBefore,
                        "SEL send SOLO responds below its horizontal meter");
        click(mainPage,mainPoint(1307,159));
        click(mainPage,mainPoint(1098,68)); // Split all send controls.
        drag(mainPage,mainPoint(1073,140),mainPoint(1073,190));
        const auto splitLevels=mainPage->property("selSendLevels").toList();
        passed &= check(splitLevels.size()==8 && std::abs(splitLevels[0].toDouble()+10.0)<1.0
                            && std::abs(splitLevels[1].toDouble())<.1,
                        "split SEL knobs keep independent channel levels");
        drag(mainPage,mainPoint(1073,312),mainPoint(1073,362));
        const auto disabledLevels=mainPage->property("selSendLevels").toList();
        passed &= check(mainPage->property("selActiveSendCount").toInt()==2
                        && std::abs(disabledLevels[2].toDouble())<.1,
                        "SEL shows eight knob cards and disables those outside the selected layout");
        click(mainPage,mainPoint(1098,68)); // Link all channels.
        drag(mainPage,mainPoint(1073,140),mainPoint(1073,170));
        const auto linkedLevels=mainPage->property("selSendLevels").toList();
        passed &= check(linkedLevels.size()==8 && std::abs(linkedLevels[0].toDouble()+16.0)<1.0
                            && std::abs(linkedLevels[1].toDouble()+6.0)<1.0,
                        "linked SEL knobs shift every channel by the same dB delta");
        const double initialFrontBack=mainPage->property("selPannerFrontBack").toDouble();
        drag(mainPage,mainPoint(427,694),mainPoint(427,734));
        drag(mainPage,mainPoint(633,694),mainPoint(633,654));
        const bool widthAndFrontBackChanged=std::abs(mainPage->property("selPannerWidth").toDouble()-.8)<.02
            && std::abs(mainPage->property("selPannerFrontBack").toDouble()-initialFrontBack-.4)<.03;
        drag(mainPage,mainPoint(839,694),mainPoint(839,654));
        passed &= check(widthAndFrontBackChanged
                            && std::abs(mainPage->property("selPannerAngle").toDouble()-72.0)<2.0,
                        "SEL spatial parameter knobs adjust width, front/back and rotating angle");
        bool modeOpened=false;
        QTimer::singleShot(0,&mixer,[&]{if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){
            modeOpened=true;menu->actions()[2]->trigger();menu->close();}});
        if (sidebarMode) sidebarMode->click();
        passed &= check(modeOpened && mainPage->property("selChannelLayout").toInt()
                            ==static_cast<int>(WinJACKNexus::Common::AuxChannelLayout::surround51)
                            && mainPage->property("selActiveSendCount").toInt()==6
                            && mainPage->property("selPanMode").toInt()
                                ==static_cast<int>(WinJACKNexus::Common::AuxPanMode::spatial)
                            && sidebarMode->text().contains(QStringLiteral("5.1")),
                        "SEL sidebar channel mode changes the actual channel layout");
        QImage surroundPreview(sel->size(), QImage::Format_ARGB32_Premultiplied);
        surroundPreview.fill(Qt::transparent);
        sel->render(&surroundPreview);
        passed &= check(surroundPreview.save(QStringLiteral("build/sel-surround-ui-review.png")),
                        "surround SEL review image is written to the existing build directory");
        click(mainPage,mainPoint(1098,68)); // Split the send levels for the LFE check.
        const auto levelsBeforeLfe=mainPage->property("selSendLevels").toList();
        drag(mainPage,mainPoint(1073,390),mainPoint(1073,440));
        const auto levelsAfterLfe=mainPage->property("selSendLevels").toList();
        passed &= check(levelsBeforeLfe.size()==8 && levelsAfterLfe.size()==8
                            && levelsAfterLfe[3].toDouble()<levelsBeforeLfe[3].toDouble()-9.0
                            && levelsAfterLfe[2].toDouble()==levelsBeforeLfe[2].toDouble(),
                        "surround LFE intensity remains independently adjustable in its send card");
        click(mainPage,mainPoint(545,69));
        passed &= check(mainPage->property("selPanMode").toInt()
                            ==static_cast<int>(WinJACKNexus::Common::AuxPanMode::spatial),
                        "SEL surround channels cannot switch to classic Pan");
        QTimer::singleShot(0,&mixer,[]{if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){
            menu->actions()[1]->trigger();menu->close();}});
        if (sidebarMode) sidebarMode->click();
        click(mainPage,mainPoint(1310,68));
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
        passed &= check(sidebarMode && sidebarMode->isVisible(),
                        "SEL sidebar channel mode remains available on the AUX page");
        passed &= check(mainPage && mainPage->isVisible() && channelName && channelName->isVisible()
                            && pages->isVisible() && pages->geometry().left() > channelName->geometry().right(),
                        "SEL Main sidebar stays visible beside AUX details");
    }
    if (auto* fxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabFX"))) {
        fxTab->click();
        bool embeddedFx = false;
        if (pages) for (auto* child : pages->widget(2)->findChildren<QWidget*>())
            if (dynamic_cast<FxSettingsWindow*>(child)) embeddedFx = true;
        passed &= check(pages && pages->currentIndex() == 2 && embeddedFx,
                        "SEL FX tab embeds the existing FX settings interface");
        passed &= check(mainPage && mainPage->isVisible() && channelName && channelName->isVisible()
                            && pages->isVisible() && pages->geometry().left() > channelName->geometry().right(),
                        "SEL Main sidebar stays visible beside FX details");
        if(sel){
            const auto originalSize=sel->size();
            sel->resize(1900,1000);
            QCoreApplication::processEvents();
            QImage widePreview(sel->size(),QImage::Format_ARGB32_Premultiplied);
            sel->render(&widePreview);
            passed &= check(widePreview.save(QStringLiteral("build/sel-fx-wide-ui-review.png")),
                            "wide SEL FX review image is written to the existing build directory");
            sel->resize(originalSize);
            QCoreApplication::processEvents();
        }
    }
    if (auto* mainTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabMain"))) mainTab->click();
    click(&mixer, {963, 418});
    passed &= check(selected && selected->text() == QStringLiteral("Main L/R"),
                    "Main strip opens its own SEL channel");
    if(mainPage){
        const auto mainPoint=[mainPage](int x,int y){
            const auto scale=std::min(mainPage->width()/1394.0,mainPage->height()/813.0);
            const auto right=313*scale+(mainPage->width()-1377*scale)/2;
            return QPoint(qRound(x*scale+(x>=313?right-313*scale:0)),
                          qRound((mainPage->height()-813*scale)/2+y*scale));
        };
        QImage before(mainPage->size(),QImage::Format_ARGB32_Premultiplied);
        mainPage->render(&before);
        click(mainPage,mainPoint(200,423));
        QImage after(mainPage->size(),QImage::Format_ARGB32_Premultiplied);
        mainPage->render(&after);
        passed &= check(before.pixelColor(mainPoint(50,423))!=after.pixelColor(mainPoint(50,423)),
                        "Main SEL sidebar has one full-width MUTE control and no SOLO");
        click(mainPage,mainPoint(200,423));
    }
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
    click(&mixer, {40, 418});
    click(&mixer, {150, 418});
    passed &= check(selected && selected->text() == QStringLiteral("Mic 2"),
                    "SEL button switches the large-screen channel");
    channelMenu->actions()[0]->menu()->actions()[3]->trigger();
    if(mainPage){
        QImage updated(mainPage->size(),QImage::Format_ARGB32_Premultiplied);
        mainPage->render(&updated);
    }
    passed &= check(channelName && channelName->text()==QStringLiteral("Mic 2"),
                    "adding a channel keeps the persistent SEL sidebar bound to the selected channel");
    channelMenu->actions()[1]->menu()->actions()[2]->trigger();
    passed &= check(openContext(QStringLiteral("打开SEL设置")),
                    "channel context menu has an active SEL entry");
    passed &= check(openContext(QStringLiteral("复制通道")),
                    "channel context menu copies an input strip");
    click(&mixer, {150, 418});
    passed &= check(selected && selected->text() == QStringLiteral("Mic 1 副本"),
                     "copied strip keeps its channel identity and SEL selection");
    if(auto* fxTab=mixer.findChild<QPushButton*>(QStringLiteral("selTabFX")))fxTab->click();
    FxSettingsWindow* copiedFx = nullptr;
    if(pages)for (auto* child : pages->widget(2)->findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<FxSettingsWindow*>(child);
            candidate && candidate->windowTitle().contains(QStringLiteral("副本"))) {
            copiedFx = candidate;
            break;
        }
    auto* copiedToggle = copiedFx ? dynamic_cast<Controls::ToggleButton*>(
        copiedFx->findChild<QWidget*>(QStringLiteral("fxChainToggle"))) : nullptr;
    auto* copiedList = copiedFx ? copiedFx->findChild<QListWidget*>() : nullptr;
    passed &= check(copiedToggle && copiedToggle->isChecked() && copiedList && copiedList->count() == 1,
                     "copied strip opens its own FX chain in SEL and preserves its switch");
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
    mixer.resize(1920, 1150);
    QCoreApplication::processEvents();
    QImage fullSizeBefore(mixer.size(), QImage::Format_ARGB32_Premultiplied);
    mixer.render(&fullSizeBefore);
    const auto mixButtonPixel = fullSizeBefore.pixelColor(72, 585);
    drag(&mixer, {61, 800}, {61, 647});
    QImage fullSizeAfter(mixer.size(), QImage::Format_ARGB32_Premultiplied);
    mixer.render(&fullSizeAfter);
    passed &= check(fullSizeAfter.pixelColor(72, 585) == mixButtonPixel,
                    "maximum fader position leaves the MIX button uncovered at full size");
    passed &= check(fullSizeAfter.save(QStringLiteral("build/mixer-ui-full-fader-review.png")),
                    "full-size mixer review image is written to the existing build directory");
    for (int remainingInputs = 9; remainingInputs > 1; --remainingInputs)
        passed &= check(openContext(QStringLiteral("删除通道")),
                        "input strip can be removed while another input remains");
    passed &= check(!openContext(QStringLiteral("删除通道")),
                    "the last input strip cannot be removed");
    return passed;
}

bool testSelPeakReadoutCadence()
{
    using namespace WinJACKNexus;
    auto& settings = UI::ApplicationSettings::instance();
    const auto originalInterval = settings.peakValueRefreshMilliseconds();
    settings.setPeakValueRefreshMilliseconds(8);
    Common::DigitalMixerState state;
    const auto inputId = state.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    float livePeak = .5F;
    UI::MixerWindow mixer(&state, [&state](const Common::DigitalMixerState& candidate, QString*) {
        state = candidate;
        return true;
    }, [inputId, &livePeak](std::uint64_t id, bool) {
        return id == inputId ? livePeak : 0.0F;
    });
    mixer.show();
    mixer.menuBar()->actions().back()->trigger();
    auto* selMain = mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
    const auto wait = [](int milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    };
    wait(120);
    const auto first = selMain ? selMain->property("selInputMeterReadoutDb") : QVariant {};
    settings.setPeakValueRefreshMilliseconds(1000);
    livePeak = 0.0F;
    wait(120);
    const auto held = selMain ? selMain->property("selInputMeterReadoutDb") : QVariant {};
    settings.setPeakValueRefreshMilliseconds(8);
    wait(120);
    const auto updated = selMain ? selMain->property("selInputMeterReadoutDb") : QVariant {};
    settings.setPeakValueRefreshMilliseconds(originalInterval);
    return check(first.isValid() && first.toDouble() > -40.0
                     && held.isValid() && std::abs(held.toDouble() - first.toDouble()) < .001
                     && updated.isValid() && updated.toDouble() < first.toDouble() - 5.0,
                 "SEL peak numeric readout follows global millisecond interval while live level changes");
}

bool testPeakHoldSurvivesFrameRateChange()
{
    using namespace WinJACKNexus::UI;
    auto& settings = ApplicationSettings::instance();
    const auto originalRate = settings.frameRate();
    const auto originalHold = settings.peakHoldSeconds();
    settings.setFrameRate(30);
    settings.setPeakHoldSeconds(.25);
    PeakMeterWidget meter;
    meter.show();
    meter.setValue(0.0F);
    meter.setValue(-70.0F);
    const auto wait = [](int milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    };
    wait(60);
    settings.setFrameRate(120);
    wait(120);
    const auto heldPeak = meter.peakValue();
    wait(150);
    const auto decayedPeak = meter.peakValue();
    settings.setPeakHoldSeconds(originalHold);
    settings.setFrameRate(originalRate);
    return check(std::abs(heldPeak) < .1F && decayedPeak < -1.0F,
                 "peak hold lasts the configured seconds across a 30 to 120 FPS change");
}

bool testBoundMixerAuxState()
{
    using namespace WinJACKNexus;
    Common::DigitalMixerState state;
    state.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    state.add(Common::MixerStripKind::aux, Common::MixerLayout::stereo);
    state.add(Common::MixerStripKind::aux, Common::MixerLayout::stereo);
    int commits = 0;
    float liveMainPeak = .5F;
    UI::MixerWindow mixer(&state, [&state, &commits](const Common::DigitalMixerState& candidate, QString*) {
        state = candidate;
        ++commits;
        return true;
    }, [&state, &liveMainPeak](std::uint64_t id, bool) {
        return id == state.main().id ? liveMainPeak : 0.0F;
    });
    mixer.show();
    QCoreApplication::processEvents();
    bool passed = check(std::abs(mixer.mainInputLevelDb() + 6.0206) < .01,
                        "bound MAIN meter reads live audio peak");
    const auto inputId = state.inputs()[0].id;
    const QPoint faderReadout(61, 685);
    const auto readoutGlobal = mixer.mapToGlobal(faderReadout);
    QMouseEvent readoutPress(QEvent::MouseButtonPress, QPointF(faderReadout),
                             QPointF(readoutGlobal), Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&mixer, &readoutPress);
    auto* faderEditor = mixer.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    passed &= check(faderEditor && faderEditor->isVisible(),
                    "mixer fader readout opens the shared numeric editor");
    if (faderEditor) {
        faderEditor->setText(QStringLiteral("-7.5"));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(faderEditor, &enter);
        const auto* strip = state.strip(inputId);
        passed &= check(strip && std::abs(strip->faderPosition
                             - (12.0 + 7.5) / 84.0 * .95) < .001,
                        "mixer exact fader value commits to the channel model");
    }
    const QPoint faderTrack(61, 550);
    const auto trackGlobal = mixer.mapToGlobal(faderTrack);
    QMouseEvent faderDouble(QEvent::MouseButtonDblClick, QPointF(faderTrack),
                            QPointF(trackGlobal), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&mixer, &faderDouble);
    const auto* resetStrip = state.strip(inputId);
    passed &= check(resetStrip && std::abs(resetStrip->faderPosition
                         - 12.0 / 84.0 * .95) < .001,
                    "mixer fader double-click restores 0 dB unity");
    liveMainPeak = 0.0F;
    passed &= check(mixer.mainOutputLevelDb() <= -72.0,
                    "bound MAIN meter clears when input disconnects");
    mixer.menuBar()->actions().back()->trigger();
    auto* selMain=mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
    passed &= check(selMain!=nullptr,"bound mixer opens the SEL Main control page");
    if(selMain) {
        const qreal scale=std::min(selMain->width()/1394.0,selMain->height()/813.0);
        const QPoint point(qRound(180*scale),qRound((selMain->height()-813*scale)/2+376*scale));
        const auto global=selMain->mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(global),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(selMain,&press);
        auto* gainEditor=selMain->findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
        passed &= check(gainEditor&&gainEditor->isVisible(),
                        "SEL input gain readout opens the shared numeric editor");
        if(gainEditor) {
            gainEditor->setText(QStringLiteral("-4.5"));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(gainEditor,&enter);
            passed &= check(std::abs(state.inputs()[0].inputGainDb+4.5)<.001,
                            "SEL exact input gain commits to the mixer model");
        }
    }
    if (auto* tab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"))) tab->click();
    UI::AuxSettingsWindow* aux = nullptr;
    for (auto* child : mixer.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<UI::AuxSettingsWindow*>(child)) { aux = candidate; break; }
    passed &= check(aux && aux->selectedBusId() == 1 && !aux->chainEnabled(),
                        "bound CH opens AUX details with model routes disabled");
    if (!aux) return false;
    const auto clickAux = [aux](int x, int y) {
        const auto scale = std::min(aux->width() / 1360.0, aux->height() / 813.0);
        const QPoint point(qRound((aux->width() - 1360 * scale) / 2 + x * scale),
                           qRound((aux->height() - 813 * scale) / 2 + y * scale));
        const auto global = aux->mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(aux, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), QPointF(global),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(aux, &release);
    };
    clickAux(222, 30);
    passed &= check(aux->chainEnabled() && state.inputs()[0].auxSendsEnabled
                        && state.inputs()[0].sends.empty(),
                    "bound AUX master switch commits without creating unintended routes");
    clickAux(48, 98);
    passed &= check(state.inputs()[0].sends.size() == 1 && state.inputs()[0].sends[0].enabled
                        && state.inputs()[0].sends[0].targetId == state.auxiliaries()[0].id,
                    "bound AUX target switch commits a route by stable model ID");
    aux->addBus(Common::AuxChannelLayout::mono);
    passed &= check(state.auxiliaries().size() == 3 && commits >= 3,
                    "AUX detail add commits a new model bus");
    const QPoint auxSelect(580, 418);
    const auto globalSelect = mixer.mapToGlobal(auxSelect);
    QMouseEvent selectPress(QEvent::MouseButtonPress, QPointF(auxSelect), QPointF(globalSelect),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&mixer, &selectPress);
    QMouseEvent selectRelease(QEvent::MouseButtonRelease, QPointF(auxSelect), QPointF(globalSelect),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&mixer, &selectRelease);
    passed &= check(aux->selectedBusId() == 2, "bound AUX source offers the other AUX as its first target");
    clickAux(222, 30);
    clickAux(48, 98);
    passed &= check(!state.auxiliaries()[0].sends.empty()
                        && state.auxiliaries()[0].sends[0].targetId == state.auxiliaries()[1].id,
                    "bound AUX1 can route to AUX2 without a self-send");
    if (auto* fxTab = mixer.findChild<QPushButton*>(QStringLiteral("selTabFX"))) fxTab->click();
    UI::FxSettingsWindow* fx = nullptr;
    for (auto* child : mixer.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<UI::FxSettingsWindow*>(child)) { fx = candidate; break; }
    auto* power = fx ? dynamic_cast<UI::Controls::ToggleButton*>(
        fx->findChild<QWidget*>(QStringLiteral("fxChainToggle"))) : nullptr;
    passed &= check(fx && power && power->isChecked()
                        && state.auxiliaries()[0].effects.size() == 1
                        && state.auxiliaries()[0].effects.front().moduleId == QStringLiteral("parametric-eq"),
                    "new AUX starts with an active Parametric EQ chain");
    if (power) {
        power->click();
        passed &= check(!state.auxiliaries()[0].fxChainEnabled,
                        "bound FX master switch bypasses the selected strip");
        power->click();
        passed &= check(state.auxiliaries()[0].fxChainEnabled,
                        "bound FX master switch restores the selected strip");
    }
    if (fx) {
        QPushButton* addEffect = nullptr;
        for (auto* button : fx->findChildren<QPushButton*>())
            if (button->text().contains(QStringLiteral("添加模块"))) { addEffect = button; break; }
        if (addEffect) {
            QTimer::singleShot(0, fx, [] {
                if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    if (!menu->actions().isEmpty()) menu->actions().first()->trigger();
                    menu->close();
                }
            });
            addEffect->click();
        }
        passed &= check(addEffect && state.auxiliaries()[0].effects.size() == 2
                            && state.auxiliaries()[0].effects.back().moduleId == QStringLiteral("gate")
                            && !state.auxiliaries()[0].effects.back().parameters.value(
                                QStringLiteral("snapshot")).toString().isEmpty(),
                        "bound FX addition commits full eight-channel parameters");
    }
    return passed;
}

bool testMeterValuePaintFollowsFrameTimer()
{
    ProbeMeter meter;
    meter.resize(80, 190);
    meter.show();
    meter.stopAnimation();
    for (int attempt = 0; attempt < 20 && meter.paintCount == 0; ++attempt) {
        QCoreApplication::processEvents();
        QThread::msleep(3);
    }
    const auto beforeValue = meter.paintCount;
    meter.setValue(0.8F);
    QCoreApplication::processEvents();
    const bool inputDidNotPaint = meter.paintCount == beforeValue;
    const auto beforeFrame = meter.frameNumber();
    meter.startAnimation();
    for (int attempt = 0; attempt < 40 && meter.frameNumber() == beforeFrame; ++attempt) {
        QThread::msleep(3);
        QCoreApplication::processEvents();
    }
    return check(beforeValue > 0 && inputDidNotPaint && meter.frameNumber() > beforeFrame,
                 "meter input waits for the application frame timer to repaint");
}

bool testMeterDecayUsesElapsedTime()
{
    using namespace WinJACKNexus::UI;
    auto& settings = ApplicationSettings::instance();
    const auto previousHold = settings.peakHoldSeconds();
    settings.setPeakHoldSeconds(0.0);
    ProbeMeter meter;
    meter.stopAnimation();
    meter.setValue(1.0F);
    meter.advanceForTest();
    QThread::msleep(75);
    meter.advanceForTest();
    const bool meterDecayed = meter.peakValue() < 0.96F;

    MeterLevelBar bar;
    bar.setValue(-30.0F);
    bar.setPeakValue(6.0F);
    bar.setPeakValue(-30.0F);
    bar.advanceAnimation();
    QThread::msleep(75);
    bar.advanceAnimation();
    const bool barDecayed = bar.peakValue().has_value() && *bar.peakValue() < 4.0F;
    settings.setPeakHoldSeconds(previousHold);
    return check(meterDecayed && barDecayed,
                 "meter and level-bar peak decay follows elapsed time after a delayed frame");
}

bool testLivePolarHostPages()
{
    using namespace WinJACKNexus;
    bool passed = true;
    const auto hasOutputCurve = [](const QImage& image) {
        int colored = 0;
        for (int y = image.height() * 22 / 100; y < image.height() * 61 / 100; ++y)
            for (int x = image.width() * 32 / 100; x < image.width() * 66 / 100; ++x) {
                const auto color = image.pixelColor(x, y);
                colored += color.red() > 190 && color.green() > 90
                    && color.green() < 210 && color.blue() < 145;
            }
        return colored > 40;
    };
    for (const auto layout : {Common::MixerLayout::mono, Common::MixerLayout::stereo,
                              Common::MixerLayout::surround51, Common::MixerLayout::surround71}) {
        Common::DigitalMixerState state;
        const auto source = state.add(Common::MixerStripKind::input, layout);
        const auto target = state.add(Common::MixerStripKind::aux, layout);
        state.strip(source)->auxSendsEnabled = true;
        state.strip(source)->faderPosition = .95 * 12.0 / 84.0;
        state.strip(source)->mainPanner.mode = Common::MixerPanMode::spatial;
        state.strip(source)->mainPanner.showPolarCurves = true;
        auto send = Common::MixerAuxSend {target, true, true,
            Common::defaultMixerPanner(layout, layout)};
        send.panner.mode = Common::MixerPanMode::spatial;
        send.panner.showPolarCurves = true;
        passed &= check(state.setSend(source, send), "polar UI has an enabled surround AUX route");
        const auto channels = static_cast<std::size_t>(static_cast<int>(layout));
        std::array<std::array<float, 960>, 8> inputData {};
        std::array<std::array<float, 960>, 10> outputData {};
        std::array<const float*, 8> inputs {};
        std::array<float*, 10> outputs {};
        inputData[channels > 2 ? 2 : 0].fill(.5F);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            inputs[channel] = inputData[channel].data();
            outputs[channel] = outputData[channel].data();
        }
        outputs[channels] = outputData[channels].data();
        outputs[channels + 1] = outputData[channels + 1].data();
        Common::DigitalMixerProcessor processor;
        Common::MixerPolarSnapshot mainSnapshot, auxSnapshot;
        passed &= check(processor.configure(state, 960)
            && processor.process(std::span(inputs.data(), channels),
                                 std::span(outputs.data(), channels + 2), 960)
            && processor.readPolar(source, state.main().id, mainSnapshot)
            && processor.readPolar(source, target, auxSnapshot),
            "real 5.1/7.1 processor frames reach both polar UI callbacks");
        bool mainPending = true;
        bool auxPending = true;
        UI::MixerWindow mixer(&state, [&state](const Common::DigitalMixerState& next, QString*) {
            state = next;
            return true;
        }, [](std::uint64_t, bool) { return .5F; },
        [&](std::uint64_t from, std::uint64_t to, Common::MixerPolarSnapshot& snapshot) {
            if (!mainPending || from != source || to != state.main().id) return false;
            snapshot = mainSnapshot;
            mainPending = false;
            return true;
        });
        mixer.show();
        mixer.menuBar()->actions().back()->trigger();
        auto pause = [](int milliseconds) {
            QEventLoop loop;
            QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
            loop.exec();
        };
        const auto waitFor = [&pause](const auto& ready) {
            for (int attempt = 0; attempt < 40 && !ready(); ++attempt) pause(50);
            return ready();
        };
        auto* mainPage = mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
        passed &= check(mainPage && waitFor([&] { return mainPage->property("selPolarDataValid").toBool(); }),
            "SEL Main displays a contour from real branch samples");
        if (mainPage) {
            QImage frame(mainPage->size(), QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            mainPage->render(&frame);
            passed &= check(hasOutputCurve(frame),
                "SEL Main renders the measured output contour for this layout");
            passed &= check(frame.save(QStringLiteral("build/m6-sel-main-polar-%1.png").arg(channels)),
                "SEL Main polar evidence image saved");
        }
        if (auto* tab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"))) tab->click();
        UI::AuxSettingsWindow* aux = nullptr;
        for (auto* child : mixer.findChildren<QWidget*>())
            if (auto* candidate = dynamic_cast<UI::AuxSettingsWindow*>(child)) { aux = candidate; break; }
        if (aux) aux->setPolarReadCallback([&](std::uint32_t bus, Common::MixerPolarSnapshot& snapshot) {
            if (!auxPending || bus != 1) return false;
            snapshot = auxSnapshot;
            auxPending = false;
            return true;
        });
        passed &= check(aux && waitFor([&] { return aux->property("auxPolarDataValid").toBool(); }),
            "AUX detail displays a contour from real branch samples");
        if (aux) {
            QImage frame(aux->size(), QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            aux->render(&frame);
            passed &= check(hasOutputCurve(frame),
                "AUX detail renders the measured output contour for this layout");
            passed &= check(frame.save(QStringLiteral("build/m6-aux-polar-%1.png").arg(channels)),
                "AUX detail polar evidence image saved");
        }
        passed &= check(mainPage && aux && waitFor([&] {
            return !mainPage->property("selPolarDataValid").toBool()
                && !aux->property("auxPolarDataValid").toBool();
        }),
            "both host pages clear stale curves after input stops");
    }
    return passed;
}

bool testPolarLegendPlacement()
{
    WinJACKNexus::Common::MixerPolarContour contour;
    contour.valid = true;
    contour.inputOverrange = true;
    contour.outputOverrange = true;
    const auto isLegendColor = [](QColor color) {
        return (color.red() > 20 && color.green() > 145
            && color.blue() > 180) || (color.red() > 190
            && color.green() > 90 && color.blue() < 145);
    };
    bool passed = true;
    for (const auto& placement : {
             std::pair {QRectF(330, 95, 616, 448), QPointF(638, 319)},
             std::pair {QRectF(420, 95, 531, 448), QPointF(686, 319)} }) {
        QImage image(1000, 550, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor("#102235"));
        QPainter painter(&image);
        WinJACKNexus::UI::drawMixerPolarCurves(painter, placement.first,
            placement.second, 190, contour, true, true);
        painter.end();
        const QRect expected(qRound(placement.first.right()) - 108,
                             qRound(placement.first.top()) + 8, 100, 42);
        int legendPixels = 0;
        for (int y = expected.top(); y <= expected.bottom(); ++y)
            for (int x = expected.left(); x <= expected.right(); ++x)
                legendPixels += isLegendColor(image.pixelColor(x, y));
        const auto nearest = QPointF(std::clamp(placement.second.x(), qreal(expected.left()), qreal(expected.right())),
                                     std::clamp(placement.second.y(), qreal(expected.top()), qreal(expected.bottom())));
        const auto clearance = std::hypot(nearest.x() - placement.second.x(),
                                          nearest.y() - placement.second.y()) - 190.0;
        passed &= check(legendPixels > 30 && clearance >= 32.0
                            && image.pixelColor(expected.left() + 20, expected.top()) == QColor("#385773"),
            "polar legend occupies the upper-right whitespace with at least 32px dial clearance");
    }
    return passed;
}

bool testPolarScenarioScreenshots()
{
    using namespace WinJACKNexus;
    bool passed = true;
    constexpr double unityFader = .95 * 12.0 / 84.0;
    for (const auto layout : {Common::MixerLayout::surround51, Common::MixerLayout::surround71}) {
        const auto channels = static_cast<std::size_t>(static_cast<int>(layout));
        for (const auto scenario : {"unit", "center", "minus6", "wide", "lfe", "cancel"}) {
            Common::DigitalMixerState state;
            const auto source = state.add(Common::MixerStripKind::input, layout);
            const auto target = state.add(Common::MixerStripKind::aux, layout);
            auto* sourceStrip = state.strip(source);
            sourceStrip->auxSendsEnabled = true;
            sourceStrip->faderPosition = unityFader;
            auto send = Common::MixerAuxSend {target, true, true,
                Common::defaultMixerPanner(layout, layout)};
            std::array<std::array<float, 960>, 8> inputData {};
            if (std::string_view(scenario) == "unit") {
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    inputData[channel].fill(.25F);
                    send.panner.points[channel].levelDb = 0.0;
                    send.panner.points[channel].influenceRadius = .1;
                    sourceStrip->mainPanner.points[channel].levelDb = 0.0;
                    sourceStrip->mainPanner.points[channel].influenceRadius = .1;
                }
            } else if (std::string_view(scenario) == "lfe") {
                inputData[3].fill(.5F);
            } else if (std::string_view(scenario) == "cancel") {
                inputData[0].fill(.5F);
                inputData[1].fill(-.5F);
                for (auto* panner : {&send.panner, &sourceStrip->mainPanner}) {
                    panner->points[0].x = panner->points[1].x = 0.0;
                    panner->points[0].y = panner->points[1].y = -1.0;
                    panner->points[0].influenceRadius = panner->points[1].influenceRadius = .75;
                }
            } else {
                inputData[2].fill(.5F);
                if (std::string_view(scenario) == "center") {
                    send.panner.points[2].levelDb = 0.0;
                    sourceStrip->mainPanner.points[2].levelDb = 0.0;
                } else if (std::string_view(scenario) == "wide") {
                    send.panner.points[2].levelDb = 0.0;
                    send.panner.points[2].influenceRadius = .75;
                    sourceStrip->mainPanner.points[2].levelDb = 0.0;
                    sourceStrip->mainPanner.points[2].influenceRadius = .75;
                }
            }
            passed &= check(state.setSend(source, send), "polar scenario sends are valid");
            std::array<std::array<float, 960>, 10> outputData {};
            std::array<const float*, 8> inputs {};
            std::array<float*, 10> outputs {};
            for (std::size_t channel = 0; channel < channels; ++channel) {
                inputs[channel] = inputData[channel].data();
                outputs[channel] = outputData[channel].data();
            }
            outputs[channels] = outputData[channels].data();
            outputs[channels + 1] = outputData[channels + 1].data();
            Common::DigitalMixerProcessor processor;
            Common::MixerPolarSnapshot mainSnapshot, auxSnapshot;
            passed &= check(processor.configure(state, 960)
                && processor.process(std::span(inputs.data(), channels),
                                     std::span(outputs.data(), channels + 2), 960)
                && processor.readPolar(source, state.main().id, mainSnapshot)
                && processor.readPolar(source, target, auxSnapshot),
                "both page snapshots come from one real processed scenario");
            bool mainPending = true;
            bool auxPending = true;
            UI::MixerWindow mixer(&state, [&state](const Common::DigitalMixerState& next, QString*) {
                state = next;
                return true;
            }, [](std::uint64_t, bool) { return .5F; },
            [&](std::uint64_t from, std::uint64_t to, Common::MixerPolarSnapshot& snapshot) {
                if (!mainPending || from != source || to != state.main().id) return false;
                snapshot = mainSnapshot;
                mainPending = false;
                return true;
            });
            mixer.show();
            mixer.menuBar()->actions().back()->trigger();
            auto waitFor = [](const auto& ready) {
                for (int attempt = 0; attempt < 40 && !ready(); ++attempt) {
                    QEventLoop loop;
                    QTimer::singleShot(50, &loop, &QEventLoop::quit);
                    loop.exec();
                }
                return ready();
            };
            auto* mainPage = mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
            passed &= check(mainPage && waitFor([&] {
                return mainPage->property("selPolarDataValid").toBool();
            }), "SEL Main accepted measured scenario contour");
            const auto stem = QStringLiteral("build/m6-%1-%2").arg(channels).arg(scenario);
            if (mainPage) {
                QImage frame(mainPage->size(), QImage::Format_ARGB32_Premultiplied);
                frame.fill(Qt::transparent);
                mainPage->render(&frame);
                passed &= check(frame.save(stem + QStringLiteral("-sel.png")),
                    "SEL Main scenario screenshot saved");
            }
            if (auto* tab = mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"))) tab->click();
            UI::AuxSettingsWindow* aux = nullptr;
            for (auto* child : mixer.findChildren<QWidget*>())
                if (auto* candidate = dynamic_cast<UI::AuxSettingsWindow*>(child)) { aux = candidate; break; }
            if (aux) aux->setPolarReadCallback([&](std::uint32_t bus, Common::MixerPolarSnapshot& snapshot) {
                if (!auxPending || bus != 1) return false;
                snapshot = auxSnapshot;
                auxPending = false;
                return true;
            });
            passed &= check(aux && waitFor([&] {
                return aux->property("auxPolarDataValid").toBool();
            }), "AUX detail accepted measured scenario contour");
            if (aux) {
                QImage frame(aux->size(), QImage::Format_ARGB32_Premultiplied);
                frame.fill(Qt::transparent);
                aux->render(&frame);
                passed &= check(frame.save(stem + QStringLiteral("-aux.png")),
                    "AUX scenario screenshot saved");
            }
        }
    }
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
    auto& settings = WinJACKNexus::UI::ApplicationSettings::instance();
    const auto originalHold = settings.peakHoldSeconds();
    settings.setPeakHoldSeconds(0.0);
    FxLevelMeterWidget fallingMeter;
    fallingMeter.show();
    fallingMeter.setLevels(12, 12);
    fallingMeter.setLevels(-72, -72);
    QEventLoop loop;
    QTimer::singleShot(80, &loop, &QEventLoop::quit);
    loop.exec();
    passed &= check(fallingMeter.peakDb(0) < 12 && fallingMeter.peakDb(1) < 12,
                    "FX meters draw a peak marker that decays after the global hold time");
    settings.setPeakHoldSeconds(originalHold);
    return passed;
}

bool testReusableFxControls()
{
    using namespace WinJACKNexus::UI::Controls;
    ControlRegistry registry;
    QWidget owner;
    owner.resize(240, 100);
    owner.show();
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
    FaderStyle faderStyle;
    faderStyle.accent = colors.accent;
    faderStyle.thumbWidth = 30;
    faderStyle.thumbHeight = 24;
    drawFader(painter, {192,65,6,105}, .25, faderStyle);
    drawToggle(painter, {155,75,55,30}, true, colors.accent, colors.border);
    drawChart(painter, {235,65,220,120}, 4, 3, colors);
    drawLevelMeter(painter, {20,205,220,20}, -6);
    drawCardLabel(painter, {255,205,200,55}, QStringLiteral("Graphic EQ"), QStringLiteral("▥"), colors);
    ButtonStyle buttonStyle;
    buttonStyle.activeFill = QColor("#27659a");
    drawStyledButton(painter, {300,275,130,30}, QStringLiteral("Selected"), true, buttonStyle);
    painter.end();
    passed &= check(imageHasVisiblePixels(surface), "shared controls paint with custom accent");
    passed &= check(surface.pixelColor(185,215) == QColor("#f7fbff")
                        && surface.pixelColor(305,280) == QColor("#27659a"),
                    "shared default peak meter draws a marker and styled buttons use configured fills");
    passed &= check(surface.pixelColor(180, 91).alpha() > 0
                        && surface.pixelColor(180, 150).alpha() == 0,
                    "shared fader places its thumb at the requested position");
    QImage configured(160, 100, QImage::Format_ARGB32_Premultiplied);
    configured.fill(Qt::transparent);
    QPainter configuredPainter(&configured);
    MeterStyle meterStyle;
    meterStyle.minimumDb = -40;
    meterStyle.maximumDb = 10;
    meterStyle.zones = {{0, QColor("#12ab34"), QColor("#102030")},
                        {10, QColor("#e5b619"), QColor("#302010")}};
    meterStyle.peakColor = QColor("#f0f0f0");
    meterStyle.showPeak = true;
    drawMeter(configuredPainter, {10, 10, 140, 18}, -10, 5.0, meterStyle);
    std::array<float, 360> radii;
    radii.fill(1.0F);
    CurveStyle curveStyle;
    curveStyle.stroke = QColor("#a012d0");
    curveStyle.minimum = 0;
    curveStyle.maximum = 2;
    drawPolarCurve(configuredPainter, {80, 68}, 45, radii, curveStyle);
    configuredPainter.end();
    passed &= check(imageContainsColor(configured, QColor("#12ab34"))
                        && imageContainsColor(configured, QColor("#a012d0"))
                        && imageContainsColor(configured, QColor("#f0f0f0")),
                    "shared meter and curve controls honor colors, ranges and peak markers");
    QImage topTicks(160, 42, QImage::Format_ARGB32_Premultiplied);
    topTicks.fill(Qt::transparent);
    QPainter tickPainter(&topTicks);
    meterStyle.ticks={{-72,QStringLiteral("-72")},{0,QStringLiteral("0")},
                      {12,QStringLiteral("+12")}};
    meterStyle.ticksAbove=true;
    meterStyle.tickBandPixels=24;
    meterStyle.labelColor=QColor("#d43af0");
    drawMeter(tickPainter,{10,5,140,33},-10,5.0,meterStyle);
    tickPainter.end();
    bool topTickVisible=false;
    for(int y=5;y<29&&!topTickVisible;++y)
        for(int x=0;x<topTicks.width();++x)
            if(topTicks.pixelColor(x,y)==meterStyle.labelColor){topTickVisible=true;break;}
    passed &= check(topTickVisible,"shared meter places labeled ticks above its horizontal track");
    QImage reduction(64, 110, QImage::Format_ARGB32_Premultiplied);
    reduction.fill(Qt::transparent);
    QPainter reductionPainter(&reduction);
    MeterStyle reductionStyle;
    reductionStyle.minimumDb = -60;
    reductionStyle.maximumDb = 0;
    reductionStyle.fillOriginDb = 0;
    reductionStyle.vertical = true;
    reductionStyle.zones = {{0, QColor("#ef3748"), QColor("#242c39")}};
    drawMeter(reductionPainter, {8, 10, 12, 90}, -30, std::nullopt, reductionStyle);
    reductionStyle.ticksLeft = true;
    reductionStyle.tickBandPixels = 34;
    reductionStyle.labelColor = QColor("#9aada5");
    reductionStyle.ticks = {{0, QStringLiteral("0")}, {-30, QStringLiteral("-30")},
                            {-60, QStringLiteral("-60")}};
    drawMeterScale(reductionPainter, {24, 10, 34, 90}, reductionStyle);
    reductionPainter.end();
    passed &= check(reduction.pixelColor(14, 20) == QColor("#ef3748")
                        && reduction.pixelColor(14, 88) == QColor("#242c39")
                        && imageContainsColor(reduction, reductionStyle.labelColor),
                    "shared meter fills from zero toward reduction and draws a separate scale");
    QImage curveImage(100, 80, QImage::Format_ARGB32_Premultiplied);
    curveImage.fill(Qt::transparent);
    QPainter curvePainter(&curveImage);
    QPainterPath strokePath;
    strokePath.moveTo(10, 20);
    strokePath.lineTo(90, 20);
    QPainterPath fillPath;
    fillPath.addRect(10, 20, 80, 40);
    CurveStyle sharedCurve;
    sharedCurve.stroke = QColor("#ff4050");
    sharedCurve.fill = QColor("#2040e0");
    sharedCurve.showFill = true;
    drawCurvePath(curvePainter, strokePath, sharedCurve, &fillPath);
    curvePainter.end();
    passed &= check(curveImage.pixelColor(50, 20) == sharedCurve.stroke
                        && curveImage.pixelColor(50, 40) == sharedCurve.fill,
                    "shared curve painter separates the fill from the unfilled stroke");
    QImage gridImage(100, 80, QImage::Format_ARGB32_Premultiplied);
    gridImage.fill(Qt::transparent);
    QPainter gridPainter(&gridImage);
    ChartGridStyle gridStyle;
    gridStyle.background=QColor("#102030");
    gridStyle.grid=QColor("#a0b0c0");
    gridStyle.verticalFractions={0.25};
    gridStyle.horizontalFractions={0.75};
    drawChartGrid(gridPainter,{10,10,80,60},gridStyle);
    gridPainter.end();
    passed &= check(gridImage.pixelColor(30,25)==gridStyle.grid
                        && gridImage.pixelColor(50,55)==gridStyle.grid
                        && gridImage.pixelColor(50,30)==gridStyle.background
                        && gridImage.pixelColor(5,5).alpha()==0,
                    "shared chart grid honors explicit positions, colors and clipping");
    QImage gradientImage(100, 80, QImage::Format_ARGB32_Premultiplied);
    gradientImage.fill(Qt::transparent);
    QPainter gradientPainter(&gradientImage);
    QLinearGradient gradient(0, 20, 0, 60);
    gradient.setColorAt(0, QColor("#40d0ff"));
    gradient.setColorAt(1, QColor("#103050"));
    sharedCurve.fillBrush = QBrush(gradient);
    sharedCurve.showStroke = false;
    drawCurvePath(gradientPainter, strokePath, sharedCurve, &fillPath);
    gradientPainter.end();
    passed &= check(gradientImage.pixelColor(50, 25) != gradientImage.pixelColor(50, 55)
                        && gradientImage.pixelColor(50, 20) != sharedCurve.stroke,
                    "shared curve painter supports a gradient fill without a stroke");
    auto accentPixels = [&](qreal pan) {
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter panPainter(&image);
        drawPanKnob(panPainter, {100, 100}, pan, 70, colors);
        panPainter.end();
        int left = 0;
        int right = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == colors.accent) {
                    (x < 100 ? left : right)++;
                }
            }
        }
        return std::pair<int, int>{left, right};
    };
    const auto [centerLeft, centerRight] = accentPixels(0);
    const auto [leftLeft, leftRight] = accentPixels(-1);
    const auto [rightLeft, rightRight] = accentPixels(1);
    passed &= check(centerLeft == 0 && centerRight == 0
                        && leftLeft > 100 && leftRight < leftLeft / 10
                        && rightRight > 100 && rightLeft < rightRight / 10,
                    "shared Pan knob accent extends from center toward the selected side");
    double edited = 0;
    auto* editor = editNumericValue(&owner, {10,10,100,25}, -2.0,
                                    {-72,12,0,1}, [&edited](double value) { edited=value; });
    passed &= check(editor && editor->isVisible() && editor->selectedText()==QStringLiteral("-2.0"),
                    "shared numeric readout opens an inline editor with its value selected");
    if(editor) {
        editor->setText(QStringLiteral("-6.5"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);
        passed &= check(edited==-6.5 && !editor->isVisible(),
                        "Enter commits an exact numeric value and restores the readout");
    }
    auto* cancelled = editNumericValue(&owner, {10,10,100,25}, -6.5,
                                       {-72,12,0,1}, [&edited](double value) { edited=value; });
    if(cancelled) {
        cancelled->setText(QStringLiteral("3.0"));
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(cancelled,&escape);
    }
    passed &= check(edited==-6.5 && cancelled && !cancelled->isVisible(),
                    "Escape closes the editor without changing its value");
    resetNumericValue({-72,12,0,1},[&edited](double value) { edited=value; });
    passed &= check(edited==0,"shared double-click reset applies the control default");
    MeterBallistics meter60;
    MeterBallistics meter120;
    for(int i=0;i<60;++i) meter60.advance(-6,1.0/60,0.1);
    for(int i=0;i<120;++i) meter120.advance(-6,1.0/120,0.1);
    passed &= check(std::abs(meter60.levelDb()-meter120.levelDb())<.01
                    && meter60.peakDb()==-6 && meter120.peakDb()==-6,
                    "shared meter smoothing is independent of the chosen frame rate");
    for(int i=0;i<6;++i) meter60.advance(-72,1.0/60,0.1);
    passed &= check(meter60.peakDb()==-6,"peak marker remains during its hold interval");
    for(int i=0;i<60;++i) meter60.advance(-72,1.0/60,0.1);
    passed &= check(meter60.peakDb()<-6,"peak marker decays after the hold interval");
    return passed;
}

}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (argc == 2 && std::string_view(argv[1]) == "bound-aux")
        return testBoundMixerAuxState() ? 0 : 1;
    bool passed = true;
    passed &= testThemePalette();
    passed &= testApplicationSettings();
    passed &= testThemeManager(application);
    passed &= testSettingsControlThemePreview();
    passed &= testSharedContainerControls();
    passed &= testMixerEmptyGroupHeaders();
    passed &= testPaintableWidget();
    passed &= testAnimatedWidget();
    passed &= testNativeWindowUtils();
    passed &= testIcons();
    passed &= testMeterLevelBar();
    passed &= testMeterArrayFrameRate();
    passed &= testPeakHoldSurvivesFrameRateChange();
    passed &= testMeterValuePaintFollowsFrameTimer();
    passed &= testMeterDecayUsesElapsedTime();
    passed &= testMetricReadout();
    passed &= testHistoryChartAndLegend();
    passed &= testHistoryCurveDoesNotFillReferenceLine();
    passed &= testMetricStatisticsWidget();
    passed &= testCompressorGraphFill();
    passed &= testDeEsserWidget();
    passed &= testFxArchiveImportFailurePreservesState();
    passed &= testFxSettingsLoadsNativeFx();
    passed &= testMixerChannelControls();
    passed &= testSelPeakReadoutCadence();
    passed &= testBoundMixerAuxState();
    passed &= testLivePolarHostPages();
    passed &= testPolarLegendPlacement();
    passed &= testPolarScenarioScreenshots();
    passed &= testFxMeterSegments();
    passed &= testReusableFxControls();
    if (!passed)
        return 1;
    std::puts("UI tests passed");
    return 0;
}
