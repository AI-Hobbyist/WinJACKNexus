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
#include <WinJACKNexus/Meter/Mod/MeterHistoryWindow.hpp>

#include <WinJACKNexus/Meter/Mod/MeterSettingsWidget.hpp>
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QShowEvent>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace WinJACKNexus::Meter::Mod {

namespace {

struct SeriesDefinition final {
    QString id;
    QString label;
    QString unit;
    QColor color;
    WinJACKNexus::UI::HistoryChartAxis axis;
};

const std::array<SeriesDefinition, 8>& seriesDefinitions()
{
    static const std::array definitions {
        SeriesDefinition { QStringLiteral("peak"), QStringLiteral("峰值（Peak）"), QStringLiteral("dBFS"), QColor(QStringLiteral("#ff5269")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("rms"), QStringLiteral("均方根（RMS）"), QStringLiteral("dBFS"), QColor(QStringLiteral("#ff8a47")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("lfe"), QStringLiteral("LFE 峰值（LFE Peak）"), QStringLiteral("dBFS"), QColor(QStringLiteral("#b96aff")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("true-peak"), QStringLiteral("dBTP"), QStringLiteral("dBTP"), QColor(QStringLiteral("#ffd143")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("momentary"), QStringLiteral("瞬时（Momentary）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#43e88d")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("short-term"), QStringLiteral("短时（Short-term）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#48c9ff")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("integrated"), QStringLiteral("综合（Integrated）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#4b95ff")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("lra"), QStringLiteral("LRA"), QStringLiteral("LU"), QColor(QStringLiteral("#b96aff")), WinJACKNexus::UI::HistoryChartAxis::right }
    };
    return definitions;
}

std::optional<float> historyValue(const MeterHistoryPoint& point, const QString& id)
{
    if (id == QStringLiteral("peak"))
        return point.metrics.peakDbfs;
    if (id == QStringLiteral("rms"))
        return point.metrics.rmsDbfs;
    if (id == QStringLiteral("lfe"))
        return point.lfePeakDbfs;
    if (id == QStringLiteral("true-peak"))
        return point.metrics.truePeakDbtp;
    if (id == QStringLiteral("momentary"))
        return point.metrics.momentaryLufs;
    if (id == QStringLiteral("short-term"))
        return point.metrics.shortTermLufs;
    if (id == QStringLiteral("integrated"))
        return point.metrics.integratedLufs;
    if (id == QStringLiteral("lra"))
        return point.metrics.lraLu;
    return std::nullopt;
}

std::optional<double> asDouble(const std::optional<float>& value)
{
    return value.has_value() ? std::optional<double> { static_cast<double>(*value) } : std::nullopt;
}

QString elapsedText(std::int64_t elapsedMilliseconds)
{
    const auto totalSeconds = std::max<std::int64_t>(0, elapsedMilliseconds / 1000);
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QToolButton* createActionButton(QAction* action, const QString& objectName, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

}

MeterHistoryWindow::MeterHistoryWindow(MeterViewDataProvider& provider,
                                       std::function<void()> requestRefresh,
                                       std::function<void()> returnToBridge,
                                       QWidget* parent)
    : QWidget(parent)
    , provider_(provider)
    , requestRefresh_(std::move(requestRefresh))
    , returnToBridge_(std::move(returnToBridge))
{
    setObjectName(QStringLiteral("meterHistory.window"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setStyleSheet(QStringLiteral(
        "QWidget#meterHistory.window { background-color: #061523; color: #d8e9ff; }"
        "QWidget[meterHistorySurface=\"true\"] { background-color: #061523; }"
        "QFrame[meterHistoryToolbar=\"true\"] { background-color: #0a1d30; border: 1px solid #183b5a; border-radius: 6px; }"
        "QToolButton[meterHistoryControl=\"range\"], QToolButton[meterHistoryControl=\"action\"] {"
        " background-color: #102742; color: #cfe4ff; border: 1px solid #204564; border-radius: 5px; padding: 6px 12px; min-height: 24px; }"
        "QToolButton[meterHistoryControl=\"range\"]:checked { background-color: #103c68; border-color: #2f9eff; color: #ffffff; }"
        "QToolButton[meterHistoryControl=\"range\"]:hover, QToolButton[meterHistoryControl=\"action\"]:hover { border-color: #4ea9ff; background-color: #173755; }"
        "QComboBox, QLineEdit, QDoubleSpinBox, QSpinBox { background-color: #102742; color: #d8e9ff; border: 1px solid #244b6b; border-radius: 4px; padding: 4px; }"
        "QCheckBox { color: #cfe4ff; spacing: 7px; min-height: 20px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #376487; background-color: #0a1d30; }"
        "QCheckBox::indicator:checked { background-color: #2f9eff; border-color: #72bcff; }"
        "QGroupBox { color: #b8d7f6; border: 1px solid #183b5a; border-radius: 5px; margin-top: 12px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QTableWidget { background-color: #0a1d30; alternate-background-color: #0c243b; color: #cfe4ff; border: 1px solid #183b5a; gridline-color: #1a3e5e; }"
        "QHeaderView::section { background-color: #102742; color: #a8c9ec; border: 0; border-bottom: 1px solid #1c4568; padding: 4px; }"
        "QSplitter::handle { background-color: #122a42; width: 1px; }"));

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("meterHistory.splitter"));
    settingsWidget_ = new MeterSettingsWidget(provider_, [this] {
        if (requestRefresh_)
            requestRefresh_();
    }, splitter);
    splitter->addWidget(settingsWidget_);
    auto* settingsLayout = qobject_cast<QVBoxLayout*>(settingsWidget_->layout());
    auto* returnButton = new QToolButton(settingsWidget_);
    returnButton->setObjectName(QStringLiteral("meterHistory.returnButton"));
    returnButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    returnButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    returnButton->setText(QStringLiteral("返回电平表桥"));
    returnButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    returnButton->setToolTip(QStringLiteral("返回电平表桥窗口"));
    if (settingsLayout != nullptr)
        settingsLayout->insertWidget(0, returnButton);
    connect(returnButton, &QToolButton::clicked, this, [this] {
        if (returnToBridge_)
            returnToBridge_();
    });

    viewModeSelector_ = new QComboBox(settingsWidget_);
    viewModeSelector_->setObjectName(QStringLiteral("meterHistory.viewMode"));
    viewModeSelector_->setAccessibleName(QStringLiteral("历史刷新形式"));
    viewModeSelector_->addItem(QStringLiteral("滚动（实时滚动）"), static_cast<int>(MeterHistoryViewMode::realtimeScroll));
    viewModeSelector_->addItem(QStringLiteral("扫描"), static_cast<int>(MeterHistoryViewMode::scanning));

    timeWindowSelector_ = new QComboBox(settingsWidget_);
    timeWindowSelector_->setObjectName(QStringLiteral("meterHistory.timeWindow"));
    timeWindowSelector_->setAccessibleName(QStringLiteral("历史时间窗口"));
    timeWindowSelector_->addItem(QStringLiteral("10 秒"), qlonglong { 10000 });
    timeWindowSelector_->addItem(QStringLiteral("1 分钟"), qlonglong { 60000 });
    timeWindowSelector_->addItem(QStringLiteral("5 分钟"), qlonglong { 300000 });
    timeWindowSelector_->addItem(QStringLiteral("10 分钟"), qlonglong { 600000 });
    timeWindowSelector_->addItem(QStringLiteral("30 分钟"), qlonglong { 1800000 });
    timeWindowSelector_->addItem(QStringLiteral("1 小时"), qlonglong { 3600000 });

    auto* displaySettingsGroup = settingsWidget_->findChild<QGroupBox*>(QStringLiteral("meterSettings.globalGroup"));
    auto* displaySettingsLayout = displaySettingsGroup != nullptr
        ? qobject_cast<QFormLayout*>(displaySettingsGroup->layout())
        : nullptr;
    if (displaySettingsLayout != nullptr) {
        displaySettingsLayout->addRow(QStringLiteral("刷新形式"), viewModeSelector_);
        displaySettingsLayout->addRow(QStringLiteral("时间范围"), timeWindowSelector_);
    }

    auto* seriesGroup = new QGroupBox(QStringLiteral("曲线显示"), settingsWidget_);
    seriesGroup->setObjectName(QStringLiteral("meterHistory.seriesGroup"));
    auto* seriesLayout = new QVBoxLayout(seriesGroup);
    seriesLayout->setContentsMargins(8, 10, 8, 6);
    seriesLayout->setSpacing(2);
    for (std::size_t index = 0; index < seriesDefinitions().size(); ++index) {
        const auto& definition = seriesDefinitions()[index];
        auto* checkBox = new QCheckBox(definition.label, seriesGroup);
        checkBox->setObjectName(QStringLiteral("meterHistory.series.%1").arg(definition.id));
        checkBox->setAccessibleName(QStringLiteral("显示 %1 曲线").arg(definition.label));
        checkBox->setChecked(true);
        checkBox->setStyleSheet(QStringLiteral("QCheckBox { color: %1; }").arg(definition.color.name()));
        seriesVisibilityCheckBoxes_[index] = checkBox;
        seriesLayout->addWidget(checkBox);
        connect(checkBox, &QCheckBox::toggled, this, [this, id = definition.id](bool visible) {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setHistorySeriesVisible;
            command.seriesId = id;
            command.enabled = visible;
            dispatch(std::move(command));
        });
    }

    const std::array referenceDefinitions {
        std::pair { QStringLiteral("momentary"), QStringLiteral("瞬时（Momentary） -18 LUFS") },
        std::pair { QStringLiteral("short-term"), QStringLiteral("短时（Short-term） -23 LUFS") },
        std::pair { QStringLiteral("integrated"), QStringLiteral("综合（Integrated） -23 LUFS") },
        std::pair { QStringLiteral("lra"), QStringLiteral("LRA 0–20 LU") }
    };
    auto* referenceGroup = new QGroupBox(QStringLiteral("参考线"), settingsWidget_);
    referenceGroup->setObjectName(QStringLiteral("meterHistory.referenceGroup"));
    auto* referenceLayout = new QVBoxLayout(referenceGroup);
    referenceLayout->setContentsMargins(8, 10, 8, 6);
    referenceLayout->setSpacing(2);
    for (std::size_t index = 0; index < referenceDefinitions.size(); ++index) {
        const auto& [id, label] = referenceDefinitions[index];
        auto* checkBox = new QCheckBox(label, referenceGroup);
        checkBox->setObjectName(QStringLiteral("meterHistory.reference.%1").arg(id));
        checkBox->setAccessibleName(QStringLiteral("显示 %1 参考线").arg(label));
        checkBox->setChecked(true);
        referenceLineVisibility_.insert(id, true);
        referenceLineCheckBoxes_[index] = checkBox;
        referenceLayout->addWidget(checkBox);
        connect(checkBox, &QCheckBox::toggled, this, [this, id](bool visible) {
            referenceLineVisibility_.insert(id, visible);
            updateReferenceLines();
        });
    }
    if (settingsLayout != nullptr) {
        settingsLayout->insertWidget(2, seriesGroup);
        settingsLayout->insertWidget(3, referenceGroup);
    }

    auto* chartSurface = new QWidget(splitter);
    chartSurface->setObjectName(QStringLiteral("meterHistory.surface"));
    chartSurface->setProperty("meterHistorySurface", true);
    auto* rootLayout = new QVBoxLayout(chartSurface);
    rootLayout->setContentsMargins(10, 10, 10, 8);
    rootLayout->setSpacing(6);

    auto* controlsFrame = new QFrame(chartSurface);
    controlsFrame->setProperty("meterHistoryToolbar", true);
    auto* controls = new QHBoxLayout(controlsFrame);
    controls->setContentsMargins(10, 6, 10, 6);
    controls->setSpacing(6);
    controls->addWidget(new QLabel(QStringLiteral("通道"), controlsFrame));
    channelSelector_ = new QComboBox(controlsFrame);
    channelSelector_->setObjectName(QStringLiteral("meterHistory.channelSelector"));
    channelSelector_->setAccessibleName(QStringLiteral("历史图表通道"));
    channelSelector_->setToolTip(QStringLiteral("切换要查看的模拟通道历史"));
    channelSelector_->setMinimumContentsLength(14);
    channelSelector_->setEnabled(false);
    controls->addWidget(channelSelector_);
    auto* rangeButtons = new QButtonGroup(controlsFrame);
    rangeButtons->setExclusive(true);
    const std::array ranges {
        std::pair { qlonglong { 60000 }, QStringLiteral("1 分钟") },
        std::pair { qlonglong { 300000 }, QStringLiteral("5 分钟") },
        std::pair { qlonglong { 600000 }, QStringLiteral("10 分钟") },
        std::pair { qlonglong { 1800000 }, QStringLiteral("30 分钟") },
        std::pair { qlonglong { 3600000 }, QStringLiteral("1 小时") }
    };
    for (const auto& [milliseconds, label] : ranges) {
        auto* button = new QToolButton(controlsFrame);
        button->setObjectName(QStringLiteral("meterHistory.range.%1").arg(milliseconds));
        button->setProperty("meterHistoryControl", QStringLiteral("range"));
        button->setProperty("historyTimeWindow", milliseconds);
        button->setText(label);
        button->setCheckable(true);
        button->setChecked(milliseconds == 600000);
        rangeButtons->addButton(button);
        controls->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, milliseconds] {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setHistoryTimeWindow;
            command.historyTimeWindowMilliseconds = milliseconds;
            dispatch(std::move(command));
        });
    }
    auto* customRangeButton = new QToolButton(controlsFrame);
    customRangeButton->setObjectName(QStringLiteral("meterHistory.range.custom"));
    customRangeButton->setProperty("meterHistoryControl", QStringLiteral("range"));
    customRangeButton->setText(QStringLiteral("自定义..."));
    customRangeButton->setToolTip(QStringLiteral("当前模拟预览使用 10 分钟自定义窗口"));
    controls->addWidget(customRangeButton);
    connect(customRangeButton, &QToolButton::clicked, this, [this] {
        timeWindowSelector_->showPopup();
    });
    controls->addStretch(1);

    scanCursorSlider_ = new QSlider(Qt::Horizontal, chartSurface);
    scanCursorSlider_->setObjectName(QStringLiteral("meterHistory.scanCursor"));
    scanCursorSlider_->setAccessibleName(QStringLiteral("扫描游标"));
    scanCursorSlider_->setMinimumWidth(150);
    controls->addWidget(scanCursorSlider_);

    pauseAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("暂停"), this);
    pauseAction_->setObjectName(QStringLiteral("meterHistory.pauseAction"));
    pauseAction_->setCheckable(true);
    pauseAction_->setToolTip(QStringLiteral("冻结历史视图，不停止模拟测量"));
    clearAction_ = new QAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("清空数据"), this);
    clearAction_->setObjectName(QStringLiteral("meterHistory.clearAction"));
    clearAction_->setToolTip(QStringLiteral("清空模拟历史，不重置响度"));
    exportAction_ = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("导出图表"), this);
    exportAction_->setObjectName(QStringLiteral("meterHistory.exportAction"));
    exportAction_->setToolTip(QStringLiteral("准备 PNG 导出预览，不写入文件"));
    auto* pauseButton = createActionButton(pauseAction_, QStringLiteral("meterHistory.pauseButton"), controlsFrame);
    pauseButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    auto* clearButton = createActionButton(clearAction_, QStringLiteral("meterHistory.clearButton"), controlsFrame);
    clearButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    auto* chartSettingsButton = new QToolButton(controlsFrame);
    chartSettingsButton->setObjectName(QStringLiteral("meterHistory.chartSettingsButton"));
    chartSettingsButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    chartSettingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    chartSettingsButton->setText(QStringLiteral("图表设置"));
    chartSettingsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    chartSettingsButton->setToolTip(QStringLiteral("查看左侧图表设置"));
    connect(chartSettingsButton, &QToolButton::clicked, this, [this, splitter] {
        const auto totalWidth = std::max(1, splitter->width());
        splitter->setSizes({ 312, std::max(1, totalWidth - 312) });
        settingsWidget_->setFocus(Qt::OtherFocusReason);
    });
    controls->addWidget(pauseButton);
    controls->addWidget(clearButton);
    controls->addWidget(chartSettingsButton);
    rootLayout->addWidget(controlsFrame);

    chart_ = new WinJACKNexus::UI::HistoryChartWidget(chartSurface);
    chart_->setObjectName(QStringLiteral("meterHistory.chart"));
    chart_->setValueRange(-72.0, 12.0);
    chart_->setRightValueRange(0.0, 20.0);
    chart_->setVerticalAxisLabel(QStringLiteral("电平（Level） / 响度（Loudness） (dB / LUFS)"));
    chart_->setRightVerticalAxisLabel(QStringLiteral("LRA (LU)"));
    chart_->setHorizontalAxisLabel(QStringLiteral("时间 (分:秒)"));
    auto historyPalette = WinJACKNexus::UI::ThemePalette::darkRack();
    historyPalette.setColor(QStringLiteral("window-bg"), QColor(QStringLiteral("#061523")));
    historyPalette.setColor(QStringLiteral("panel-bg"), QColor(QStringLiteral("#0a1d30")));
    historyPalette.setColor(QStringLiteral("panel-raised"), QColor(QStringLiteral("#102742")));
    historyPalette.setColor(QStringLiteral("text"), QColor(QStringLiteral("#d8e9ff")));
    historyPalette.setColor(QStringLiteral("text-muted"), QColor(QStringLiteral("#88a9ce")));
    historyPalette.setColor(QStringLiteral("accent"), QColor(QStringLiteral("#3b9dff")));
    historyPalette.setColor(QStringLiteral("accent-alt"), QColor(QStringLiteral("#ffd143")));
    historyPalette.setColor(QStringLiteral("border"), QColor(QStringLiteral("#244b6b")));
    historyPalette.setColor(QStringLiteral("danger"), QColor(QStringLiteral("#ff5269")));
    historyPalette.setColor(QStringLiteral("success"), QColor(QStringLiteral("#43e88d")));
    chart_->setThemePalette(historyPalette);
    updateReferenceLines();
    rootLayout->addWidget(chart_, 1);

    legend_ = new WinJACKNexus::UI::HistoryLegendWidget(chartSurface);
    legend_->setObjectName(QStringLiteral("meterHistory.legend"));
    legend_->setThemePalette(historyPalette);
    legend_->hide();

    auto* statisticsTitle = new QLabel(QStringLiteral("统计信息（当前视图）"), chartSurface);
    statisticsTitle->setObjectName(QStringLiteral("meterHistory.statisticsTitle"));
    statisticsTitle->setStyleSheet(QStringLiteral("color: #b8d7f6; font-weight: 600; padding: 3px 0;"));
    rootLayout->addWidget(statisticsTitle);
    statistics_ = new WinJACKNexus::UI::MetricStatisticsWidget(chartSurface);
    statistics_->setObjectName(QStringLiteral("meterHistory.statistics"));
    statistics_->setPrecision(1);
    statistics_->setThemePalette(historyPalette);
    rootLayout->addWidget(statistics_);

    auto* markersGroup = new QGroupBox(QStringLiteral("标记点"), chartSurface);
    auto* markersLayout = new QVBoxLayout(markersGroup);
    markersLayout->setContentsMargins(6, 10, 6, 5);
    markerTable_ = new QTableWidget(markersGroup);
    markerTable_->setObjectName(QStringLiteral("meterHistory.markerTable"));
    markerTable_->setAccessibleName(QStringLiteral("历史标记点"));
    markerTable_->setColumnCount(10);
    markerTable_->setHorizontalHeaderLabels({ QStringLiteral("#"), QStringLiteral("时间"), QStringLiteral("峰值（Peak） (dBFS)"),
                                               QStringLiteral("均方根（RMS） (dBFS)"), QStringLiteral("dBTP (dBTP)"),
                                               QStringLiteral("瞬时（Momentary） (LUFS)"), QStringLiteral("短时（Short-term） (LUFS)"),
                                               QStringLiteral("综合（Integrated） (LUFS)"), QStringLiteral("LRA (LU)"), QStringLiteral("备注") });
    markerTable_->verticalHeader()->setVisible(false);
    markerTable_->horizontalHeader()->setStretchLastSection(true);
    for (int column = 0; column < markerTable_->columnCount() - 1; ++column)
        markerTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    markerTable_->setMinimumHeight(126);
    markersLayout->addWidget(markerTable_);
    auto* markerButtons = new QHBoxLayout;
    addMarkerAction_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("添加标记点"), this);
    addMarkerAction_->setObjectName(QStringLiteral("meterHistory.addMarkerAction"));
    removeMarkerAction_ = new QAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("删除选中"), this);
    removeMarkerAction_->setObjectName(QStringLiteral("meterHistory.removeMarkerAction"));
    jumpMarkerAction_ = new QAction(style()->standardIcon(QStyle::SP_ArrowRight), QStringLiteral("跳转到选中"), this);
    jumpMarkerAction_->setObjectName(QStringLiteral("meterHistory.jumpMarkerAction"));
    markerButtons->addStretch(1);
    for (const auto [action, objectName] : std::array {
             std::pair { addMarkerAction_, QStringLiteral("meterHistory.addMarkerButton") },
             std::pair { removeMarkerAction_, QStringLiteral("meterHistory.removeMarkerButton") },
             std::pair { jumpMarkerAction_, QStringLiteral("meterHistory.jumpMarkerButton") } }) {
        auto* button = createActionButton(action, objectName, markersGroup);
        button->setProperty("meterHistoryControl", QStringLiteral("action"));
        markerButtons->addWidget(button);
    }
    markersLayout->addLayout(markerButtons);

    auto* csvGroup = new QGroupBox(QStringLiteral("实时记录到 CSV"), settingsWidget_);
    auto* csvLayout = new QVBoxLayout(csvGroup);
    csvLayout->setContentsMargins(8, 10, 8, 6);
    csvPathLabel_ = new QLabel(csvGroup);
    csvPathLabel_->setObjectName(QStringLiteral("meterHistory.csvPath"));
    csvPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    csvPathLabel_->setWordWrap(true);
    csvStatusLabel_ = new QLabel(csvGroup);
    csvStatusLabel_->setObjectName(QStringLiteral("meterHistory.csvStatus"));
    csvStatusLabel_->setWordWrap(true);
    exportStatusLabel_ = new QLabel(csvGroup);
    exportStatusLabel_->setObjectName(QStringLiteral("meterHistory.exportStatus"));
    exportStatusLabel_->setWordWrap(true);
    recordingAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("记录响度"), this);
    recordingAction_->setObjectName(QStringLiteral("meterHistory.recordingAction"));
    recordingAction_->setCheckable(true);
    recordingAction_->setToolTip(QStringLiteral("切换模拟响度记录状态"));
    recordingCheckBox_ = new QCheckBox(QStringLiteral("启用实时记录到 CSV"), csvGroup);
    recordingCheckBox_->setObjectName(QStringLiteral("meterHistory.recordingToggle"));
    recordingCheckBox_->setAccessibleName(QStringLiteral("启用实时记录到 CSV"));
    auto* recordingButton = createActionButton(recordingAction_, QStringLiteral("meterHistory.recordingButton"), csvGroup);
    recordingButton->hide();
    auto* exportButton = createActionButton(exportAction_, QStringLiteral("meterHistory.exportButton"), csvGroup);
    exportButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    csvLayout->addWidget(recordingCheckBox_);
    csvLayout->addWidget(csvPathLabel_);
    csvLayout->addWidget(csvStatusLabel_);
    csvLayout->addWidget(exportStatusLabel_);
    csvLayout->addWidget(exportButton);
    if (settingsLayout != nullptr)
        settingsLayout->insertWidget(4, csvGroup);
    rootLayout->addWidget(markersGroup);

    splitter->addWidget(chartSurface);
    splitter->setSizes({ 312, 1048 });
    pageLayout->addWidget(splitter);

    scanTimer_ = new QTimer(this);
    scanTimer_->setInterval(100);
    connect(scanTimer_, &QTimer::timeout, this, [this] {
        if (!isVisible() || snapshot_.historyView.mode != MeterHistoryViewMode::scanning || snapshot_.historyView.paused)
            return;
        const auto timeWindow = std::max<std::int64_t>(1000, snapshot_.historyView.timeWindowMilliseconds);
        const auto latestElapsed = snapshot_.history.empty() ? 0 : snapshot_.history.back().elapsedMilliseconds;
        const auto cursor = scanCursorMilliseconds_.value_or(latestElapsed % timeWindow);
        scanCursorMilliseconds_ = (cursor + 100) % timeWindow;
        const QSignalBlocker sliderBlocker(scanCursorSlider_);
        scanCursorSlider_->setValue(static_cast<int>(*scanCursorMilliseconds_ / 100));
        updateChart();
    });

    connect(viewModeSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setHistoryViewMode;
        command.historyViewMode = static_cast<MeterHistoryViewMode>(viewModeSelector_->itemData(index).toInt());
        dispatch(std::move(command));
    });
    connect(timeWindowSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setHistoryTimeWindow;
        command.historyTimeWindowMilliseconds = timeWindowSelector_->itemData(index).toLongLong();
        dispatch(std::move(command));
    });
    connect(channelSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= static_cast<int>(availableSnapshots_.size()))
            return;
        setSnapshot(availableSnapshots_[static_cast<std::size_t>(index)]);
    });
    connect(scanCursorSlider_, &QSlider::valueChanged, this, [this](int tick) {
        scanCursorMilliseconds_ = static_cast<std::int64_t>(tick) * 100;
        updateChart();
    });
    connect(pauseAction_, &QAction::toggled, this, [this](bool paused) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setHistoryPaused;
        command.enabled = paused;
        dispatch(std::move(command));
    });
    connect(clearAction_, &QAction::triggered, this, [this] {
        if (QMessageBox::question(this,
                                  QStringLiteral("清空历史数据"),
                                  QStringLiteral("仅清空模拟历史数据，响度数值不会改变。"))
            != QMessageBox::Yes)
            return;
        MeterViewCommand command;
        command.type = MeterViewCommandType::clearHistory;
        dispatch(std::move(command));
    });
    connect(exportAction_, &QAction::triggered, this, [this] {
        exportStatusLabel_->setText(QStringLiteral("PNG 导出预览已准备好；不会写入文件。"));
    });
    connect(recordingAction_, &QAction::toggled, this, [this](bool enabled) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setRecording;
        command.enabled = enabled;
        dispatch(std::move(command));
    });
    connect(recordingCheckBox_, &QCheckBox::toggled, recordingAction_, &QAction::setChecked);
    connect(recordingAction_, &QAction::toggled, recordingCheckBox_, &QCheckBox::setChecked);
    connect(legend_, &WinJACKNexus::UI::HistoryLegendWidget::entryVisibilityChanged,
            this, [this](const QString& id, bool visible) {
                MeterViewCommand command;
                command.type = MeterViewCommandType::setHistorySeriesVisible;
                command.seriesId = id;
                command.enabled = visible;
                dispatch(std::move(command));
            });
    connect(addMarkerAction_, &QAction::triggered, this, [this] {
        MeterViewCommand command;
        command.type = MeterViewCommandType::addHistoryMarker;
        command.markerElapsedMilliseconds = scanCursorMilliseconds_.value_or(
            snapshot_.history.empty() ? 0 : snapshot_.history.back().elapsedMilliseconds);
        dispatch(std::move(command));
    });
    connect(removeMarkerAction_, &QAction::triggered, this, [this] { removeSelectedMarker(); });
    connect(jumpMarkerAction_, &QAction::triggered, this, [this] { jumpToSelectedMarker(); });
}

