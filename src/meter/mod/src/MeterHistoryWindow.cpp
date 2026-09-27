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

#include <WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp>
#include <WinJACKNexus/Meter/Mod/MeterSettingsWidget.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeBackground.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QSlider>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStyleOption>
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

constexpr std::int64_t historyDataRefreshIntervalMilliseconds = 100;

QColor meterColor(const char* role, const char* fallback)
{
    return WinJACKNexus::UI::Controls::themeTokenColor(
        QStringLiteral("meter-bridge"), QString::fromLatin1(role), QColor(fallback));
}

QString meterCssColor(const char* role, const char* fallback)
{
    const auto value = meterColor(role, fallback);
    if (value.alpha() == 255) return value.name();
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alphaF());
}

int intervalForRefreshRate(int framesPerSecond) noexcept
{
    return std::max(1, 1000 / framesPerSecond);
}

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
        SeriesDefinition { QStringLiteral("lfe"), QStringLiteral("LFE 峰值（LFE Peak）"), QStringLiteral("dBFS"), QColor(QStringLiteral("#ff78c7")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("true-peak"), QStringLiteral("dBTP"), QStringLiteral("dBTP"), QColor(QStringLiteral("#ffd143")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("momentary"), QStringLiteral("瞬时（Momentary）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#43e88d")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("short-term"), QStringLiteral("短时（Short-term）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#48c9ff")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("integrated"), QStringLiteral("综合（Integrated）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#4b95ff")), WinJACKNexus::UI::HistoryChartAxis::left },
        SeriesDefinition { QStringLiteral("lra"), QStringLiteral("LRA"), QStringLiteral("LU"), QColor(QStringLiteral("#b96aff")), WinJACKNexus::UI::HistoryChartAxis::right }
    };
    return definitions;
}

