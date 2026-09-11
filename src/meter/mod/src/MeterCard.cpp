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
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>

#include <WinJACKNexus/UI/MeterLevelBar.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>

namespace WinJACKNexus::Meter::Mod {

namespace {

constexpr int minimumMeterCardHeight = 560;

QString stateText(MeterProbeState state)
{
    switch (state) {
    case MeterProbeState::silent:
        return QStringLiteral("静音");
    case MeterProbeState::active:
        return QStringLiteral("活动");
    case MeterProbeState::overload:
        return QStringLiteral("超限");
    case MeterProbeState::offline:
        return QStringLiteral("离线");
    }
    return QStringLiteral("未知");
}

std::optional<double> asDouble(const std::optional<float>& value)
{
    if (!value.has_value())
        return std::nullopt;
    return static_cast<double>(*value);
}

WinJACKNexus::UI::ThemePalette meterCardPalette()
{
    auto palette = WinJACKNexus::UI::ThemePalette::darkRack();
    palette.setColor(QStringLiteral("window-bg"), QColor(QStringLiteral("#0b1928")));
    palette.setColor(QStringLiteral("panel-bg"), QColor(QStringLiteral("#0b1928")));
    palette.setColor(QStringLiteral("panel-raised"), QColor(QStringLiteral("#10253b")));
    palette.setColor(QStringLiteral("text"), QColor(QStringLiteral("#e7f3ff")));
    palette.setColor(QStringLiteral("text-muted"), QColor(QStringLiteral("#83a7d0")));
    palette.setColor(QStringLiteral("accent"), QColor(QStringLiteral("#3b9dff")));
    palette.setColor(QStringLiteral("accent-alt"), QColor(QStringLiteral("#ffd143")));
    palette.setColor(QStringLiteral("border"), QColor(QStringLiteral("#2a5275")));
    palette.setColor(QStringLiteral("danger"), QColor(QStringLiteral("#ff5269")));
    palette.setColor(QStringLiteral("success"), QColor(QStringLiteral("#43e88d")));
    return palette;
}

QToolButton* createCommandButton(QAction* action, const QString& objectName, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setFixedSize(68, 76);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setProperty("meterCardControl", QStringLiteral("action"));
    return button;
}

}

MeterCard::MeterCard(MeterViewDataProvider& provider,
                     std::function<void()> requestRefresh,
                     std::function<void(const QString&)> openHistory,
                     QWidget* parent)
    : QFrame(parent)
    , provider_(provider)
    , requestRefresh_(std::move(requestRefresh))
    , openHistory_(std::move(openHistory))
{
    setObjectName(QStringLiteral("meterbridge.card"));
    setProperty("meterCardSurface", true);
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground);
    setFixedSize(242, 834);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral(
        "QFrame[meterCardSurface=\"true\"] {"
        " background-color: #0b1928;"
        " border: 1px solid #27516c;"
        " border-radius: 8px;"
        "}"
        "QFrame[meterCardSection=\"header\"] {"
        " background-color: transparent;"
        " border: 0;"
        "}"
        "QFrame[meterCardSection=\"preset\"],"
        "QFrame[meterCardSection=\"content\"],"
        "QFrame[meterCardSection=\"level\"],"
        "QFrame[meterCardSection=\"metrics\"],"
        "QFrame[meterCardSection=\"actions\"] {"
        " background-color: transparent;"
        " border: 0;"
        "}"
        "QLabel[meterCardRole=\"number\"] {"
        " background-color: #1688e8;"
        " color: #071626;"
        " border: 1px solid #51b8ff;"
        " border-radius: 4px;"
        " font-size: 17px;"
        " font-weight: 600;"
        "}"
        "QLabel[meterCardRole=\"title\"] { color: #e7f3ff; }"
        "QLabel[meterCardRole=\"meta\"],"
        "QLabel[meterCardRole=\"presetLabel\"] { color: #82a6cf; }"
        "QLabel[meterCardRole=\"state\"] {"
        " color: #8ce0bf;"
        " background-color: #1b3a34;"
        " border: 1px solid #3f8270;"
        " border-radius: 8px;"
        " padding: 2px 6px;"
        "}"
        "QComboBox[meterCardControl=\"preset\"] {"
        " background-color: #12263c;"
        " color: #dcecff;"
        " border: 1px solid #294d70;"
        " border-radius: 7px;"
        " padding: 4px 28px 4px 16px;"
        " font-size: 16px;"
        "}"
        "QComboBox[meterCardControl=\"preset\"]::drop-down {"
        " border: 0;"
        " width: 28px;"
        "}"
        "QToolButton[meterCardControl=\"action\"] {"
        " background-color: #13263b;"
        " color: #dbeaff;"
        " border: 1px solid #274b6c;"
        " border-radius: 8px;"
        " padding: 4px 2px;"
        "}"
        "QToolButton[meterCardControl=\"action\"]:hover {"
        " background-color: #1b3957;"
        " border-color: #4a9df0;"
        "}"
        "QToolButton[meterCardControl=\"action\"]:checked {"
        " background-color: #183f58;"
        " border-color: #3fa9f5;"
        " color: #ffffff;"
        "}"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 12, 10, 10);
    rootLayout->setSpacing(8);
    const auto cardPalette = meterCardPalette();