void MeterHistoryWindow::setSnapshots(const std::vector<MeterViewSnapshot>& snapshots, const QString& preferredInstanceId)
{
    const auto selectedInstanceId = preferredInstanceId.isEmpty() ? snapshot_.instanceId : preferredInstanceId;
    availableSnapshots_ = snapshots;

    const QSignalBlocker selectorBlocker(channelSelector_);
    channelSelector_->clear();
    channelSelector_->setEnabled(!availableSnapshots_.empty());
    int selectedIndex = -1;
    for (std::size_t index = 0; index < availableSnapshots_.size(); ++index) {
        const auto& snapshot = availableSnapshots_[index];
        channelSelector_->addItem(QStringLiteral("%1（%2）").arg(snapshot.clientName, snapshot.layoutLabel), snapshot.instanceId);
        if (snapshot.instanceId == selectedInstanceId)
            selectedIndex = static_cast<int>(index);
    }
    if (selectedIndex < 0 && !availableSnapshots_.empty())
        selectedIndex = 0;
    channelSelector_->setCurrentIndex(selectedIndex);

    if (selectedIndex >= 0) {
        setSnapshot(availableSnapshots_[static_cast<std::size_t>(selectedIndex)]);
        return;
    }

    snapshot_ = {};
    chart_->setSeries({});
    statistics_->setEntries({});
    markerTable_->clearContents();
    markerTable_->setRowCount(0);
    csvPathLabel_->clear();
    csvStatusLabel_->clear();
    exportStatusLabel_->clear();
}

