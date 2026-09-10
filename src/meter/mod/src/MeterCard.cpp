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

QString stateText(MeterProbeState state)
{
    switch (state) {
    case MeterProbeState::silent:
        return QStringLiteral("Silent");
    case MeterProbeState::active:
        return QStringLiteral("Active");
    case MeterProbeState::overload:
        return QStringLiteral("Over limit");
    case MeterProbeState::offline:
        return QStringLiteral("Offline");
    }
    return QStringLiteral("Unknown");
}

std::optional<double> asDouble(const std::optional<float>& value)
{
    if (!value.has_value())
        return std::nullopt;
    return static_cast<double>(*value);
}

QToolButton* createCommandButton(QAction* action, const QString& objectName, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setMinimumSize(82, 58);
    return button;
}

}

MeterCard::MeterCard(MeterViewDataProvider& provider, std::function<void()> requestRefresh, QWidget* parent)
    : QFrame(parent)
    , provider_(provider)
    , requestRefresh_(std::move(requestRefresh))
{
    setObjectName(QStringLiteral("meterbridge.card"));
    setFrameShape(QFrame::StyledPanel);
    setMinimumSize(320, 610);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(8);

    auto* headerLayout = new QHBoxLayout;
    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("meterCard.clientName"));
    QFont titleFont = titleLabel_->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    titleLabel_->setFont(titleFont);
    layoutLabel_ = new QLabel(this);
    layoutLabel_->setObjectName(QStringLiteral("meterCard.layout"));
    stateLabel_ = new QLabel(this);
    stateLabel_->setObjectName(QStringLiteral("meterCard.state"));
    headerLayout->addWidget(titleLabel_, 1);
    headerLayout->addWidget(layoutLabel_);
    headerLayout->addWidget(stateLabel_);
    rootLayout->addLayout(headerLayout);

    presetSelector_ = new QComboBox(this);
    presetSelector_->setObjectName(QStringLiteral("meterCard.preset"));
    presetSelector_->setAccessibleName(QStringLiteral("Loudness preset"));
    presetSelector_->setToolTip(QStringLiteral("Selects the display reference preset for this probe"));
    configurePresetSelector();
    rootLayout->addWidget(presetSelector_);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(10);
    levelBar_ = new WinJACKNexus::UI::MeterLevelBar(this);
    levelBar_->setObjectName(QStringLiteral("meterCard.levelBar"));
    levelBar_->setRange(-70.0F, 12.0F);
    levelBar_->setThresholds(-18.0F, 0.0F);
    levelBar_->setMarks({
        { -70.0F, QStringLiteral("-70") },
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
    contentLayout->addWidget(levelBar_);

    auto* metricsLayout = new QVBoxLayout;
    metricsLayout->setSpacing(5);
    const std::array definitions {
        std::pair { QStringLiteral("Peak"), QStringLiteral("dBFS") },
        std::pair { QStringLiteral("RMS"), QStringLiteral("dBFS") },
        std::pair { QStringLiteral("dBTP"), QStringLiteral("dBTP") },
        std::pair { QStringLiteral("Momentary"), QStringLiteral("LUFS") },
        std::pair { QStringLiteral("Short-term"), QStringLiteral("LUFS") },
        std::pair { QStringLiteral("Integrated"), QStringLiteral("LUFS") },
        std::pair { QStringLiteral("LRA"), QStringLiteral("LU") }
    };
    std::array<WinJACKNexus::UI::MetricReadout**, 7> readoutTargets {
        &peakReadout_, &rmsReadout_, &truePeakReadout_, &momentaryReadout_, &shortTermReadout_, &integratedReadout_, &lraReadout_
    };
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        auto* readout = new WinJACKNexus::UI::MetricReadout(this);
        readout->setObjectName(QStringLiteral("meterCard.readout.%1").arg(definitions[index].first));
        readout->setLabel(definitions[index].first);
        readout->setUnit(definitions[index].second);
        readout->setPrecision(1);
        *readoutTargets[index] = readout;
        metricsLayout->addWidget(readout);
    }
    contentLayout->addLayout(metricsLayout, 1);
    rootLayout->addLayout(contentLayout, 1);

    resetAction_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Reset loudness"), this);
    resetAction_->setObjectName(QStringLiteral("meterCard.resetAction"));
    resetAction_->setToolTip(QStringLiteral("Reset the loudness session"));
    recordingAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Record loudness"), this);
    recordingAction_->setObjectName(QStringLiteral("meterCard.recordingAction"));
    recordingAction_->setCheckable(true);
    recordingAction_->setToolTip(QStringLiteral("Start or stop loudness recording"));
    historyAction_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("History chart"), this);
    historyAction_->setObjectName(QStringLiteral("meterCard.historyAction"));
    historyAction_->setToolTip(QStringLiteral("Open the history chart for this probe"));
    automaticResetAction_ = new QAction(QStringLiteral("Automatic reset"), this);
    automaticResetAction_->setObjectName(QStringLiteral("meterCard.automaticResetAction"));
    automaticResetAction_->setCheckable(true);
    locateAction_ = new QAction(QStringLiteral("Locate in Patchbay"), this);
    locateAction_->setObjectName(QStringLiteral("meterCard.locateAction"));

    auto* actionsLayout = new QHBoxLayout;
    actionsLayout->setSpacing(6);
    resetButton_ = createCommandButton(resetAction_, QStringLiteral("meterCard.resetButton"), this);
    recordingButton_ = createCommandButton(recordingAction_, QStringLiteral("meterCard.recordingButton"), this);
    historyButton_ = createCommandButton(historyAction_, QStringLiteral("meterCard.historyButton"), this);
    actionsLayout->addWidget(resetButton_);
    actionsLayout->addWidget(recordingButton_);
    actionsLayout->addWidget(historyButton_);
    rootLayout->addLayout(actionsLayout);

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
    setToolTip(QStringLiteral("%1\nName revision %2").arg(snapshot.instanceId).arg(snapshot.nameRevision));

    auto presetIndex = presetSelector_->findData(snapshot.presetId);
    if (presetIndex < 0) {
        presetSelector_->addItem(snapshot.presetId, snapshot.presetId);
        presetIndex = presetSelector_->count() - 1;
    }
    const QSignalBlocker presetBlocker(presetSelector_);
    presetSelector_->setCurrentIndex(presetIndex);
    const QSignalBlocker recordingBlocker(recordingAction_);
    recordingAction_->setChecked(snapshot.recordingEnabled);
    recordingAction_->setText(snapshot.recordingEnabled ? QStringLiteral("Stop recording") : QStringLiteral("Record loudness"));
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
    presetSelector_->addItem(QStringLiteral("Streaming -14 LUFS"), QStringLiteral("streaming-14"));
    presetSelector_->addItem(QStringLiteral("Custom reference"), QStringLiteral("custom"));
}

}