QColor seriesThemeColor(const SeriesDefinition& definition)
{
    return WinJACKNexus::UI::Controls::themeTokenColor(
        QStringLiteral("meter-bridge"),
        QStringLiteral("chart-series-%1-stroke").arg(definition.id), definition.color);
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

QToolButton* createActionButton(QAction* action, const QString& objectName, QWidget* parent,
                                const WinJACKNexus::UI::Controls::Palette& colors)
{
    auto* button = new WinJACKNexus::UI::Controls::IconActionButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
    WinJACKNexus::UI::Controls::applyPalette(button, colors);
    return button;
}

const WinJACKNexus::Common::MeterLoudnessPreset& referencePreset(const QString& presetId)
{
    if (const auto* preset = WinJACKNexus::Common::findMeterLoudnessPreset(presetId.toStdString()); preset != nullptr)
        return *preset;
    return *WinJACKNexus::Common::findMeterLoudnessPreset("ebu-r128");
}

QString formatReferenceValue(double value)
{
    return QString::number(value, 'f', 1);
}

QString integratedReferenceLabel(const WinJACKNexus::Common::MeterLoudnessPreset& preset)
{
    if (!preset.alternateTargetHighLufs.has_value())
        return QStringLiteral("综合响度（Integrated） %1 LUFS").arg(formatReferenceValue(preset.targetIntegratedLufs));
    return QStringLiteral("综合响度（Integrated） %1 至 %2 LUFS")
        .arg(formatReferenceValue(preset.targetIntegratedLufs), formatReferenceValue(*preset.alternateTargetHighLufs));
}

QString truePeakReferenceLabel(const WinJACKNexus::Common::MeterLoudnessPreset& preset)
{
    return QStringLiteral("最大真峰值（dBTP） %1 dBTP").arg(formatReferenceValue(preset.maximumTruePeakDbtp));
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
    namespace Controls = WinJACKNexus::UI::Controls;
    Controls::Palette controlColors;
    controlColors.background = meterColor("surface-page", "#061523");
    controlColors.panel = meterColor("control-tab-surface-normal", "#102742");
    controlColors.text = meterColor("text-history-control", "#cfe4ff");
    controlColors.accent = meterColor("accent-primary", "#3b9dff");
    controlColors.border = meterColor("control-tab-border-normal", "#244b6b");
    setObjectName(QStringLiteral("meterHistory.window"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new Controls::SplitField(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("meterHistory.splitter"));
    settingsWidget_ = new MeterSettingsWidget(provider_, [this] {
        if (requestRefresh_)
            requestRefresh_();
    }, splitter);
    splitter->addWidget(settingsWidget_);
    auto* settingsLayout = qobject_cast<QVBoxLayout*>(settingsWidget_->layout());
    auto* returnButton = new Controls::IconActionButton(settingsWidget_);
    Controls::applyPalette(returnButton, controlColors);
    returnButton->setObjectName(QStringLiteral("meterHistory.returnButton"));
    returnButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    returnButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    returnButton->setText(QStringLiteral("返回电平表桥"));
    returnButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    returnButton->setFixedHeight(Controls::sidebarControlHeight);
    returnButton->setToolTip(QStringLiteral("返回电平表桥窗口"));
    if (settingsLayout != nullptr)
        settingsLayout->insertWidget(0, returnButton);
    connect(returnButton, &QToolButton::clicked, this, [this] {
        if (returnToBridge_)
            returnToBridge_();
    });

    viewModeSelector_ = new Controls::ChoiceField(settingsWidget_);
    Controls::applyPalette(viewModeSelector_, controlColors);
    viewModeSelector_->setObjectName(QStringLiteral("meterHistory.viewMode"));
    viewModeSelector_->setAccessibleName(QStringLiteral("历史刷新形式"));
    viewModeSelector_->setFixedHeight(Controls::sidebarControlHeight);
    viewModeSelector_->setMinimumWidth(180);
    viewModeSelector_->addItem(QStringLiteral("滚动（实时滚动）"), static_cast<int>(MeterHistoryViewMode::realtimeScroll));
    viewModeSelector_->addItem(QStringLiteral("扫描"), static_cast<int>(MeterHistoryViewMode::scanning));

    timeWindowSpinBox_ = new Controls::IntegerField(settingsWidget_);
    timeWindowSpinBox_->setObjectName(QStringLiteral("meterHistory.timeWindow"));
    timeWindowSpinBox_->setAccessibleName(QStringLiteral("历史时间窗口（秒）"));
    timeWindowSpinBox_->setFixedHeight(Controls::sidebarControlHeight);
    timeWindowSpinBox_->setRange(static_cast<int>(minimumMeterHistoryWindowMilliseconds / 1000),
                                  static_cast<int>(maximumMeterHistoryWindowMilliseconds / 1000));
    timeWindowSpinBox_->setSuffix(QStringLiteral(" 秒"));
    timeWindowSpinBox_->setValue(static_cast<int>(defaultMeterHistoryWindowMilliseconds / 1000));

    auto* displaySettingsGroup = settingsWidget_->findChild<QGroupBox*>(QStringLiteral("meterSettings.globalGroup"));
    auto* displaySettingsLayout = displaySettingsGroup != nullptr
        ? qobject_cast<QFormLayout*>(displaySettingsGroup->layout())
        : nullptr;
    if (displaySettingsLayout != nullptr) {
        displaySettingsLayout->addRow(QStringLiteral("刷新形式"), viewModeSelector_);
        displaySettingsLayout->addRow(QStringLiteral("时间窗口"), timeWindowSpinBox_);
    }

    auto* seriesGroup = new Controls::GroupPanel(QStringLiteral("曲线显示"), settingsWidget_);
    seriesGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    seriesGroup->setObjectName(QStringLiteral("meterHistory.seriesGroup"));
    auto* seriesLayout = new QVBoxLayout(seriesGroup);
    seriesLayout->setContentsMargins(8, 10, 8, 6);
    seriesLayout->setSpacing(2);
    for (std::size_t index = 0; index < seriesDefinitions().size(); ++index) {
        const auto& definition = seriesDefinitions()[index];
        auto* checkBox = new Controls::ToggleField(definition.label, seriesGroup);
        checkBox->setObjectName(QStringLiteral("meterHistory.series.%1").arg(definition.id));
        checkBox->setAccessibleName(QStringLiteral("显示 %1 曲线").arg(definition.label));
        checkBox->setChecked(true);
        auto seriesColors = controlColors;
        seriesColors.text = seriesThemeColor(definition);
        Controls::applyPalette(checkBox, seriesColors);
        seriesVisibilityCheckBoxes_[index] = checkBox;
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(7);
        auto* swatch = new QLabel(seriesGroup);
        swatch->setObjectName(QStringLiteral("meterHistory.seriesColor.%1").arg(definition.id));
        swatch->setFixedSize(12, 12);
        swatch->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 2px;")
                                 .arg(seriesThemeColor(definition).name()));
        row->addWidget(swatch);
        row->addWidget(checkBox, 1);
        seriesLayout->addLayout(row);
        connect(checkBox, &QCheckBox::toggled, this, [this, id = definition.id](bool visible) {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setHistorySeriesVisible;
            command.seriesId = id;
            command.enabled = visible;
            dispatch(std::move(command));
        });
    }

    const auto& initialReferencePreset = referencePreset({});
    const std::array referenceDefinitions {
        std::pair { QStringLiteral("integrated"), integratedReferenceLabel(initialReferencePreset) },
        std::pair { QStringLiteral("dbtp"), truePeakReferenceLabel(initialReferencePreset) }
    };
    auto* referenceGroup = new Controls::GroupPanel(QStringLiteral("参考线"), settingsWidget_);
    referenceGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    referenceGroup->setObjectName(QStringLiteral("meterHistory.referenceGroup"));
    auto* referenceLayout = new QVBoxLayout(referenceGroup);
    referenceLayout->setContentsMargins(8, 10, 8, 6);
    referenceLayout->setSpacing(2);
    for (std::size_t index = 0; index < referenceDefinitions.size(); ++index) {
        const auto& [id, label] = referenceDefinitions[index];
        auto* checkBox = new Controls::ToggleField(label, referenceGroup);
        Controls::applyPalette(checkBox, controlColors);
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
        if (displaySettingsGroup != nullptr)
            displaySettingsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
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
    controls->addWidget(new Controls::TextLabel(QStringLiteral("通道"), controlsFrame));
    channelSelector_ = new Controls::ChoiceField(controlsFrame);
    Controls::applyPalette(channelSelector_, controlColors);
    channelSelector_->setObjectName(QStringLiteral("meterHistory.channelSelector"));
    channelSelector_->setAccessibleName(QStringLiteral("历史图表通道"));
    channelSelector_->setToolTip(QStringLiteral("切换要查看的模拟通道历史"));
    channelSelector_->setMinimumContentsLength(14);
    channelSelector_->setMinimumWidth(270);
    channelSelector_->setMaximumWidth(420);
    channelSelector_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    channelSelector_->setFixedHeight(Controls::sidebarControlHeight);
    channelSelector_->setEnabled(false);
    controls->addWidget(channelSelector_);
    controls->addStretch(1);

    scanCursorSlider_ = new Controls::RangeSlider(Qt::Horizontal, chartSurface);
    Controls::applyPalette(scanCursorSlider_, controlColors);
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
    auto* pauseButton = createActionButton(pauseAction_, QStringLiteral("meterHistory.pauseButton"), controlsFrame, controlColors);
    pauseButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    auto* clearButton = createActionButton(clearAction_, QStringLiteral("meterHistory.clearButton"), controlsFrame, controlColors);
    clearButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    auto* chartSettingsButton = new Controls::IconActionButton(controlsFrame);
    Controls::applyPalette(chartSettingsButton, controlColors);
    chartSettingsButton->setObjectName(QStringLiteral("meterHistory.chartSettingsButton"));
    chartSettingsButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    chartSettingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    chartSettingsButton->setText(QStringLiteral("图表设置"));
    chartSettingsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    chartSettingsButton->setFixedHeight(Controls::sidebarControlHeight);
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
    chart_->setRightValueRange(0.0, 84.0);
    chart_->setVerticalAxisLabel(QStringLiteral("电平（Level） / 响度（Loudness） (dB / LUFS)"));
    chart_->setRightVerticalAxisLabel(QStringLiteral("LRA (LU)"));
    chart_->setHorizontalAxisLabel(QStringLiteral("系统时间"));
    auto historyPalette = WinJACKNexus::UI::ThemePalette::darkRack();
    historyPalette.setColor(QStringLiteral("window-bg"), meterColor("surface-page", "#061523"));
    historyPalette.setColor(QStringLiteral("panel-bg"), meterColor("control-tab-surface-disabled", "#0a1d30"));
    historyPalette.setColor(QStringLiteral("panel-raised"), meterColor("control-tab-surface-normal", "#102742"));
    historyPalette.setColor(QStringLiteral("text"), meterColor("text-page", "#d8e9ff"));
    historyPalette.setColor(QStringLiteral("text-muted"), meterColor("text-history-muted", "#88a9ce"));
    historyPalette.setColor(QStringLiteral("accent"), meterColor("accent-primary", "#3b9dff"));
    historyPalette.setColor(QStringLiteral("accent-alt"), meterColor("accent-secondary", "#ffd143"));
    historyPalette.setColor(QStringLiteral("border"), meterColor("control-tab-border-normal", "#244b6b"));
    historyPalette.setColor(QStringLiteral("danger"), meterColor("state-danger", "#ff5269"));
    historyPalette.setColor(QStringLiteral("success"), meterColor("state-success", "#43e88d"));
    chart_->setThemePalette(historyPalette);
    updateReferenceLines();
    rootLayout->addWidget(chart_, 1);

    legend_ = new WinJACKNexus::UI::HistoryLegendWidget(chartSurface);
    legend_->setObjectName(QStringLiteral("meterHistory.legend"));
    legend_->setThemePalette(historyPalette);
    rootLayout->addWidget(legend_);

    auto* statisticsTitle = new Controls::TextLabel(QStringLiteral("统计信息（当前视图）"), chartSurface);
    statisticsTitle->setObjectName(QStringLiteral("meterHistory.statisticsTitle"));
    statisticsTitle->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; padding: 3px 0;")
        .arg(meterCssColor("control-history-group-text", "#b8d7f6")));
    rootLayout->addWidget(statisticsTitle);
    statistics_ = new WinJACKNexus::UI::MetricStatisticsWidget(chartSurface);
    statistics_->setObjectName(QStringLiteral("meterHistory.statistics"));
    statistics_->setPrecision(1);
    statistics_->setThemePalette(historyPalette);
    rootLayout->addWidget(statistics_);

    auto* csvGroup = new Controls::GroupPanel(QStringLiteral("实时记录到 CSV"), settingsWidget_);
    csvGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* csvLayout = new QVBoxLayout(csvGroup);
    csvLayout->setContentsMargins(8, 10, 8, 6);
    csvPathLabel_ = new Controls::TextLabel(csvGroup);
    csvPathLabel_->setObjectName(QStringLiteral("meterHistory.csvPath"));
    csvPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    csvPathLabel_->setWordWrap(true);
    csvStatusLabel_ = new Controls::TextLabel(csvGroup);
    csvStatusLabel_->setObjectName(QStringLiteral("meterHistory.csvStatus"));
    csvStatusLabel_->setWordWrap(true);
    exportStatusLabel_ = new Controls::TextLabel(csvGroup);
    exportStatusLabel_->setObjectName(QStringLiteral("meterHistory.exportStatus"));
    exportStatusLabel_->setWordWrap(true);
    recordingAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("记录响度"), this);
    recordingAction_->setObjectName(QStringLiteral("meterHistory.recordingAction"));
    recordingAction_->setCheckable(true);
    recordingAction_->setToolTip(QStringLiteral("切换模拟响度记录状态"));
    recordingCheckBox_ = new Controls::ToggleField(QStringLiteral("启用实时记录到 CSV"), csvGroup);
    Controls::applyPalette(recordingCheckBox_, controlColors);
    recordingCheckBox_->setObjectName(QStringLiteral("meterHistory.recordingToggle"));
    recordingCheckBox_->setAccessibleName(QStringLiteral("启用实时记录到 CSV"));
    auto* recordingButton = createActionButton(recordingAction_, QStringLiteral("meterHistory.recordingButton"), csvGroup, controlColors);
    recordingButton->hide();
    auto* exportButton = createActionButton(exportAction_, QStringLiteral("meterHistory.exportButton"), csvGroup, controlColors);
    exportButton->setProperty("meterHistoryControl", QStringLiteral("action"));
    csvLayout->addWidget(recordingCheckBox_);
    csvLayout->addWidget(csvPathLabel_);
    csvLayout->addWidget(csvStatusLabel_);
    csvLayout->addWidget(exportStatusLabel_);
    csvLayout->addWidget(exportButton);
    if (settingsLayout != nullptr) {
        settingsLayout->insertWidget(4, csvGroup);
        settingsLayout->addStretch(1);
    }

    splitter->addWidget(chartSurface);
    splitter->setSizes({ 312, 1048 });
    pageLayout->addWidget(splitter);

    scanTimer_ = new QTimer(this);
    scanTimer_->setInterval(intervalForRefreshRate(refreshRate_));
    connect(scanTimer_, &QTimer::timeout, this, [this] {
        if (!isVisible() || snapshot_.historyView.paused)
            return;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (lastHistoryDataRefreshMilliseconds_ < 0
            || now - lastHistoryDataRefreshMilliseconds_ >= historyDataRefreshIntervalMilliseconds)
            refreshHistoryData();
    });
            auto& applicationSettings = WinJACKNexus::UI::ApplicationSettings::instance();
            connect(&applicationSettings,
                &WinJACKNexus::UI::ApplicationSettings::frameRateChanged,
                this,
                [this](int framesPerSecond) { applyRefreshRate(framesPerSecond); });
            applyRefreshRate(applicationSettings.frameRate());

    connect(viewModeSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setHistoryViewMode;
        command.historyViewMode = static_cast<MeterHistoryViewMode>(viewModeSelector_->itemData(index).toInt());
        dispatch(std::move(command));
    });
    connect(timeWindowSpinBox_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int seconds) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setHistoryTimeWindow;
        command.historyTimeWindowMilliseconds = static_cast<std::int64_t>(seconds) * 1000;
        dispatch(std::move(command));
    });
    connect(channelSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        const auto instanceId = channelSelector_->itemData(index).toString();
        const auto snapshots = provider_.snapshots();
        const auto target = std::find_if(snapshots.cbegin(), snapshots.cend(), [&instanceId](const MeterViewSnapshot& snapshot) {
            return snapshot.instanceId == instanceId;
        });
        if (target != snapshots.cend())
            setSnapshot(*target);
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
    applyTheme();
    new WinJACKNexus::UI::ThemeBackgroundOverlay(QStringLiteral("meter-bridge"), this);
    if (auto* core = WinJACKNexus::UI::ThemeManager::installedThemeCore())
        connect(core, &WinJACKNexus::UI::ThemeCore::themeChanged, this, [this] {
            applyTheme();
            updateLegend();
            updateChart();
        });
}

void MeterHistoryWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    WinJACKNexus::UI::ThemeBackground::paint(painter, rect(), QStringLiteral("meter-bridge"));
}

void MeterHistoryWindow::applyTheme()
{
    QString style = QStringLiteral(
        "QWidget#meterHistory.window { background-color:%page%; color:%text%; }"
        "QWidget[meterHistorySurface=\"true\"] { background:transparent; }"
        "QFrame[meterHistoryToolbar=\"true\"] { background-color:%panel%; border:1px solid %toolbar-border%; border-radius:6px; }"
        "QComboBox, QLineEdit, QDoubleSpinBox, QSpinBox { background-color:%raised%; color:%text%; border:1px solid %border%; border-radius:4px; padding:4px; }"
        "QCheckBox { color:%control-text%; spacing:7px; min-height:20px; }"
        "QCheckBox::indicator { width:14px; height:14px; border:1px solid %checkbox-border%; background-color:%panel%; }"
        "QCheckBox::indicator:checked { background-color:%checked-surface%; border-color:%checked-border%; }"
        "QGroupBox { color:%group-text%; border:1px solid %toolbar-border%; border-radius:5px; margin-top:12px; padding-top:8px; }"
        "QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0 4px; }"
        "QSplitter::handle { background-color:%splitter%; width:1px; }");
    style.replace(QStringLiteral("%page%"), meterCssColor("surface-page", "#061523"));
    style.replace(QStringLiteral("%text%"), meterCssColor("text-page", "#d8e9ff"));
    style.replace(QStringLiteral("%panel%"), meterCssColor("control-tab-surface-disabled", "#0a1d30"));
    style.replace(QStringLiteral("%toolbar-border%"), meterCssColor("control-history-toolbar-border", "#183b5a"));
    style.replace(QStringLiteral("%raised%"), meterCssColor("control-tab-surface-normal", "#102742"));
    style.replace(QStringLiteral("%border%"), meterCssColor("control-tab-border-normal", "#244b6b"));
    style.replace(QStringLiteral("%control-text%"), meterCssColor("text-history-control", "#cfe4ff"));
    style.replace(QStringLiteral("%checkbox-border%"), meterCssColor("control-history-checkbox-border", "#376487"));
    style.replace(QStringLiteral("%checked-surface%"), meterCssColor("control-history-checkbox-selected-surface", "#2f9eff"));
    style.replace(QStringLiteral("%checked-border%"), meterCssColor("control-history-checkbox-selected-border", "#72bcff"));
    style.replace(QStringLiteral("%group-text%"), meterCssColor("control-history-group-text", "#b8d7f6"));
    style.replace(QStringLiteral("%splitter%"), meterCssColor("control-history-splitter-surface", "#122a42"));
    setStyleSheet(style);
    const auto opacity = WinJACKNexus::UI::ThemeBackground::componentOpacity(QStringLiteral("meter-bridge"));
    for (auto* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (dynamic_cast<WinJACKNexus::UI::ThemeBackgroundOverlay*>(child)) continue;
        if (opacity < 1.0) {
            auto* effect = qobject_cast<QGraphicsOpacityEffect*>(child->graphicsEffect());
            if (!effect) { effect = new QGraphicsOpacityEffect(child); child->setGraphicsEffect(effect); }
            effect->setOpacity(opacity);
        } else if (child->graphicsEffect()) {
            child->setGraphicsEffect(nullptr);
        }
    }
    update();

    auto palette = WinJACKNexus::UI::ThemePalette::darkRack();
    palette.setColor(QStringLiteral("window-bg"), meterColor("surface-page", "#061523"));
    palette.setColor(QStringLiteral("panel-bg"), meterColor("control-tab-surface-disabled", "#0a1d30"));
    palette.setColor(QStringLiteral("panel-raised"), meterColor("control-tab-surface-normal", "#102742"));
    palette.setColor(QStringLiteral("text"), meterColor("text-page", "#d8e9ff"));
    palette.setColor(QStringLiteral("text-muted"), meterColor("text-history-muted", "#88a9ce"));
    palette.setColor(QStringLiteral("accent"), meterColor("accent-primary", "#3b9dff"));
    palette.setColor(QStringLiteral("accent-alt"), meterColor("accent-secondary", "#ffd143"));
    palette.setColor(QStringLiteral("border"), meterColor("control-tab-border-normal", "#244b6b"));
    palette.setColor(QStringLiteral("danger"), meterColor("state-danger", "#ff5269"));
    palette.setColor(QStringLiteral("success"), meterColor("state-success", "#43e88d"));
    chart_->setThemePalette(palette);
    legend_->setThemePalette(palette);
    statistics_->setThemePalette(palette);
    if (auto* title = findChild<QLabel*>(QStringLiteral("meterHistory.statisticsTitle")))
        title->setStyleSheet(QStringLiteral("color:%1; font-weight:600; padding:3px 0;")
            .arg(meterCssColor("control-history-group-text", "#b8d7f6")));
    WinJACKNexus::UI::Controls::Palette controls;
    controls.background = palette.color(QStringLiteral("window-bg"));
    controls.panel = palette.color(QStringLiteral("panel-raised"));
    controls.text = meterColor("text-history-control", "#cfe4ff");
    controls.accent = palette.color(QStringLiteral("accent"));
    controls.border = palette.color(QStringLiteral("border"));
    for (auto* widget : findChildren<QWidget*>()) {
        if (qobject_cast<QToolButton*>(widget) || qobject_cast<QComboBox*>(widget)
            || qobject_cast<QCheckBox*>(widget) || qobject_cast<QSlider*>(widget))
            WinJACKNexus::UI::Controls::applyPalette(widget, controls);
    }
    const auto& definitions = seriesDefinitions();
    for (std::size_t index = 0; index < seriesVisibilityCheckBoxes_.size(); ++index) {
        if (!seriesVisibilityCheckBoxes_[index]) continue;
        auto seriesColors = controls;
        seriesColors.accent = seriesThemeColor(definitions[index]);
        WinJACKNexus::UI::Controls::applyPalette(seriesVisibilityCheckBoxes_[index], seriesColors);
    }
}