void MeterHistoryWindow::setSnapshot(const MeterViewSnapshot& snapshot)
{
    snapshot_ = snapshot;
    settingsWidget_->setSnapshot(snapshot_);

    const QSignalBlocker modeBlocker(viewModeSelector_);
    viewModeSelector_->setCurrentIndex(viewModeSelector_->findData(static_cast<int>(snapshot_.historyView.mode)));
    const QSignalBlocker windowBlocker(timeWindowSelector_);
    for (int index = 0; index < timeWindowSelector_->count(); ++index) {
        if (timeWindowSelector_->itemData(index).toLongLong() == snapshot_.historyView.timeWindowMilliseconds) {
            timeWindowSelector_->setCurrentIndex(index);
            break;
        }
    }
    for (auto* rangeButton : findChildren<QToolButton*>()) {
        if (!rangeButton->property("historyTimeWindow").isValid())
            continue;
        const QSignalBlocker rangeBlocker(rangeButton);
        rangeButton->setChecked(rangeButton->property("historyTimeWindow").toLongLong()
                                == snapshot_.historyView.timeWindowMilliseconds);
    }
    for (std::size_t index = 0; index < seriesDefinitions().size(); ++index) {
        auto* checkBox = seriesVisibilityCheckBoxes_[index];
        if (checkBox == nullptr)
            continue;
        const auto& id = seriesDefinitions()[index].id;
        const QSignalBlocker seriesBlocker(checkBox);
        checkBox->setChecked(std::find(snapshot_.historyView.visibleSeriesIds.cbegin(),
                                       snapshot_.historyView.visibleSeriesIds.cend(),
                                       id)
                             != snapshot_.historyView.visibleSeriesIds.cend());
    }
    for (auto* checkBox : referenceLineCheckBoxes_) {
        if (checkBox == nullptr)
            continue;
        const auto id = checkBox->objectName().section(QLatin1Char('.'), -1);
        const QSignalBlocker referenceBlocker(checkBox);
        checkBox->setChecked(referenceLineVisibility_.value(id, true));
    }
    const QSignalBlocker pauseBlocker(pauseAction_);
    pauseAction_->setChecked(snapshot_.historyView.paused);
    recordingAction_->blockSignals(true);
    recordingAction_->setChecked(snapshot_.recordingEnabled);
    recordingAction_->blockSignals(false);
    const QSignalBlocker recordingBlocker(recordingCheckBox_);
    recordingCheckBox_->setChecked(snapshot_.recordingEnabled);
    recordingAction_->setText(snapshot_.recordingEnabled ? QStringLiteral("停止记录") : QStringLiteral("记录响度"));
    updateLegend();
    updateReferenceLines();
    updateChart();
    updateMarkerTable();
    updateCsvPreview();
}