    auto* headerPanel = new QFrame(this);
    headerPanel->setObjectName(QStringLiteral("meterCard.header"));
    headerPanel->setProperty("meterCardSection", QStringLiteral("header"));
    headerPanel->setAttribute(Qt::WA_StyledBackground);
    auto* headerLayout = new QHBoxLayout(headerPanel);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(12);
    numberLabel_ = new QLabel(QStringLiteral("1"), headerPanel);
    numberLabel_->setObjectName(QStringLiteral("meterCard.number"));
    numberLabel_->setProperty("meterCardRole", QStringLiteral("number"));
    numberLabel_->setAlignment(Qt::AlignCenter);
    numberLabel_->setFixedSize(36, 36);
    titleLabel_ = new QLabel(headerPanel);
    titleLabel_->setObjectName(QStringLiteral("meterCard.clientName"));
    titleLabel_->setProperty("meterCardRole", QStringLiteral("title"));
    QFont titleFont = titleLabel_->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    titleLabel_->setFont(titleFont);
    layoutLabel_ = new QLabel(headerPanel);
    layoutLabel_->setObjectName(QStringLiteral("meterCard.layout"));
    layoutLabel_->setProperty("meterCardRole", QStringLiteral("meta"));
    QFont layoutFont = layoutLabel_->font();
    layoutFont.setPointSize(layoutFont.pointSize() + 1);
    layoutLabel_->setFont(layoutFont);
    stateLabel_ = new QLabel(headerPanel);
    stateLabel_->setObjectName(QStringLiteral("meterCard.state"));
    stateLabel_->setProperty("meterCardRole", QStringLiteral("state"));
    stateLabel_->hide();
    auto* headerText = new QWidget(headerPanel);
    auto* headerTextLayout = new QVBoxLayout(headerText);
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(0);
    headerTextLayout->addWidget(titleLabel_);
    headerTextLayout->addWidget(layoutLabel_);
    headerLayout->addWidget(numberLabel_);
    headerLayout->addWidget(headerText, 1);
    rootLayout->addWidget(headerPanel);

    auto* presetPanel = new QFrame(this);
    presetPanel->setObjectName(QStringLiteral("meterCard.presetPanel"));
    presetPanel->setProperty("meterCardSection", QStringLiteral("preset"));
    presetPanel->setAttribute(Qt::WA_StyledBackground);
    auto* presetLayout = new QHBoxLayout(presetPanel);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    presetSelector_ = new QComboBox(presetPanel);
    presetSelector_->setObjectName(QStringLiteral("meterCard.preset"));
    presetSelector_->setProperty("meterCardControl", QStringLiteral("preset"));
    presetSelector_->setAccessibleName(QStringLiteral("响度预设（Loudness Preset）"));
    presetSelector_->setToolTip(QStringLiteral("选择此探针的显示参考预设"));
    presetSelector_->setFixedHeight(50);
    configurePresetSelector();
    presetLayout->addWidget(presetSelector_);
    rootLayout->addWidget(presetPanel);

