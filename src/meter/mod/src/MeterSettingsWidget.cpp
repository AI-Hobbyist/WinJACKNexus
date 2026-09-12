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
#include <WinJACKNexus/Meter/Mod/MeterSettingsWidget.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace WinJACKNexus::Meter::Mod {

MeterSettingsWidget::MeterSettingsWidget(MeterViewDataProvider& provider,
                                         std::function<void()> requestRefresh,
                                         QWidget* parent)
    : QFrame(parent)
    , provider_(provider)
    , requestRefresh_(std::move(requestRefresh))
{
    setObjectName(QStringLiteral("meterSettings.widget"));
    setFrameShape(QFrame::StyledPanel);
    setMinimumWidth(250);
    setMaximumWidth(340);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    auto* globalGroup = new QGroupBox(QStringLiteral("显示设置"), this);
    globalGroup->setObjectName(QStringLiteral("meterSettings.globalGroup"));
    auto* globalLayout = new QFormLayout(globalGroup);
    globalLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    silenceThresholdSpinBox_ = new QDoubleSpinBox(globalGroup);
    silenceThresholdSpinBox_->setObjectName(QStringLiteral("meterSettings.silenceThreshold"));
    silenceThresholdSpinBox_->setAccessibleName(QStringLiteral("静音阈值"));
    silenceThresholdSpinBox_->setRange(-144.0, 0.0);
    silenceThresholdSpinBox_->setDecimals(1);
    silenceThresholdSpinBox_->setSuffix(QStringLiteral(" dBFS"));
    globalLayout->addRow(QStringLiteral("静音阈值"), silenceThresholdSpinBox_);

    silenceDurationSpinBox_ = new QDoubleSpinBox(globalGroup);
    silenceDurationSpinBox_->setObjectName(QStringLiteral("meterSettings.silenceDuration"));
    silenceDurationSpinBox_->setAccessibleName(QStringLiteral("静音时长"));
    silenceDurationSpinBox_->setRange(0.1, 60.0);
    silenceDurationSpinBox_->setDecimals(1);
    silenceDurationSpinBox_->setSuffix(QStringLiteral(" 秒"));
    globalLayout->addRow(QStringLiteral("静音时长"), silenceDurationSpinBox_);

    logRootDirectoryEdit_ = new QLineEdit(globalGroup);
    logRootDirectoryEdit_->setObjectName(QStringLiteral("meterSettings.logRoot"));
    logRootDirectoryEdit_->setAccessibleName(QStringLiteral("CSV 日志根目录"));
    logRootDirectoryEdit_->setToolTip(QStringLiteral("仅用于预览的 CSV 日志根目录"));
    globalLayout->addRow(QStringLiteral("日志目录"), logRootDirectoryEdit_);
    rootLayout->addWidget(globalGroup);

    customChannelsGroup_ = new QGroupBox(QStringLiteral("自定义声道"), this);
    customChannelsGroup_->setObjectName(QStringLiteral("meterSettings.customChannels"));
    auto* customLayout = new QVBoxLayout(customChannelsGroup_);
    customLayout->setContentsMargins(6, 8, 6, 6);
    auto* countLayout = new QHBoxLayout;
    countLayout->addWidget(new QLabel(QStringLiteral("声道数"), customChannelsGroup_));
    customChannelCountSpinBox_ = new QSpinBox(customChannelsGroup_);
    customChannelCountSpinBox_->setObjectName(QStringLiteral("meterSettings.channelCount"));
    customChannelCountSpinBox_->setAccessibleName(QStringLiteral("自定义声道数量"));
    customChannelCountSpinBox_->setRange(1, maximumMeterChannelCount);
    countLayout->addWidget(customChannelCountSpinBox_, 1);
    customLayout->addLayout(countLayout);

    channelTable_ = new QTableWidget(customChannelsGroup_);
    channelTable_->setObjectName(QStringLiteral("meterSettings.channelTable"));
    channelTable_->setAccessibleName(QStringLiteral("自定义声道分配"));
    channelTable_->setColumnCount(3);
    channelTable_->setHorizontalHeaderLabels({ QStringLiteral("名称"), QStringLiteral("角色"), QStringLiteral("LFE") });
    channelTable_->verticalHeader()->setVisible(false);
    channelTable_->horizontalHeader()->setStretchLastSection(false);
    channelTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    channelTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    channelTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    channelTable_->setMinimumHeight(180);
    customLayout->addWidget(channelTable_, 1);
    rootLayout->addWidget(customChannelsGroup_, 1);

    connect(silenceThresholdSpinBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setGlobalSilenceThreshold;
        command.numericValue = static_cast<float>(value);
        dispatch(std::move(command));
    });
    connect(silenceDurationSpinBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setGlobalSilenceDuration;
        command.numericValue = static_cast<float>(value);
        dispatch(std::move(command));
    });
    connect(logRootDirectoryEdit_, &QLineEdit::editingFinished, this, [this] {
        MeterViewCommand command;
        command.type = MeterViewCommandType::setGlobalLogRootDirectory;
        command.textValue = logRootDirectoryEdit_->text().trimmed();
        dispatch(std::move(command));
    });
    connect(customChannelCountSpinBox_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int count) {
        if (!channelLayoutEditable_)
            return;
        MeterViewCommand command;
        command.type = MeterViewCommandType::setCustomChannelCount;
        command.channelCount = static_cast<std::uint16_t>(count);
        dispatch(std::move(command));
    });
}