QString MeterHistoryWindow::instanceId() const
{
    return snapshot_.instanceId;
}

WinJACKNexus::UI::HistoryChartWidget* MeterHistoryWindow::chart() const noexcept
{
    return chart_;
}

WinJACKNexus::UI::HistoryLegendWidget* MeterHistoryWindow::legend() const noexcept
{
    return legend_;
}

WinJACKNexus::UI::MetricStatisticsWidget* MeterHistoryWindow::statistics() const noexcept
{
    return statistics_;
}

MeterSettingsWidget* MeterHistoryWindow::settingsWidget() const noexcept
{
    return settingsWidget_;
}

int MeterHistoryWindow::markerCount() const noexcept
{
    return markerTable_->rowCount();
}

void MeterHistoryWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    scanTimer_->start();
}

void MeterHistoryWindow::hideEvent(QHideEvent* event)
{
    scanTimer_->stop();
    QWidget::hideEvent(event);
}

void MeterHistoryWindow::dispatch(MeterViewCommand command)
{
    if (snapshot_.instanceId.isEmpty())
        return;
    command.instanceId = snapshot_.instanceId;
    if (provider_.dispatch(command) && requestRefresh_)
        requestRefresh_();
}

void MeterHistoryWindow::updateChart()
{
    const auto timeWindow = std::max<std::int64_t>(1000, snapshot_.historyView.timeWindowMilliseconds);
    const auto firstElapsed = snapshot_.history.empty() ? 0 : snapshot_.history.front().elapsedMilliseconds;
    const auto latestElapsed = snapshot_.history.empty() ? timeWindow : snapshot_.history.back().elapsedMilliseconds;
    const auto scanning = snapshot_.historyView.mode == MeterHistoryViewMode::scanning;

    std::int64_t startElapsed = firstElapsed;
    std::int64_t endElapsed = std::max(firstElapsed + 1, latestElapsed);
    if (scanning) {
        startElapsed = 0;
        endElapsed = timeWindow;
        if (!scanCursorMilliseconds_.has_value() || *scanCursorMilliseconds_ < 0 || *scanCursorMilliseconds_ > timeWindow)
            scanCursorMilliseconds_ = latestElapsed % timeWindow;
        const QSignalBlocker sliderBlocker(scanCursorSlider_);
        scanCursorSlider_->setRange(0, static_cast<int>(timeWindow / 100));
        scanCursorSlider_->setValue(static_cast<int>(*scanCursorMilliseconds_ / 100));
    } else {
        const auto desiredEnd = focusElapsedMilliseconds_.has_value()
            ? *focusElapsedMilliseconds_ + timeWindow / 2
            : latestElapsed;
        endElapsed = std::clamp(desiredEnd,
                                firstElapsed + 1,
                                std::max(firstElapsed + 1, latestElapsed));
        startElapsed = endElapsed - timeWindow;
    }
    scanCursorSlider_->setVisible(scanning);
    chart_->setTimeRange(startElapsed, endElapsed);
    chart_->setTimeCursorMilliseconds(scanning ? scanCursorMilliseconds_ : focusElapsedMilliseconds_);

    std::vector<WinJACKNexus::UI::HistoryChartSeries> series;
    series.reserve(seriesDefinitions().size());
    for (const auto& definition : seriesDefinitions()) {
        const auto& id = definition.id;
        WinJACKNexus::UI::HistoryChartSeries item;
        item.id = id;
        item.label = definition.label;
        item.color = definition.color;
        item.axis = definition.axis;
        item.visible = std::find(snapshot_.historyView.visibleSeriesIds.cbegin(),
                                 snapshot_.historyView.visibleSeriesIds.cend(),
                                 id)
            != snapshot_.historyView.visibleSeriesIds.cend();
        item.preservePointOrder = scanning;
        std::optional<std::int64_t> previousMappedElapsed;
        for (const auto& point : snapshot_.history) {
            if (!scanning && (point.elapsedMilliseconds < startElapsed || point.elapsedMilliseconds > endElapsed))
                continue;
            const auto mappedElapsed = scanning ? point.elapsedMilliseconds % timeWindow : point.elapsedMilliseconds;
            if (scanning && (point.discontinuity
                             || (previousMappedElapsed.has_value() && mappedElapsed < *previousMappedElapsed))) {
                item.points.push_back({ mappedElapsed, std::nullopt });
            }
            item.points.push_back({ mappedElapsed, asDouble(historyValue(point, id)) });
            previousMappedElapsed = mappedElapsed;
        }
        series.push_back(std::move(item));
    }
    chart_->setSeries(std::move(series));
    updateStatistics(std::max(firstElapsed, endElapsed - timeWindow), endElapsed);
}