    auto* contentPanel = new QFrame(this);
    contentPanel->setObjectName(QStringLiteral("meterCard.content"));
    contentPanel->setProperty("meterCardSection", QStringLiteral("content"));
    contentPanel->setAttribute(Qt::WA_StyledBackground);
    auto* contentLayout = new QHBoxLayout(contentPanel);
    contentLayout->setContentsMargins(0, 12, 0, 8);
    contentLayout->setSpacing(12);
    auto* levelPanel = new QFrame(contentPanel);
    levelPanel->setObjectName(QStringLiteral("meterCard.levelPanel"));
    levelPanel->setProperty("meterCardSection", QStringLiteral("level"));
    levelPanel->setAttribute(Qt::WA_StyledBackground);
    levelPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    levelPanel->setFixedWidth(108);
    auto* levelLayout = new QVBoxLayout(levelPanel);
    levelLayout->setContentsMargins(0, 0, 0, 0);
    levelBar_ = new WinJACKNexus::UI::MeterLevelBar(levelPanel);
    levelBar_->setObjectName(QStringLiteral("meterCard.levelBar"));
    levelBar_->setRange(-72.0F, 12.0F);
    levelBar_->setThresholds(-18.0F, 0.0F);
    levelBar_->setMarks({
        { -72.0F, QStringLiteral("-72") },
        { -60.0F, QStringLiteral("-60") },
        { -50.0F, QStringLiteral("-50") },
        { -40.0F, QStringLiteral("-40") },
        { -30.0F, QStringLiteral("-30") },
        { -20.0F, QStringLiteral("-20") },
        { -10.0F, QStringLiteral("-10") },
        { 0.0F, QStringLiteral("0") },
        { 12.0F, QStringLiteral("+12") }
    });
    levelBar_->setUnit(QStringLiteral("dBFS"));
    levelBar_->setShowScaleLabels(true);
    levelBar_->setThemePalette(cardPalette);
    levelLayout->addWidget(levelBar_);
    contentLayout->addWidget(levelPanel);