void MeterSettingsWidget::setSnapshot(const MeterViewSnapshot& snapshot)
{
    instanceId_ = snapshot.instanceId;
    channelLayoutEditable_ = snapshot.channelLayoutEditable;

    const QSignalBlocker thresholdBlocker(silenceThresholdSpinBox_);
    silenceThresholdSpinBox_->setValue(snapshot.globalSettings.silenceThresholdDbfs);
    const QSignalBlocker durationBlocker(silenceDurationSpinBox_);
    silenceDurationSpinBox_->setValue(snapshot.globalSettings.silenceDurationSeconds);
    const QSignalBlocker logRootBlocker(logRootDirectoryEdit_);
    logRootDirectoryEdit_->setText(snapshot.globalSettings.logRootDirectory);
    const QSignalBlocker countBlocker(customChannelCountSpinBox_);
    customChannelCountSpinBox_->setValue(std::max(1, static_cast<int>(snapshot.channels.size())));
    customChannelsGroup_->setEnabled(channelLayoutEditable_);
    customChannelsGroup_->setVisible(channelLayoutEditable_);
    rebuildChannelTable(snapshot);
}

QString MeterSettingsWidget::instanceId() const
{
    return instanceId_;
}

bool MeterSettingsWidget::channelLayoutEditable() const noexcept
{
    return channelLayoutEditable_;
}

void MeterSettingsWidget::dispatch(MeterViewCommand command)
{
    if (instanceId_.isEmpty())
        return;
    command.instanceId = instanceId_;
    if (provider_.dispatch(command) && requestRefresh_)
        requestRefresh_();
}

void MeterSettingsWidget::rebuildChannelTable(const MeterViewSnapshot& snapshot)
{
    channelTable_->clearContents();
    channelTable_->setRowCount(static_cast<int>(snapshot.channels.size()));
    for (std::size_t index = 0; index < snapshot.channels.size(); ++index) {
        const auto& channel = snapshot.channels[index];
        const auto row = static_cast<int>(index);

        auto* nameEditor = new QLineEdit(channel.displayName, channelTable_);
        nameEditor->setObjectName(QStringLiteral("meterSettings.channelName.%1").arg(index));
        nameEditor->setEnabled(channelLayoutEditable_);
        channelTable_->setCellWidget(row, 0, nameEditor);
        connect(nameEditor, &QLineEdit::editingFinished, this, [this, index, nameEditor] {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setChannelDisplayName;
            command.channelIndex = static_cast<std::uint16_t>(index);
            command.textValue = nameEditor->text().trimmed();
            dispatch(std::move(command));
        });

        auto* roleSelector = new QComboBox(channelTable_);
        roleSelector->setObjectName(QStringLiteral("meterSettings.channelRole.%1").arg(index));
        roleSelector->addItems({ QStringLiteral("M+030"), QStringLiteral("M-030"), QStringLiteral("M+000"),
                                 QStringLiteral("LFE"), QStringLiteral("M+110"), QStringLiteral("M-110"),
                                 QStringLiteral("M+135"), QStringLiteral("M-135"), QStringLiteral("U+000") });
        auto roleIndex = roleSelector->findText(channel.speakerRole);
        if (roleIndex < 0) {
            roleSelector->addItem(channel.speakerRole);
            roleIndex = roleSelector->count() - 1;
        }
        roleSelector->setCurrentIndex(roleIndex);
        roleSelector->setEnabled(channelLayoutEditable_);
        channelTable_->setCellWidget(row, 1, roleSelector);
        connect(roleSelector, &QComboBox::currentIndexChanged, this, [this, index, roleSelector](int) {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setChannelRole;
            command.channelIndex = static_cast<std::uint16_t>(index);
            command.textValue = roleSelector->currentText();
            dispatch(std::move(command));
        });

        auto* lfeCheckBox = new QCheckBox(channelTable_);
        lfeCheckBox->setObjectName(QStringLiteral("meterSettings.channelLfe.%1").arg(index));
        lfeCheckBox->setAccessibleName(QStringLiteral("声道 %1 的 LFE 标记").arg(index + 1));
        lfeCheckBox->setChecked(channel.isLfe);
        lfeCheckBox->setEnabled(channelLayoutEditable_);
        channelTable_->setCellWidget(row, 2, lfeCheckBox);
        connect(lfeCheckBox, &QCheckBox::toggled, this, [this, index](bool enabled) {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setChannelLfe;
            command.channelIndex = static_cast<std::uint16_t>(index);
            command.enabled = enabled;
            dispatch(std::move(command));
        });
    }
}

}