void MeterHistoryWindow::updateLegend()
{
    std::vector<WinJACKNexus::UI::HistoryLegendEntry> entries;
    entries.reserve(seriesDefinitions().size());
    for (const auto& definition : seriesDefinitions()) {
        const auto& id = definition.id;
        const auto hasData = std::any_of(snapshot_.history.cbegin(), snapshot_.history.cend(), [&id](const MeterHistoryPoint& point) {
            return historyValue(point, id).has_value();
        });
        entries.push_back({
            id,
            definition.label,
            definition.color,
            std::find(snapshot_.historyView.visibleSeriesIds.cbegin(), snapshot_.historyView.visibleSeriesIds.cend(), id)
                != snapshot_.historyView.visibleSeriesIds.cend(),
            hasData
        });
    }
    legend_->setEntries(std::move(entries));
}

void MeterHistoryWindow::updateStatistics(std::int64_t startMilliseconds, std::int64_t endMilliseconds)
{
    std::vector<WinJACKNexus::UI::MetricStatisticEntry> entries;
    entries.reserve(seriesDefinitions().size());
    for (const auto& definition : seriesDefinitions()) {
        const auto& id = definition.id;
        std::vector<double> values;
        for (const auto& point : snapshot_.history) {
            if (point.elapsedMilliseconds < startMilliseconds || point.elapsedMilliseconds > endMilliseconds)
                continue;
            const auto value = asDouble(historyValue(point, id));
            if (value.has_value())
                values.push_back(*value);
        }
        WinJACKNexus::UI::MetricStatisticEntry entry;
        entry.id = id;
        entry.label = definition.label;
        entry.unit = definition.unit;
        entry.color = definition.color;
        if (id == QStringLiteral("lfe") && values.empty())
            continue;
        if (!values.empty()) {
            const auto [minimum, maximum] = std::minmax_element(values.cbegin(), values.cend());
            entry.currentValue = values.back();
            entry.minimumValue = *minimum;
            entry.maximumValue = *maximum;
        }
        entries.push_back(std::move(entry));
    }
    statistics_->setEntries(std::move(entries));
}