int MeterHistoryWindow::refreshRate() const noexcept
{
    return refreshRate_;
}

int MeterHistoryWindow::refreshIntervalMilliseconds() const noexcept
{
    return intervalForRefreshRate(refreshRate_);
}

void MeterHistoryWindow::setRefreshRate(int framesPerSecond)
{
    WinJACKNexus::UI::ApplicationSettings::instance().setFrameRate(framesPerSecond);
}

void MeterHistoryWindow::applyRefreshRate(int framesPerSecond)
{
    if (refreshRate_ == framesPerSecond)
        return;
    refreshRate_ = framesPerSecond;
    scanTimer_->setInterval(intervalForRefreshRate(refreshRate_));
}

void MeterHistoryWindow::setSnapshots(const std::vector<MeterViewSnapshot>& snapshots, const QString& preferredInstanceId)
{
    lastHistoryDataRefreshMilliseconds_ = QDateTime::currentMSecsSinceEpoch();
    const auto selectedInstanceId = preferredInstanceId.isEmpty() ? snapshot_.instanceId : preferredInstanceId;
    bool selectorChanged = channelSelector_->count() != static_cast<int>(snapshots.size());
    for (std::size_t index = 0; !selectorChanged && index < snapshots.size(); ++index) {
        const auto& snapshot = snapshots[index];
        const auto label = QStringLiteral("%1（%2）").arg(snapshot.clientName, snapshot.layoutLabel);
        selectorChanged = channelSelector_->itemData(static_cast<int>(index)).toString() != snapshot.instanceId
            || channelSelector_->itemText(static_cast<int>(index)) != label;
    }
    if (selectorChanged) {
        const QSignalBlocker selectorBlocker(channelSelector_);
        channelSelector_->clear();
        for (const auto& snapshot : snapshots)
            channelSelector_->addItem(QStringLiteral("%1（%2）").arg(snapshot.clientName, snapshot.layoutLabel), snapshot.instanceId);
    }
    channelSelector_->setEnabled(!snapshots.empty());
    int selectedIndex = -1;
    if (!selectedInstanceId.isEmpty())
        selectedIndex = channelSelector_->findData(selectedInstanceId);
    if (selectedIndex < 0 && !snapshots.empty())
        selectedIndex = 0;
    if (channelSelector_->currentIndex() != selectedIndex) {
        const QSignalBlocker selectorBlocker(channelSelector_);
        channelSelector_->setCurrentIndex(selectedIndex);
    }

    if (selectedIndex >= 0) {
        setSnapshot(snapshots[static_cast<std::size_t>(selectedIndex)]);
        return;
    }

    snapshot_ = {};
    chart_->setSeries({});
    statistics_->setEntries({});
    csvPathLabel_->clear();
    csvStatusLabel_->clear();
    exportStatusLabel_->clear();
    renderedHistoryElapsedMilliseconds_ = -1;
}

