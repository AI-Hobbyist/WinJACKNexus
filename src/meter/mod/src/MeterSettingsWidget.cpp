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
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>

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

namespace Controls = WinJACKNexus::UI::Controls;

namespace {
using WinJACKNexus::UI::bindLocalizedText;
using WinJACKNexus::UI::localizedText;
Controls::TextLabel* settingsLabel(const char* key, const char* english, QWidget* parent) {
    auto* label = new Controls::TextLabel({}, parent);
    bindLocalizedText(label, "text", "meterbridge", key, english);
    return label;
}
}


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

    auto* globalGroup = new Controls::GroupPanel(localizedText("meterbridge", "settings.display", "Display settings"), this);
    bindLocalizedText(globalGroup, "title", "meterbridge", "settings.display", "Display settings");
    globalGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    globalGroup->setObjectName(QStringLiteral("meterSettings.globalGroup"));
    auto* globalLayout = new QFormLayout(globalGroup);
    globalLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    globalLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    silenceThresholdSpinBox_ = new Controls::DecimalField(globalGroup);
    silenceThresholdSpinBox_->setFixedHeight(Controls::sidebarControlHeight);
    silenceThresholdSpinBox_->setObjectName(QStringLiteral("meterSettings.silenceThreshold"));
    bindLocalizedText(silenceThresholdSpinBox_, "accessibleName", "meterbridge", "settings.threshold", "Silence threshold");
    silenceThresholdSpinBox_->setRange(-144.0, 0.0);
    silenceThresholdSpinBox_->setDecimals(1);
    silenceThresholdSpinBox_->setSuffix(QStringLiteral(" dBFS"));
    globalLayout->addRow(settingsLabel("settings.threshold", "Silence threshold", globalGroup), silenceThresholdSpinBox_);

    silenceDurationSpinBox_ = new Controls::DecimalField(globalGroup);
    silenceDurationSpinBox_->setFixedHeight(Controls::sidebarControlHeight);
    silenceDurationSpinBox_->setObjectName(QStringLiteral("meterSettings.silenceDuration"));
    bindLocalizedText(silenceDurationSpinBox_, "accessibleName", "meterbridge", "settings.duration", "Silence duration");
    silenceDurationSpinBox_->setRange(0.1, 60.0);
    silenceDurationSpinBox_->setDecimals(1);
    silenceDurationSpinBox_->setSuffix(QStringLiteral(" s"));
    globalLayout->addRow(settingsLabel("settings.duration", "Silence duration", globalGroup), silenceDurationSpinBox_);

    logRootDirectoryEdit_ = new Controls::TextField(globalGroup);
    logRootDirectoryEdit_->setFixedHeight(Controls::sidebarControlHeight);
    logRootDirectoryEdit_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    logRootDirectoryEdit_->setObjectName(QStringLiteral("meterSettings.logRoot"));
    bindLocalizedText(logRootDirectoryEdit_, "accessibleName", "meterbridge", "settings.logRoot", "CSV log root directory");
    bindLocalizedText(logRootDirectoryEdit_, "toolTip", "meterbridge", "settings.logHelp", "Root directory for live CSV logs. Choose the PNG export location separately. Leave empty to disable CSV output.");
    globalLayout->addRow(settingsLabel("settings.csvDirectory", "CSV directory", globalGroup), logRootDirectoryEdit_);
    rootLayout->addWidget(globalGroup);

    customChannelsGroup_ = new Controls::GroupPanel(localizedText("meterbridge", "settings.customChannels", "Custom channels"), this);
    bindLocalizedText(customChannelsGroup_, "title", "meterbridge", "settings.customChannels", "Custom channels");
    customChannelsGroup_->setObjectName(QStringLiteral("meterSettings.customChannels"));
    auto* customLayout = new QVBoxLayout(customChannelsGroup_);
    customLayout->setContentsMargins(6, 8, 6, 6);
    auto* countLayout = new QHBoxLayout;
    countLayout->addWidget(settingsLabel("settings.channelCount", "Channel count", customChannelsGroup_));
    customChannelCountSpinBox_ = new Controls::IntegerField(customChannelsGroup_);
    customChannelCountSpinBox_->setFixedHeight(Controls::sidebarControlHeight);
    customChannelCountSpinBox_->setObjectName(QStringLiteral("meterSettings.channelCount"));
    bindLocalizedText(customChannelCountSpinBox_, "accessibleName", "meterbridge", "settings.customCount", "Custom channel count");
    customChannelCountSpinBox_->setRange(1, maximumMeterChannelCount);
    countLayout->addWidget(customChannelCountSpinBox_, 1);
    customLayout->addLayout(countLayout);

    channelTable_ = new Controls::TableField(customChannelsGroup_);
    channelTable_->setObjectName(QStringLiteral("meterSettings.channelTable"));
    bindLocalizedText(channelTable_, "accessibleName", "meterbridge", "settings.assignment", "Custom channel assignment");
    channelTable_->setColumnCount(3);
    channelTable_->setHorizontalHeaderLabels({ localizedText("meterbridge", "settings.name", "Name"), localizedText("meterbridge", "settings.role", "Role"), QStringLiteral("LFE") });
    if (auto* manager = WinJACKNexus::UI::I18nManager::installed())
        manager->bind(channelTable_, [this] {
            const QSignalBlocker blocker(channelTable_);
            channelTable_->setHorizontalHeaderLabels({
                localizedText("meterbridge", "settings.name", "Name"),
                localizedText("meterbridge", "settings.role", "Role"), QStringLiteral("LFE") });
        });
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
    if (!logRootDirectoryEdit_->hasFocus())
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

        auto* nameEditor = new Controls::TextField(channelTable_);
        nameEditor->setText(channel.displayName);
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

        auto* roleSelector = new Controls::ChoiceField(channelTable_);
        roleSelector->setFixedHeight(Controls::sidebarControlHeight);
        roleSelector->setObjectName(QStringLiteral("meterSettings.channelRole.%1").arg(index));
        for (const auto& role : { QStringLiteral("M+030"), QStringLiteral("M-030"), QStringLiteral("M+000"),
                                 QStringLiteral("LFE"), QStringLiteral("M+110"), QStringLiteral("M-110"),
                                 QStringLiteral("M+135"), QStringLiteral("M-135"), QStringLiteral("U+000") })
            roleSelector->addItem(role, role);
        auto roleIndex = roleSelector->findData(channel.speakerRole);
        if (roleIndex < 0) {
            roleSelector->addItem(channel.speakerRole, channel.speakerRole);
            roleIndex = roleSelector->count() - 1;
        }
        roleSelector->setCurrentIndex(roleIndex);
        const auto refreshRole = [roleSelector] {
            const QSignalBlocker blocker(roleSelector);
            const auto index = roleSelector->findData(QStringLiteral("未分配"));
            if (index >= 0) roleSelector->setItemText(index,
                localizedText("meterbridge", "settings.unassigned", "Unassigned"));
        };
        if (auto* manager = WinJACKNexus::UI::I18nManager::installed()) manager->bind(roleSelector, refreshRole);
        else refreshRole();
        roleSelector->setEnabled(channelLayoutEditable_);
        channelTable_->setCellWidget(row, 1, roleSelector);
        connect(roleSelector, &QComboBox::currentIndexChanged, this, [this, index, roleSelector](int) {
            MeterViewCommand command;
            command.type = MeterViewCommandType::setChannelRole;
            command.channelIndex = static_cast<std::uint16_t>(index);
            command.textValue = roleSelector->currentData().toString();
            dispatch(std::move(command));
        });

        auto* lfeCheckBox = new Controls::ToggleField({}, channelTable_);
        lfeCheckBox->setObjectName(QStringLiteral("meterSettings.channelLfe.%1").arg(index));
        const auto updateLfeName = [lfeCheckBox, index] {
            lfeCheckBox->setAccessibleName(localizedText("meterbridge", "settings.channelLfe", "LFE flag for channel %1").arg(index + 1));
        };
        if (auto* manager = WinJACKNexus::UI::I18nManager::installed()) manager->bind(lfeCheckBox, updateLfeName);
        else updateLfeName();
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