void MeterHistoryWindow::updateMarkerTable()
{
    markerTable_->clearContents();
    markerTable_->setRowCount(static_cast<int>(snapshot_.markers.size()));
    for (std::size_t index = 0; index < snapshot_.markers.size(); ++index) {
        const auto& marker = snapshot_.markers[index];
        const auto row = static_cast<int>(index);
        const auto historyPoint = std::find_if(snapshot_.history.cbegin(), snapshot_.history.cend(), [&marker](const MeterHistoryPoint& point) {
            return point.elapsedMilliseconds == marker.elapsedMilliseconds;
        });
        const auto metricText = [historyPoint, this](const std::optional<float>& value) {
            return value.has_value() ? QString::number(*value, 'f', 1) : QStringLiteral("--");
        };
        markerTable_->setItem(row, 0, new QTableWidgetItem(QString::number(index + 1)));
        auto* timeItem = new QTableWidgetItem(elapsedText(marker.elapsedMilliseconds));
        timeItem->setData(Qt::UserRole, static_cast<qulonglong>(marker.id));
        timeItem->setData(Qt::UserRole + 1, static_cast<qlonglong>(marker.elapsedMilliseconds));
        markerTable_->setItem(row, 1, timeItem);
        if (historyPoint != snapshot_.history.cend()) {
            markerTable_->setItem(row, 2, new QTableWidgetItem(metricText(historyPoint->metrics.peakDbfs)));
            markerTable_->setItem(row, 3, new QTableWidgetItem(metricText(historyPoint->metrics.rmsDbfs)));
            markerTable_->setItem(row, 4, new QTableWidgetItem(metricText(historyPoint->metrics.truePeakDbtp)));
            markerTable_->setItem(row, 5, new QTableWidgetItem(metricText(historyPoint->metrics.momentaryLufs)));
            markerTable_->setItem(row, 6, new QTableWidgetItem(metricText(historyPoint->metrics.shortTermLufs)));
            markerTable_->setItem(row, 7, new QTableWidgetItem(metricText(historyPoint->metrics.integratedLufs)));
            markerTable_->setItem(row, 8, new QTableWidgetItem(metricText(historyPoint->metrics.lraLu)));
        }
        markerTable_->setItem(row, 9, new QTableWidgetItem(marker.note.isEmpty() ? marker.label : marker.note));
    }
}