void MeterHistoryWindow::setLiveSnapshots(const std::vector<MeterViewSnapshot>& snapshots)
{
    if (!isVisible() || snapshot_.instanceId.isEmpty())
        return;
    const auto liveSnapshot = std::find_if(snapshots.cbegin(), snapshots.cend(), [this](const MeterViewSnapshot& candidate) {
        return candidate.instanceId == snapshot_.instanceId;
    });
    if (liveSnapshot == snapshots.cend())
        return;
    const auto metadataChanged = liveSnapshot->clientName != snapshot_.clientName
        || liveSnapshot->layoutLabel != snapshot_.layoutLabel
        || liveSnapshot->presetId != snapshot_.presetId
        || liveSnapshot->recordingEnabled != snapshot_.recordingEnabled
        || liveSnapshot->channels != snapshot_.channels
        || liveSnapshot->historyView != snapshot_.historyView
        || liveSnapshot->globalSettings != snapshot_.globalSettings
        || liveSnapshot->channelLayoutEditable != snapshot_.channelLayoutEditable
        || liveSnapshot->csvPreview != snapshot_.csvPreview;
    if (metadataChanged) {
        refreshHistoryData();
        return;
    }
    if (snapshot_.historyView.paused)
        return;
    const auto latestElapsed = liveSnapshot->latestHistoryElapsedMilliseconds.has_value()
        ? liveSnapshot->latestHistoryElapsedMilliseconds
        : (liveSnapshot->history.empty()
            ? std::optional<std::int64_t> {}
            : std::optional<std::int64_t> { liveSnapshot->history.back().elapsedMilliseconds });
    if (latestElapsed.has_value())
        updateTimeline(*latestElapsed);
}