    auto* metricsPanel = new QFrame(contentPanel);
    metricsPanel->setObjectName(QStringLiteral("meterCard.metricsPanel"));
    metricsPanel->setProperty("meterCardSection", QStringLiteral("metrics"));
    metricsPanel->setAttribute(Qt::WA_StyledBackground);
    auto* metricsLayout = new QVBoxLayout(metricsPanel);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(7);
    struct MetricDefinition {
        QString objectName;
        QString label;
        QString unit;
        QColor accent;
    };
    const std::array definitions {
        MetricDefinition { QStringLiteral("Peak"), QStringLiteral("峰值（Peak）"), QStringLiteral("dBFS"), QColor(QStringLiteral("#ff5269")) },
        MetricDefinition { QStringLiteral("RMS"), QStringLiteral("均方根（RMS）"), QStringLiteral("dBFS"), QColor(QStringLiteral("#ff8a47")) },
        MetricDefinition { QStringLiteral("dBTP"), QStringLiteral("dBTP"), QStringLiteral("dBTP"), QColor(QStringLiteral("#ffd143")) },
        MetricDefinition { QStringLiteral("Momentary"), QStringLiteral("瞬时（M）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#43e88d")) },
        MetricDefinition { QStringLiteral("Short-term"), QStringLiteral("短时（S）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#48c9ff")) },
        MetricDefinition { QStringLiteral("Integrated"), QStringLiteral("综合（I）"), QStringLiteral("LUFS"), QColor(QStringLiteral("#4b95ff")) },
        MetricDefinition { QStringLiteral("LRA"), QStringLiteral("LRA"), QStringLiteral("LU"), QColor(QStringLiteral("#b96aff")) }
    };
    std::array<WinJACKNexus::UI::MetricReadout**, 7> readoutTargets {
        &peakReadout_, &rmsReadout_, &truePeakReadout_, &momentaryReadout_, &shortTermReadout_, &integratedReadout_, &lraReadout_
    };
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        auto* readout = new WinJACKNexus::UI::MetricReadout(metricsPanel);
        readout->setObjectName(QStringLiteral("meterCard.readout.%1").arg(definition.objectName));
        readout->setLabel(definition.label);
        readout->setUnit(definition.unit);
        readout->setPrecision(1);
        readout->setAccentColor(definition.accent);
        readout->setShowSurface(false);
        readout->setThemePalette(cardPalette);
        readout->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        *readoutTargets[index] = readout;
        metricsLayout->addWidget(readout, 1);
        if (index == 2) {
            auto* divider = new QFrame(metricsPanel);
            divider->setObjectName(QStringLiteral("meterCard.metricDivider"));
            divider->setProperty("meterCardSection", QStringLiteral("divider"));
            divider->setAttribute(Qt::WA_StyledBackground);
            divider->setFixedHeight(1);
            metricsLayout->addWidget(divider);
        }
    }
    contentLayout->addWidget(metricsPanel, 1);
    rootLayout->addWidget(contentPanel, 1);

    resetAction_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("重置响度"), this);
    resetAction_->setObjectName(QStringLiteral("meterCard.resetAction"));
    resetAction_->setToolTip(QStringLiteral("重置当前响度会话"));
    recordingAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("记录响度"), this);
    recordingAction_->setObjectName(QStringLiteral("meterCard.recordingAction"));
    recordingAction_->setCheckable(true);
    recordingAction_->setToolTip(QStringLiteral("开始或停止响度记录"));
    historyAction_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("历史图表"), this);
    historyAction_->setObjectName(QStringLiteral("meterCard.historyAction"));
    historyAction_->setToolTip(QStringLiteral("打开此探针的历史图表"));
    automaticResetAction_ = new QAction(QStringLiteral("自动重置"), this);
    automaticResetAction_->setObjectName(QStringLiteral("meterCard.automaticResetAction"));
    automaticResetAction_->setCheckable(true);
    locateAction_ = new QAction(QStringLiteral("在接线盘中定位"), this);
    locateAction_->setObjectName(QStringLiteral("meterCard.locateAction"));

    auto* actionsPanel = new QFrame(this);
    actionsPanel->setObjectName(QStringLiteral("meterCard.actions"));
    actionsPanel->setProperty("meterCardSection", QStringLiteral("actions"));
    actionsPanel->setAttribute(Qt::WA_StyledBackground);
    auto* actionsLayout = new QHBoxLayout(actionsPanel);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(9);
    resetButton_ = createCommandButton(resetAction_, QStringLiteral("meterCard.resetButton"), actionsPanel);
    recordingButton_ = createCommandButton(recordingAction_, QStringLiteral("meterCard.recordingButton"), actionsPanel);
    historyButton_ = createCommandButton(historyAction_, QStringLiteral("meterCard.historyButton"), actionsPanel);
    actionsLayout->addWidget(resetButton_);
    actionsLayout->addWidget(recordingButton_);
    actionsLayout->addWidget(historyButton_);
    rootLayout->addWidget(actionsPanel);

    connect(presetSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        dispatch(MeterViewCommandType::setPreset, std::nullopt, presetSelector_->itemData(index).toString());
    });
    connect(resetAction_, &QAction::triggered, this, [this] {
        dispatch(MeterViewCommandType::resetLoudness);
    });
    connect(recordingAction_, &QAction::toggled, this, [this](bool enabled) {
        dispatch(MeterViewCommandType::setRecording, enabled);
    });
    connect(historyAction_, &QAction::triggered, this, [this] {
        dispatch(MeterViewCommandType::openHistory);
        if (!instanceId_.isEmpty() && openHistory_)
            openHistory_(instanceId_);
    });
    connect(automaticResetAction_, &QAction::toggled, this, [this](bool enabled) {
        dispatch(MeterViewCommandType::setAutomaticReset, enabled);
    });
    connect(locateAction_, &QAction::triggered, this, [this] {
        dispatch(MeterViewCommandType::selectInPatchbay);
    });
}