void MeterHistoryWindow::updateCsvPreview()
{
    csvPathLabel_->setText(snapshot_.csvPreview.activeFilePath);
    csvStatusLabel_->setText(snapshot_.csvPreview.rotationStatus);
    if (exportStatusLabel_->text().isEmpty())
    exportStatusLabel_->setText(QStringLiteral("当前版本中 PNG 导出仅为预览。"));
}

void MeterHistoryWindow::updateReferenceLines()
{
    const auto visible = [this](const QString& id) {
        return referenceLineVisibility_.value(id, true);
    };
    chart_->setReferenceLines({
        { -18.0, QStringLiteral("瞬时（Momentary） -18 LUFS"), QColor(QStringLiteral("#43e88d")), Qt::DashLine, visible(QStringLiteral("momentary")) },
        { -23.0, QStringLiteral("短时（Short-term） -23 LUFS"), QColor(QStringLiteral("#48c9ff")), Qt::DashLine, visible(QStringLiteral("short-term")) },
        { -23.0, QStringLiteral("综合（Integrated） -23 LUFS"), QColor(QStringLiteral("#4b95ff")), Qt::DashLine, visible(QStringLiteral("integrated")) },
        { 20.0, QStringLiteral("LRA 0–20 LU"), QColor(QStringLiteral("#b96aff")), Qt::DashLine, visible(QStringLiteral("lra")), WinJACKNexus::UI::HistoryChartAxis::right }
    });
}