void MeterHistoryWindow::setSnapshot(const MeterViewSnapshot& snapshot)
{
    const auto selectedInstanceChanged = snapshot_.instanceId != snapshot.instanceId;
    const auto historyChanged = selectedInstanceChanged
        || snapshot_.history.size() != snapshot.history.size()
        || (!snapshot.history.empty()
            && (snapshot_.history.front() != snapshot.history.front()
                || snapshot_.history.back() != snapshot.history.back()));
    const auto historyViewChanged = selectedInstanceChanged || snapshot_.historyView != snapshot.historyView;
    const auto settingsChanged = selectedInstanceChanged
        || snapshot_.globalSettings != snapshot.globalSettings
        || snapshot_.channels != snapshot.channels
        || snapshot_.channelLayoutEditable != snapshot.channelLayoutEditable;
    const auto controlsChanged = selectedInstanceChanged || historyViewChanged
        || snapshot_.recordingEnabled != snapshot.recordingEnabled;
    const auto referenceChanged = selectedInstanceChanged || snapshot_.presetId != snapshot.presetId;
    const auto csvChanged = selectedInstanceChanged || snapshot_.csvPreview != snapshot.csvPreview;
    if (!historyChanged && !settingsChanged && !controlsChanged && !referenceChanged && !csvChanged)
        return;
    const auto wasPaused = snapshot_.historyView.paused;
    if (!snapshot.historyView.paused) {
        pausedHistoryElapsedMilliseconds_.reset();
    } else {
        const auto latestElapsed = snapshot.history.empty() ? 0 : snapshot.history.back().elapsedMilliseconds;
        if (selectedInstanceChanged || !wasPaused || !pausedHistoryElapsedMilliseconds_.has_value()
            || latestElapsed < *pausedHistoryElapsedMilliseconds_) {
            pausedHistoryElapsedMilliseconds_ = latestElapsed;
            scanCursorMilliseconds_.reset();
        }
    }
    snapshot_ = snapshot;
    if (settingsChanged)
        settingsWidget_->setSnapshot(snapshot_);

    if (controlsChanged) {
        const QSignalBlocker modeBlocker(viewModeSelector_);
        viewModeSelector_->setCurrentIndex(viewModeSelector_->findData(static_cast<int>(snapshot_.historyView.mode)));
        const QSignalBlocker windowBlocker(timeWindowSpinBox_);
        timeWindowSpinBox_->setValue(static_cast<int>(snapshot_.historyView.timeWindowMilliseconds / 1000));
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
        const QSignalBlocker pauseBlocker(pauseAction_);
        pauseAction_->setChecked(snapshot_.historyView.paused);
        recordingAction_->blockSignals(true);
        recordingAction_->setChecked(snapshot_.recordingEnabled);
        recordingAction_->blockSignals(false);
        const QSignalBlocker recordingBlocker(recordingCheckBox_);
        recordingCheckBox_->setChecked(snapshot_.recordingEnabled);
        recordingAction_->setText(snapshot_.recordingEnabled ? QStringLiteral("停止记录") : QStringLiteral("记录响度"));
    }
    if (historyChanged || historyViewChanged) {
        updateLegend();
        updateChart();
    }
    if (referenceChanged)
        updateReferenceLines();
    if (csvChanged)
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

void MeterHistoryWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshHistoryData();
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
    if (!provider_.dispatch(command))
        return;
    refreshHistoryData();
    if (requestRefresh_)
        requestRefresh_();
}