void MeterCard::setSnapshot(const MeterViewSnapshot& snapshot)
{
    instanceId_ = snapshot.instanceId;
    clientName_ = snapshot.clientName;
    presetId_ = snapshot.presetId;
    titleLabel_->setText(snapshot.clientName);
    layoutLabel_->setText(snapshot.layoutLabel);
    stateLabel_->setText(stateText(snapshot.state));
    setToolTip(QStringLiteral("实例：%1\n名称版本：%2").arg(snapshot.instanceId).arg(snapshot.nameRevision));

    auto presetIndex = presetSelector_->findData(snapshot.presetId);
    if (presetIndex < 0) {
        presetSelector_->addItem(snapshot.presetId, snapshot.presetId);
        presetIndex = presetSelector_->count() - 1;
    }
    const QSignalBlocker presetBlocker(presetSelector_);
    presetSelector_->setCurrentIndex(presetIndex);
    const QSignalBlocker recordingBlocker(recordingAction_);
    recordingAction_->setChecked(snapshot.recordingEnabled);
    recordingAction_->setText(snapshot.recordingEnabled ? QStringLiteral("停止记录") : QStringLiteral("记录响度"));
    const QSignalBlocker automaticResetBlocker(automaticResetAction_);
    automaticResetAction_->setChecked(snapshot.automaticResetEnabled);

    levelBar_->setValue(snapshot.currentMetrics.peakDbfs);
    levelBar_->setPeakValue(snapshot.currentMetrics.truePeakDbtp);
    peakReadout_->setValue(asDouble(snapshot.currentMetrics.peakDbfs));
    rmsReadout_->setValue(asDouble(snapshot.currentMetrics.rmsDbfs));
    truePeakReadout_->setValue(asDouble(snapshot.currentMetrics.truePeakDbtp));
    momentaryReadout_->setValue(asDouble(snapshot.currentMetrics.momentaryLufs));
    shortTermReadout_->setValue(asDouble(snapshot.currentMetrics.shortTermLufs));
    integratedReadout_->setValue(asDouble(snapshot.currentMetrics.integratedLufs));
    lraReadout_->setValue(asDouble(snapshot.currentMetrics.lraLu));
}

void MeterCard::setDisplayIndex(int index)
{
    if (numberLabel_ != nullptr)
        numberLabel_->setText(QString::number(std::max(1, index)));
}

void MeterCard::setAvailableHeight(int height)
{
    const auto adjustedHeight = std::max(minimumMeterCardHeight, height);
    if (this->height() == adjustedHeight)
        return;
    setFixedHeight(adjustedHeight);
}

QString MeterCard::instanceId() const
{
    return instanceId_;
}

QString MeterCard::clientName() const
{
    return clientName_;
}

QString MeterCard::presetId() const
{
    return presetId_;
}

void MeterCard::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.addAction(historyAction_);
    menu.addAction(automaticResetAction_);
    menu.addSeparator();
    menu.addAction(resetAction_);
    menu.addAction(recordingAction_);
    menu.addSeparator();
    menu.addAction(locateAction_);
    menu.exec(event->globalPos());
}

void MeterCard::dispatch(MeterViewCommandType type, std::optional<bool> enabled, const QString& presetId)
{
    if (instanceId_.isEmpty())
        return;
    MeterViewCommand command;
    command.type = type;
    command.instanceId = instanceId_;
    command.enabled = enabled;
    command.presetId = presetId;
    if (provider_.dispatch(command) && requestRefresh_)
        requestRefresh_();
}

void MeterCard::configurePresetSelector()
{
    presetSelector_->addItem(QStringLiteral("EBU R128"), QStringLiteral("ebu-r128"));
    presetSelector_->addItem(QStringLiteral("ATSC A/85"), QStringLiteral("atsc-a85"));
    presetSelector_->addItem(QStringLiteral("流媒体 -14 LUFS"), QStringLiteral("streaming-14"));
    presetSelector_->addItem(QStringLiteral("自定义参考"), QStringLiteral("custom"));
}

}