void MeterHistoryWindow::jumpToSelectedMarker()
{
    const auto elapsed = selectedMarkerElapsedMilliseconds();
    if (!elapsed.has_value())
        return;
    focusElapsedMilliseconds_ = *elapsed;
    const auto timeWindow = std::max<std::int64_t>(1000, snapshot_.historyView.timeWindowMilliseconds);
    if (snapshot_.historyView.mode == MeterHistoryViewMode::scanning) {
        scanCursorMilliseconds_ = *elapsed % timeWindow;
        const QSignalBlocker sliderBlocker(scanCursorSlider_);
        scanCursorSlider_->setValue(static_cast<int>(*scanCursorMilliseconds_ / 100));
    }
    updateChart();
}

void MeterHistoryWindow::removeSelectedMarker()
{
    const auto selectedItems = markerTable_->selectedItems();
    if (selectedItems.isEmpty())
        return;
    const auto row = selectedItems.front()->row();
    const auto* timeItem = markerTable_->item(row, 1);
    if (timeItem == nullptr)
        return;
    MeterViewCommand command;
    command.type = MeterViewCommandType::removeHistoryMarker;
    command.markerId = static_cast<std::uint32_t>(timeItem->data(Qt::UserRole).toULongLong());
    dispatch(std::move(command));
}

std::optional<std::int64_t> MeterHistoryWindow::selectedMarkerElapsedMilliseconds() const
{
    const auto selectedItems = markerTable_->selectedItems();
    if (selectedItems.isEmpty())
        return std::nullopt;
    const auto row = selectedItems.front()->row();
    const auto* timeItem = markerTable_->item(row, 1);
    if (timeItem == nullptr)
        return std::nullopt;
    return timeItem->data(Qt::UserRole + 1).toLongLong();
}

}