void MeterHistoryWindow::updateChart()
{
    const auto timeWindow = std::max<std::int64_t>(1000, snapshot_.historyView.timeWindowMilliseconds);
    const auto firstElapsed = snapshot_.history.empty() ? 0 : snapshot_.history.front().elapsedMilliseconds;
    const auto liveLatestElapsed = snapshot_.history.empty() ? timeWindow : snapshot_.history.back().elapsedMilliseconds;
    const auto latestElapsed = snapshot_.historyView.paused && pausedHistoryElapsedMilliseconds_.has_value()
        ? std::min(liveLatestElapsed, *pausedHistoryElapsedMilliseconds_)
        : liveLatestElapsed;
    const auto scanning = snapshot_.historyView.mode == MeterHistoryViewMode::scanning;
    const auto scanCycleStart = latestElapsed / timeWindow * timeWindow;

    std::int64_t startElapsed = firstElapsed;
    std::int64_t endElapsed = std::max(firstElapsed + 1, latestElapsed);
    if (scanning) {
        startElapsed = 0;
        endElapsed = timeWindow;
        if (!snapshot_.historyView.paused || !scanCursorMilliseconds_.has_value()
            || *scanCursorMilliseconds_ < 0 || *scanCursorMilliseconds_ > timeWindow)
            scanCursorMilliseconds_ = latestElapsed % timeWindow;
        const QSignalBlocker sliderBlocker(scanCursorSlider_);
        scanCursorSlider_->setRange(0, static_cast<int>(timeWindow));
        scanCursorSlider_->setValue(static_cast<int>(*scanCursorMilliseconds_));
    } else {
        startElapsed = endElapsed - timeWindow;
    }
    scanCursorSlider_->setVisible(scanning);
    scanCursorSlider_->setEnabled(false);
    chart_->setTimeRange(startElapsed, endElapsed);
    chart_->setTimeCursorMilliseconds(scanning ? scanCursorMilliseconds_ : std::optional<std::int64_t> {});
    const auto latestTimestamp = std::find_if(snapshot_.history.crbegin(),
                                              snapshot_.history.crend(),
                                              [latestElapsed](const MeterHistoryPoint& point) {
                                                  return point.elapsedMilliseconds <= latestElapsed
                                                      && point.systemTimeMilliseconds.has_value();
                                              });
    std::optional<std::int64_t> timeDisplayOffset;
    if (latestTimestamp != snapshot_.history.crend()) {
        timeDisplayOffset = *latestTimestamp->systemTimeMilliseconds - latestTimestamp->elapsedMilliseconds;
        if (scanning)
            *timeDisplayOffset += scanCycleStart;
    } else if (!snapshot_.history.empty()) {
        timeDisplayOffset = QDateTime::currentMSecsSinceEpoch() - latestElapsed;
        if (scanning)
            *timeDisplayOffset += scanCycleStart;
    }
    chart_->setTimeDisplayOffsetMilliseconds(timeDisplayOffset);

    constexpr auto scanSlotMilliseconds = std::int64_t { 100 };
    if (!scanning) {
        for (auto& points : scanOverwritePoints_)
            points.clear();
        scanOverwriteInstanceId_.clear();
        scanOverwriteTimeWindowMilliseconds_ = 0;
        scanOverwriteLatestElapsedMilliseconds_ = -1;
    } else {
        const auto cacheNeedsReset = snapshot_.history.empty()
            || scanOverwriteInstanceId_ != snapshot_.instanceId
            || scanOverwriteTimeWindowMilliseconds_ != timeWindow
            || latestElapsed < scanOverwriteLatestElapsedMilliseconds_;
        if (cacheNeedsReset) {
            for (auto& points : scanOverwritePoints_)
                points.clear();
            scanOverwriteInstanceId_ = snapshot_.instanceId;
            scanOverwriteTimeWindowMilliseconds_ = timeWindow;
            scanOverwriteLatestElapsedMilliseconds_ = -1;
        }

        const auto minimumNewElapsed = scanOverwriteLatestElapsedMilliseconds_ + 1;
        const auto scanSlotCount = std::max<std::int64_t>(1, timeWindow / scanSlotMilliseconds);
        const MeterHistoryPoint* previousPoint = nullptr;
        for (const auto& point : snapshot_.history) {
            if (point.elapsedMilliseconds > latestElapsed)
                continue;
            if (point.elapsedMilliseconds < minimumNewElapsed) {
                previousPoint = &point;
                continue;
            }

            const auto currentSlot = point.elapsedMilliseconds / scanSlotMilliseconds;
            const auto canInterpolate = previousPoint != nullptr
                && !point.discontinuity
                && point.elapsedMilliseconds > previousPoint->elapsedMilliseconds;
            const auto firstSlot = canInterpolate
                ? previousPoint->elapsedMilliseconds / scanSlotMilliseconds + 1
                : currentSlot;
            const auto firstCoveredSlot = std::max(firstSlot, currentSlot - scanSlotCount + 1);
            for (auto coveredSlot = firstCoveredSlot; coveredSlot <= currentSlot; ++coveredSlot) {
                const auto mappedSlot = coveredSlot % scanSlotCount;
                const auto mappedElapsed = mappedSlot * scanSlotMilliseconds;
                const auto isLatestSlot = coveredSlot == currentSlot;
                const auto timestamp = isLatestSlot
                    ? point.elapsedMilliseconds % timeWindow
                    : mappedElapsed;
                const auto interpolationFraction = canInterpolate
                    ? std::clamp(static_cast<double>(coveredSlot * scanSlotMilliseconds - previousPoint->elapsedMilliseconds)
                                     / static_cast<double>(point.elapsedMilliseconds - previousPoint->elapsedMilliseconds),
                                 0.0,
                                 1.0)
                    : 1.0;
                for (std::size_t seriesIndex = 0; seriesIndex < seriesDefinitions().size(); ++seriesIndex) {
                    const auto currentValue = asDouble(historyValue(point, seriesDefinitions()[seriesIndex].id));
                    const auto previousValue = previousPoint != nullptr
                        ? asDouble(historyValue(*previousPoint, seriesDefinitions()[seriesIndex].id))
                        : std::optional<double> {};
                    const auto value = canInterpolate && previousValue.has_value() && currentValue.has_value()
                        ? std::optional<double> { *previousValue + (*currentValue - *previousValue) * interpolationFraction }
                        : currentValue;
                    scanOverwritePoints_[seriesIndex].insert_or_assign(mappedSlot,
                                                                        ScanOverwritePoint {
                                                                            timestamp,
                                                                            value,
                                                                            point.discontinuity && isLatestSlot
                                                                        });
                }
            }
            previousPoint = &point;
        }
        scanOverwriteLatestElapsedMilliseconds_ = latestElapsed;
    }

    std::vector<WinJACKNexus::UI::HistoryChartSeries> series;
    series.reserve(seriesDefinitions().size());
    for (std::size_t seriesIndex = 0; seriesIndex < seriesDefinitions().size(); ++seriesIndex) {
        const auto& definition = seriesDefinitions()[seriesIndex];
        const auto& id = definition.id;
        WinJACKNexus::UI::HistoryChartSeries item;
        item.id = id;
        item.label = definition.label;
        item.color = seriesThemeColor(definition);
        item.axis = definition.axis;
        item.visible = std::find(snapshot_.historyView.visibleSeriesIds.cbegin(),
                                 snapshot_.historyView.visibleSeriesIds.cend(),
                                 id)
            != snapshot_.historyView.visibleSeriesIds.cend();
        item.preservePointOrder = scanning;
        if (scanning) {
            for (const auto& [slot, point] : scanOverwritePoints_[seriesIndex]) {
                Q_UNUSED(slot)
                if (point.discontinuity)
                    item.points.push_back({ point.timestampMilliseconds, std::nullopt });
                item.points.push_back({ point.timestampMilliseconds, point.value });
            }
        } else {
            for (const auto& point : snapshot_.history) {
                if (point.elapsedMilliseconds < startElapsed || point.elapsedMilliseconds > endElapsed)
                    continue;
                item.points.push_back({ point.elapsedMilliseconds, asDouble(historyValue(point, id)) });
            }
        }
        series.push_back(std::move(item));
    }
    chart_->setSeries(std::move(series));
    updateStatistics(scanning ? std::max(firstElapsed, scanCycleStart) : std::max(firstElapsed, endElapsed - timeWindow),
                     scanning ? latestElapsed : endElapsed);
    renderedHistoryElapsedMilliseconds_ = latestElapsed;
}

void MeterHistoryWindow::updateTimeline(std::int64_t latestElapsedMilliseconds)
{
    const auto timeWindow = std::max<std::int64_t>(1000, snapshot_.historyView.timeWindowMilliseconds);
    const auto firstElapsed = snapshot_.history.empty() ? 0 : snapshot_.history.front().elapsedMilliseconds;
    const auto latestElapsed = std::max(firstElapsed, latestElapsedMilliseconds);
    if (snapshot_.historyView.mode == MeterHistoryViewMode::scanning) {
        scanCursorMilliseconds_ = latestElapsed % timeWindow;
        const QSignalBlocker sliderBlocker(scanCursorSlider_);
        scanCursorSlider_->setRange(0, static_cast<int>(timeWindow));
        scanCursorSlider_->setValue(static_cast<int>(*scanCursorMilliseconds_));
        chart_->setTimeCursorMilliseconds(scanCursorMilliseconds_);
        return;
    }
    chart_->setTimeRange(latestElapsed - timeWindow, std::max(firstElapsed + 1, latestElapsed));
}

void MeterHistoryWindow::refreshHistoryData()
{
    setSnapshots(provider_.snapshots());
}

void MeterHistoryWindow::updateLegend()
{
    std::vector<WinJACKNexus::UI::HistoryLegendEntry> entries;
    entries.reserve(seriesDefinitions().size());
    for (const auto& definition : seriesDefinitions()) {
        const auto& id = definition.id;
        entries.push_back({
            id,
            definition.label,
            seriesThemeColor(definition),
            std::find(snapshot_.historyView.visibleSeriesIds.cbegin(), snapshot_.historyView.visibleSeriesIds.cend(), id)
                != snapshot_.historyView.visibleSeriesIds.cend(),
            true
        });
    }
    legend_->setEntries(std::move(entries));
    for (std::size_t index = 0; index < seriesDefinitions().size(); ++index) {
        const auto& definition = seriesDefinitions()[index];
        const auto color = seriesThemeColor(definition);
        if (auto* checkBox = seriesVisibilityCheckBoxes_[index]) {
            checkBox->setProperty("textColor", color);
            checkBox->update();
        }
        if (auto* swatch = findChild<QLabel*>(QStringLiteral("meterHistory.seriesColor.%1").arg(definition.id)))
            swatch->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 2px;").arg(color.name()));
    }
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
        entry.color = seriesThemeColor(definition);
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
    const auto& preset = referencePreset(snapshot_.presetId);
    const auto integratedLabel = integratedReferenceLabel(preset);
    const auto truePeakLabel = truePeakReferenceLabel(preset);
    if (referenceLineCheckBoxes_[0] != nullptr)
        referenceLineCheckBoxes_[0]->setText(integratedLabel);
    if (referenceLineCheckBoxes_[1] != nullptr)
        referenceLineCheckBoxes_[1]->setText(truePeakLabel);

    std::vector<WinJACKNexus::UI::HistoryChartReferenceLine> lines;
    lines.reserve(preset.alternateTargetHighLufs.has_value() ? 3 : 2);
    lines.push_back({
        preset.targetIntegratedLufs,
        preset.alternateTargetHighLufs.has_value()
            ? QStringLiteral("综合响度（Integrated）下限 %1 LUFS").arg(formatReferenceValue(preset.targetIntegratedLufs))
            : integratedLabel,
        QColor(QStringLiteral("#4b95ff")),
        Qt::DashLine,
        visible(QStringLiteral("integrated"))
    });
    if (preset.alternateTargetHighLufs.has_value()) {
        lines.push_back({
            *preset.alternateTargetHighLufs,
            QStringLiteral("综合响度（Integrated）上限 %1 LUFS").arg(formatReferenceValue(*preset.alternateTargetHighLufs)),
            QColor(QStringLiteral("#4b95ff")),
            Qt::DashLine,
            visible(QStringLiteral("integrated"))
        });
    }
    lines.push_back({
        preset.maximumTruePeakDbtp,
        truePeakLabel,
        QColor(QStringLiteral("#ffd143")),
        Qt::DashLine,
        visible(QStringLiteral("dbtp"))
    });
    chart_->setReferenceLines(std::move(lines));
}

}
