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
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/ChannelLayout.hpp>
#include <WinJACKNexus/Common/LoudnessEngine.hpp>
#include <WinJACKNexus/Common/MeterProbeRegistry.hpp>
#include <WinJACKNexus/Common/SilenceResetController.hpp>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using WinJACKNexus::Audio::JackClient;
using WinJACKNexus::Audio::JackPort;
using WinJACKNexus::Common::LoudnessEngine;
using WinJACKNexus::Common::LoudnessEngineConfiguration;
using WinJACKNexus::Common::MeterChannelLayout;
using WinJACKNexus::Common::MeterChannelRole;
using WinJACKNexus::Common::MeterProbeDescriptor;
using WinJACKNexus::Common::MeterProbeRealtimeState;
using WinJACKNexus::Common::SilenceResetController;

constexpr std::size_t maximumChannelCount = MeterChannelLayout::maximumChannelCount;

enum class ProbeKind : std::uint8_t {
    mono,
    stereo,
    fivePointOne,
    sevenPointOne,
    custom
};

struct ProbeDefinition final {
    const char* name;
    const char* description;
    const char* defaultClientName;
    ProbeKind kind;
};

constexpr std::array probeDefinitions {
    ProbeDefinition { "Meter Mono", "Single-channel Meterbridge probe.", "Meter-Mono", ProbeKind::mono },
    ProbeDefinition { "Meter Stereo", "Two-channel Meterbridge probe.", "Meter-Stereo", ProbeKind::stereo },
    ProbeDefinition { "Meter 5.1", "5.1-channel Meterbridge probe.", "Meter-5.1", ProbeKind::fivePointOne },
    ProbeDefinition { "Meter 7.1", "7.1-channel Meterbridge probe.", "Meter-7.1", ProbeKind::sevenPointOne },
    ProbeDefinition { "Meter Custom", "Custom-channel Meterbridge probe.", "Meter-Custom", ProbeKind::custom }
};

struct ProbeChannelSettings final {
    std::string name;
    MeterChannelRole role { MeterChannelRole::unassigned };
    bool lfe {};
};

struct ProbeSettings final {
    std::string instanceId;
    std::string clientName;
    std::string presetId { "ebu-r128" };
    std::array<ProbeChannelSettings, maximumChannelCount> channels {};
    std::size_t channelCount {};
    bool followGlobalPreset { true };
    bool autoReset { true };
    double silenceThresholdDbfs { -70.0 };
    double silenceDurationSeconds { 3.0 };
};

struct JackConnection final {
    std::string sourcePortId;
    std::string destinationPortId;
};

struct JackConnectionCapture final {
    std::vector<JackConnection>* connections = nullptr;
    const char* localPortId = nullptr;
    bool localIsOutput = false;
};

std::atomic_uint64_t nextProbeInstanceId { 1 };

const char* probeKindId(ProbeKind kind) noexcept
{
    switch (kind) {
    case ProbeKind::mono:
        return "mono";
    case ProbeKind::stereo:
        return "stereo";
    case ProbeKind::fivePointOne:
        return "fivePointOne";
    case ProbeKind::sevenPointOne:
        return "sevenPointOne";
    case ProbeKind::custom:
        return "custom";
    }
    return "custom";
}

MeterChannelLayout defaultLayoutFor(ProbeKind kind) noexcept
{
    switch (kind) {
    case ProbeKind::mono:
        return MeterChannelLayout::mono();
    case ProbeKind::stereo:
        return MeterChannelLayout::stereo();
    case ProbeKind::fivePointOne:
    case ProbeKind::custom:
        return MeterChannelLayout::fivePointOne();
    case ProbeKind::sevenPointOne:
        return MeterChannelLayout::sevenPointOne();
    }
    return MeterChannelLayout::mono();
}

const char* roleId(MeterChannelRole role) noexcept
{
    switch (role) {
    case MeterChannelRole::mono:
        return "mono";
    case MeterChannelRole::frontLeft:
        return "frontLeft";
    case MeterChannelRole::frontRight:
        return "frontRight";
    case MeterChannelRole::frontCenter:
        return "frontCenter";
    case MeterChannelRole::sideLeft:
        return "sideLeft";
    case MeterChannelRole::sideRight:
        return "sideRight";
    case MeterChannelRole::rearLeft:
        return "rearLeft";
    case MeterChannelRole::rearRight:
        return "rearRight";
    case MeterChannelRole::topFrontLeft:
        return "topFrontLeft";
    case MeterChannelRole::topFrontRight:
        return "topFrontRight";
    case MeterChannelRole::topRearLeft:
        return "topRearLeft";
    case MeterChannelRole::topRearRight:
        return "topRearRight";
    case MeterChannelRole::lfe:
        return "lfe";
    case MeterChannelRole::unassigned:
        return "unassigned";
    }
    return "unassigned";
}

bool roleFromId(std::string_view id, MeterChannelRole& role) noexcept
{
    constexpr std::array roles {
        std::pair { "mono", MeterChannelRole::mono },
        std::pair { "frontLeft", MeterChannelRole::frontLeft },
        std::pair { "frontRight", MeterChannelRole::frontRight },
        std::pair { "frontCenter", MeterChannelRole::frontCenter },
        std::pair { "sideLeft", MeterChannelRole::sideLeft },
        std::pair { "sideRight", MeterChannelRole::sideRight },
        std::pair { "rearLeft", MeterChannelRole::rearLeft },
        std::pair { "rearRight", MeterChannelRole::rearRight },
        std::pair { "topFrontLeft", MeterChannelRole::topFrontLeft },
        std::pair { "topFrontRight", MeterChannelRole::topFrontRight },
        std::pair { "topRearLeft", MeterChannelRole::topRearLeft },
        std::pair { "topRearRight", MeterChannelRole::topRearRight },
        std::pair { "lfe", MeterChannelRole::lfe },
        std::pair { "unassigned", MeterChannelRole::unassigned }
    };
    for (const auto& [name, candidate] : roles) {
        if (id == name) {
            role = candidate;
            return true;
        }
    }
    return false;
}

std::string defaultChannelName(MeterChannelRole role, std::size_t index)
{
    switch (role) {
    case MeterChannelRole::mono:
        return "M";
    case MeterChannelRole::frontLeft:
        return "L";
    case MeterChannelRole::frontRight:
        return "R";
    case MeterChannelRole::frontCenter:
        return "C";
    case MeterChannelRole::sideLeft:
        return "Ls";
    case MeterChannelRole::sideRight:
        return "Rs";
    case MeterChannelRole::rearLeft:
        return "Lb";
    case MeterChannelRole::rearRight:
        return "Rb";
    case MeterChannelRole::topFrontLeft:
        return "TFL";
    case MeterChannelRole::topFrontRight:
        return "TFR";
    case MeterChannelRole::topRearLeft:
        return "TRL";
    case MeterChannelRole::topRearRight:
        return "TRR";
    case MeterChannelRole::lfe:
        return "LFE";
    case MeterChannelRole::unassigned:
        return "Ch " + std::to_string(index + 1);
    }
    return "Ch " + std::to_string(index + 1);
}

std::string makeInstanceId()
{
    return "meter-probe-" + std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(nextProbeInstanceId.fetch_add(1, std::memory_order_relaxed));
}

void applyLayout(ProbeSettings& settings, const MeterChannelLayout& layout)
{
    settings.channelCount = layout.channelCount();
    for (std::size_t index = 0; index < settings.channelCount; ++index) {
        const auto* channel = layout.channel(index);
        if (channel == nullptr)
            continue;
        settings.channels[index] = {
            defaultChannelName(channel->role, index),
            channel->role,
            channel->lfe
        };
    }
}

ProbeSettings makeDefaultSettings(const ProbeDefinition& definition)
{
    ProbeSettings settings;
    settings.instanceId = makeInstanceId();
    settings.clientName = definition.defaultClientName;
    QSettings globalSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
    const auto presetId = globalSettings.value(QStringLiteral("defaultPresetId"), QStringLiteral("ebu-r128")).toString().trimmed();
    settings.presetId = presetId.isEmpty() ? std::string("ebu-r128") : presetId.toUtf8().toStdString();
    const auto silenceThreshold = globalSettings.value(QStringLiteral("silenceThresholdDbfs"), -70.0).toDouble();
    if (std::isfinite(silenceThreshold) && silenceThreshold >= -144.0 && silenceThreshold <= 0.0)
        settings.silenceThresholdDbfs = silenceThreshold;
    const auto silenceDuration = globalSettings.value(QStringLiteral("silenceDurationSeconds"), 3.0).toDouble();
    if (std::isfinite(silenceDuration) && silenceDuration >= 0.1 && silenceDuration <= 60.0)
        settings.silenceDurationSeconds = silenceDuration;
    applyLayout(settings, defaultLayoutFor(definition.kind));
    return settings;
}

bool isFixedLayout(ProbeKind kind) noexcept
{
    return kind != ProbeKind::custom;
}

bool isValidClientName(std::string_view name) noexcept
{
    if (name.empty() || name.size() >= 128)
        return false;
    for (const auto character : name) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || character == ':')
            return false;
    }
    return true;
}

bool isValidPortName(std::string_view name) noexcept
{
    return isValidClientName(name);
}

bool isValidSettings(const ProbeSettings& settings, ProbeKind kind) noexcept
{
    if (settings.instanceId.empty() || settings.presetId.empty()
        || !isValidClientName(settings.clientName)
        || settings.channelCount == 0 || settings.channelCount > maximumChannelCount
        || !std::isfinite(settings.silenceThresholdDbfs)
        || !std::isfinite(settings.silenceDurationSeconds)
        || settings.silenceDurationSeconds < 0.0)
        return false;

    MeterChannelLayout layout;
    if (!layout.setChannelCount(settings.channelCount))
        return false;
    for (std::size_t index = 0; index < settings.channelCount; ++index) {
        const auto& channel = settings.channels[index];
        if (!isValidPortName(channel.name) || !layout.setChannel(index, channel.role, channel.lfe))
            return false;
        for (std::size_t otherIndex = 0; otherIndex < index; ++otherIndex) {
            if (channel.name == settings.channels[otherIndex].name)
                return false;
        }
    }
    if (!layout.hasMeasuredChannels())
        return false;

    if (!isFixedLayout(kind))
        return true;

    const auto expected = defaultLayoutFor(kind);
    if (settings.channelCount != expected.channelCount())
        return false;
    for (std::size_t index = 0; index < settings.channelCount; ++index) {
        const auto* expectedChannel = expected.channel(index);
        if (expectedChannel == nullptr
            || settings.channels[index].role != expectedChannel->role
            || settings.channels[index].lfe != expectedChannel->lfe)
            return false;
    }
    return true;
}

MeterChannelLayout layoutFor(const ProbeSettings& settings) noexcept
{
    MeterChannelLayout layout;
    if (!layout.setChannelCount(settings.channelCount))
        return {};
    for (std::size_t index = 0; index < settings.channelCount; ++index) {
        const auto& channel = settings.channels[index];
        if (!layout.setChannel(index, channel.role, channel.lfe))
            return {};
    }
    return layout;
}

void resizeCustomLayout(ProbeSettings& settings, std::size_t channelCount)
{
    const auto previousCount = settings.channelCount;
    settings.channelCount = channelCount;
    for (std::size_t index = previousCount; index < channelCount; ++index) {
        settings.channels[index] = {
            defaultChannelName(MeterChannelRole::unassigned, index),
            MeterChannelRole::unassigned,
            false
        };
    }
}

bool readOptionalString(const QJsonObject& object, const char* key, std::string& output)
{
    const auto jsonKey = QString::fromLatin1(key);
    if (!object.contains(jsonKey))
        return true;
    const auto value = object.value(jsonKey);
    if (!value.isString())
        return false;
    output = value.toString().toUtf8().toStdString();
    return true;
}

bool readOptionalBool(const QJsonObject& object, const char* key, bool& output) noexcept
{
    const auto jsonKey = QString::fromLatin1(key);
    if (!object.contains(jsonKey))
        return true;
    const auto value = object.value(jsonKey);
    if (!value.isBool())
        return false;
    output = value.toBool();
    return true;
}

bool readOptionalNumber(const QJsonObject& object, const char* key, double& output) noexcept
{
    const auto jsonKey = QString::fromLatin1(key);
    if (!object.contains(jsonKey))
        return true;
    const auto value = object.value(jsonKey);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        return false;
    output = value.toDouble();
    return true;
}

bool parseChannelSettings(const QJsonArray& channels, ProbeSettings& settings)
{
    if (channels.size() != static_cast<qsizetype>(settings.channelCount))
        return false;
    for (std::size_t index = 0; index < settings.channelCount; ++index) {
        const auto value = channels.at(static_cast<qsizetype>(index));
        if (!value.isObject())
            return false;
        const auto object = value.toObject();
        ProbeChannelSettings channel = settings.channels[index];
        std::string role;
        if (!readOptionalString(object, "name", channel.name)
            || !readOptionalString(object, "role", role)
            || !readOptionalBool(object, "lfe", channel.lfe)
            || role.empty()
            || !roleFromId(role, channel.role))
            return false;
        settings.channels[index] = std::move(channel);
    }
    return true;
}

bool parseSettingsJson(const char* json,
                       ProbeKind kind,
                       const ProbeSettings& current,
                       ProbeSettings& settings)
{
    if (json == nullptr)
        return false;

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const auto object = document.object();
    if (object.contains(QStringLiteral("schemaVersion"))) {
        const auto value = object.value(QStringLiteral("schemaVersion"));
        if (!value.isDouble() || value.toDouble() != 1.0)
            return false;
    }
    if (object.contains(QStringLiteral("nodeKind"))) {
        std::string nodeKind;
        if (!readOptionalString(object, "nodeKind", nodeKind) || nodeKind != probeKindId(kind))
            return false;
    }

    settings = current;
    if (!readOptionalString(object, "instanceId", settings.instanceId)
        || !readOptionalString(object, "clientName", settings.clientName)
        || !readOptionalString(object, "presetId", settings.presetId)
        || !readOptionalBool(object, "followGlobalPreset", settings.followGlobalPreset)
        || !readOptionalBool(object, "autoReset", settings.autoReset)
        || !readOptionalNumber(object, "silenceThresholdDbfs", settings.silenceThresholdDbfs)
        || !readOptionalNumber(object, "silenceDurationSeconds", settings.silenceDurationSeconds))
        return false;

    if (object.contains(QStringLiteral("channelCount"))) {
        const auto value = object.value(QStringLiteral("channelCount"));
        const auto count = value.toDouble(-1.0);
        if (!value.isDouble() || !std::isfinite(count) || std::floor(count) != count
            || count < 1.0 || count > static_cast<double>(maximumChannelCount))
            return false;
        if (kind == ProbeKind::custom)
            resizeCustomLayout(settings, static_cast<std::size_t>(count));
        else
            settings.channelCount = static_cast<std::size_t>(count);
    }

    if (object.contains(QStringLiteral("channels"))) {
        const auto value = object.value(QStringLiteral("channels"));
        if (!value.isArray() || !parseChannelSettings(value.toArray(), settings))
            return false;
    }

    return isValidSettings(settings, kind);
}

QJsonObject settingsObject(const ProbeSettings& settings, ProbeKind kind)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("nodeKind"), QString::fromLatin1(probeKindId(kind)));
    object.insert(QStringLiteral("instanceId"), QString::fromUtf8(settings.instanceId.c_str()));
    object.insert(QStringLiteral("clientName"), QString::fromUtf8(settings.clientName.c_str()));
    object.insert(QStringLiteral("presetId"), QString::fromUtf8(settings.presetId.c_str()));
    object.insert(QStringLiteral("followGlobalPreset"), settings.followGlobalPreset);
    object.insert(QStringLiteral("autoReset"), settings.autoReset);
    object.insert(QStringLiteral("silenceThresholdDbfs"), settings.silenceThresholdDbfs);
    object.insert(QStringLiteral("silenceDurationSeconds"), settings.silenceDurationSeconds);
    object.insert(QStringLiteral("channelCount"), static_cast<int>(settings.channelCount));

    QJsonArray channels;
    for (std::size_t index = 0; index < settings.channelCount; ++index) {
        const auto& channel = settings.channels[index];
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), QString::fromUtf8(channel.name.c_str()));
        entry.insert(QStringLiteral("role"), QString::fromLatin1(roleId(channel.role)));
        entry.insert(QStringLiteral("lfe"), channel.lfe);
        channels.append(entry);
    }
    object.insert(QStringLiteral("channels"), channels);
    return object;
}

bool captureJackConnection(const char* connection, void* userData) noexcept
{
    auto* capture = static_cast<JackConnectionCapture*>(userData);
    if (capture == nullptr || capture->connections == nullptr
        || capture->localPortId == nullptr || connection == nullptr)
        return false;
    try {
        if (capture->localIsOutput)
            capture->connections->push_back({ capture->localPortId, connection });
        else
            capture->connections->push_back({ connection, capture->localPortId });
    } catch (...) {
        return false;
    }
    return true;
}

void captureJackConnections(const JackPort& port, std::vector<JackConnection>& connections) noexcept
{
    if (!port.isValid())
        return;
    const auto* localPortId = port.name();
    if (localPortId == nullptr || *localPortId == '\0')
        return;
    JackConnectionCapture capture { &connections, localPortId, port.isOutput() };
    port.visitConnections(&captureJackConnection, &capture);
}

std::string remapJackPortId(const std::string& portId,
                            std::string_view oldClientName,
                            std::string_view newClientName,
                            const ProbeSettings& oldSettings,
                            const ProbeSettings& newSettings)
{
    if (!oldClientName.empty()
        && portId.size() > oldClientName.size()
        && portId.compare(0, oldClientName.size(), oldClientName.data(), oldClientName.size()) == 0
        && portId[oldClientName.size()] == ':') {
        const auto oldPortName = std::string_view(portId).substr(oldClientName.size() + 1);
        for (std::size_t index = 0; index < oldSettings.channelCount; ++index) {
            if (oldPortName != oldSettings.channels[index].name)
                continue;
            if (index >= newSettings.channelCount)
                return {};
            return std::string(newClientName) + ":" + newSettings.channels[index].name;
        }
        return std::string(newClientName) + portId.substr(oldClientName.size());
    }
    return portId;
}

void restoreJackConnections(const std::vector<JackConnection>& connections,
                            std::string_view oldClientName,
                            std::string_view newClientName,
                            const ProbeSettings& oldSettings,
                            const ProbeSettings& newSettings,
                            JackClient& client) noexcept
{
    if (!client.isOpen() || oldClientName.empty() || newClientName.empty())
        return;
    for (const auto& connection : connections) {
        try {
            const auto sourcePortId = remapJackPortId(connection.sourcePortId,
                                                       oldClientName,
                                                       newClientName,
                                                       oldSettings,
                                                       newSettings);
            const auto destinationPortId = remapJackPortId(connection.destinationPortId,
                                                            oldClientName,
                                                            newClientName,
                                                            oldSettings,
                                                            newSettings);
            if (sourcePortId.empty() || destinationPortId.empty())
                continue;
            WinJACKNexus::Audio::JackGraph::connect(client, sourcePortId.c_str(), destinationPortId.c_str());
        } catch (...) {
        }
    }
}

class MeterProbeSettingsPanel final : public QWidget {
public:
    using SettingsProvider = std::function<ProbeSettings()>;
    using ApplyHandler = std::function<bool(const ProbeSettings&)>;

    MeterProbeSettingsPanel(ProbeKind kind,
                            SettingsProvider settingsProvider,
                            ApplyHandler applyHandler,
                            QWidget* parent)
        : QWidget(parent)
        , kind_(kind)
        , settingsProvider_(std::move(settingsProvider))
        , applyHandler_(std::move(applyHandler))
    {
        setObjectName(QStringLiteral("meterProbeSettingsPanel"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 4, 0, 4);
        layout->setSpacing(6);

        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        clientNameEditor_ = new QLineEdit(this);
        clientNameEditor_->setObjectName(QStringLiteral("meterProbeClientNameEditor"));
        clientNameEditor_->setMaxLength(127);
        clientNameEditor_->setPlaceholderText(QStringLiteral("例如 Meter-Program"));
        form->addRow(QStringLiteral("客户端名称"), clientNameEditor_);

        autoResetCheckBox_ = new QCheckBox(QStringLiteral("自动重置响度"), this);
        autoResetCheckBox_->setObjectName(QStringLiteral("meterProbeAutoResetCheckBox"));
        form->addRow(QString(), autoResetCheckBox_);

        if (kind_ == ProbeKind::custom) {
            channelCountSpinBox_ = new QSpinBox(this);
            channelCountSpinBox_->setObjectName(QStringLiteral("meterProbeChannelCountSpinBox"));
            channelCountSpinBox_->setRange(1, static_cast<int>(maximumChannelCount));
            form->addRow(QStringLiteral("输入声道"), channelCountSpinBox_);
        } else {
            fixedLayoutLabel_ = new QLabel(this);
            fixedLayoutLabel_->setObjectName(QStringLiteral("meterProbeFixedLayoutLabel"));
            fixedLayoutLabel_->setWordWrap(true);
            form->addRow(QStringLiteral("声道布局"), fixedLayoutLabel_);
        }
        layout->addLayout(form);

        if (kind_ == ProbeKind::custom) {
            auto* channelTitle = new QLabel(QStringLiteral("通道设置"), this);
            layout->addWidget(channelTitle);

            channelTable_ = new QTableWidget(this);
            channelTable_->setObjectName(QStringLiteral("meterProbeChannelTable"));
            channelTable_->setColumnCount(4);
            channelTable_->setHorizontalHeaderLabels({
                QStringLiteral("端口"),
                QStringLiteral("名称"),
                QStringLiteral("角色"),
                QStringLiteral("LFE")
            });
            channelTable_->verticalHeader()->setVisible(false);
            channelTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            channelTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            channelTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            channelTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            channelTable_->setSelectionMode(QAbstractItemView::NoSelection);
            channelTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            channelTable_->setMinimumHeight(164);
            channelTable_->setMaximumHeight(300);
            layout->addWidget(channelTable_);
        }

        auto* actionRow = new QHBoxLayout;
        applyButton_ = new QPushButton(QStringLiteral("应用"), this);
        applyButton_->setObjectName(QStringLiteral("meterProbeApplyButton"));
        statusLabel_ = new QLabel(this);
        statusLabel_->setObjectName(QStringLiteral("meterProbeSettingsStatus"));
        statusLabel_->setWordWrap(true);
        actionRow->addWidget(applyButton_);
        actionRow->addWidget(statusLabel_, 1);
        layout->addLayout(actionRow);

        connect(applyButton_, &QPushButton::clicked, this, [this] { apply(); });
        if (channelCountSpinBox_ != nullptr) {
            connect(channelCountSpinBox_, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this](int channelCount) { rebuildChannelTable(static_cast<std::size_t>(channelCount)); });
        }
        refresh();
    }

    void refresh()
    {
        if (!settingsProvider_)
            return;

        const auto settings = settingsProvider_();
        const QSignalBlocker clientNameBlocker(clientNameEditor_);
        const QSignalBlocker autoResetBlocker(autoResetCheckBox_);
        clientNameEditor_->setText(QString::fromUtf8(settings.clientName.c_str()));
        autoResetCheckBox_->setChecked(settings.autoReset);

        if (kind_ == ProbeKind::custom) {
            const QSignalBlocker channelCountBlocker(channelCountSpinBox_);
            channelCountSpinBox_->setValue(static_cast<int>(settings.channelCount));
            populateChannelTable(settings);
        } else if (fixedLayoutLabel_ != nullptr) {
            fixedLayoutLabel_->setText(layoutDescription(settings));
        }
        statusLabel_->clear();
    }

private:
    struct RoleOption final {
        const char* id;
        const char* label;
    };

    static constexpr std::array roleOptions {
        RoleOption { "unassigned", "未分配" },
        RoleOption { "mono", "单声道 (M)" },
        RoleOption { "frontLeft", "前左 (L)" },
        RoleOption { "frontRight", "前右 (R)" },
        RoleOption { "frontCenter", "前中 (C)" },
        RoleOption { "sideLeft", "侧左 (Ls)" },
        RoleOption { "sideRight", "侧右 (Rs)" },
        RoleOption { "rearLeft", "后左 (Lb)" },
        RoleOption { "rearRight", "后右 (Rb)" },
        RoleOption { "topFrontLeft", "顶部前左" },
        RoleOption { "topFrontRight", "顶部前右" },
        RoleOption { "topRearLeft", "顶部后左" },
        RoleOption { "topRearRight", "顶部后右" },
        RoleOption { "lfe", "低频效果 (LFE)" }
    };

    static QString layoutDescription(const ProbeSettings& settings)
    {
        QStringList names;
        for (std::size_t index = 0; index < settings.channelCount; ++index)
            names.append(QString::fromUtf8(settings.channels[index].name.c_str()));
        return QStringLiteral("固定：%1（%2 声道）")
            .arg(names.join(QStringLiteral(" / ")))
            .arg(static_cast<int>(settings.channelCount));
    }

    ProbeSettings editorSettings() const
    {
        auto settings = settingsProvider_();
        settings.clientName = clientNameEditor_->text().trimmed().toUtf8().toStdString();
        settings.autoReset = autoResetCheckBox_->isChecked();
        if (kind_ != ProbeKind::custom || channelTable_ == nullptr)
            return settings;

        const auto channelCount = static_cast<std::size_t>(channelTable_->rowCount());
        resizeCustomLayout(settings, channelCount);
        for (std::size_t index = 0; index < channelCount; ++index) {
            auto& channel = settings.channels[index];
            if (auto* editor = qobject_cast<QLineEdit*>(channelTable_->cellWidget(static_cast<int>(index), 1)); editor != nullptr)
                channel.name = editor->text().trimmed().toUtf8().toStdString();
            if (auto* editor = qobject_cast<QComboBox*>(channelTable_->cellWidget(static_cast<int>(index), 2)); editor != nullptr) {
                const auto role = editor->currentData().toString().toUtf8().toStdString();
                roleFromId(role, channel.role);
            }
            if (auto* editor = qobject_cast<QCheckBox*>(channelTable_->cellWidget(static_cast<int>(index), 3)); editor != nullptr)
                channel.lfe = editor->isChecked() || channel.role == MeterChannelRole::lfe;
        }
        return settings;
    }

    void populateChannelTable(const ProbeSettings& settings)
    {
        if (channelTable_ == nullptr)
            return;

        channelTable_->setRowCount(static_cast<int>(settings.channelCount));
        for (std::size_t index = 0; index < settings.channelCount; ++index) {
            const auto row = static_cast<int>(index);
            const auto& channel = settings.channels[index];

            auto* portItem = new QTableWidgetItem(QStringLiteral("in_%1").arg(index + 1));
            portItem->setFlags(portItem->flags() & ~Qt::ItemIsEditable);
            channelTable_->setItem(row, 0, portItem);

            auto* nameEditor = new QLineEdit(QString::fromUtf8(channel.name.c_str()), channelTable_);
            nameEditor->setObjectName(QStringLiteral("meterProbeChannelNameEditor"));
            nameEditor->setMaxLength(64);
            channelTable_->setCellWidget(row, 1, nameEditor);

            auto* roleEditor = new QComboBox(channelTable_);
            roleEditor->setObjectName(QStringLiteral("meterProbeChannelRoleEditor"));
            for (const auto& option : roleOptions)
                roleEditor->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.id));
            const auto roleIndex = roleEditor->findData(QString::fromLatin1(roleId(channel.role)));
            roleEditor->setCurrentIndex(roleIndex >= 0 ? roleIndex : 0);
            channelTable_->setCellWidget(row, 2, roleEditor);

            auto* lfeEditor = new QCheckBox(channelTable_);
            lfeEditor->setObjectName(QStringLiteral("meterProbeChannelLfeEditor"));
            lfeEditor->setChecked(channel.lfe);
            channelTable_->setCellWidget(row, 3, lfeEditor);
        }
    }

    void rebuildChannelTable(std::size_t channelCount)
    {
        auto settings = editorSettings();
        resizeCustomLayout(settings, channelCount);
        populateChannelTable(settings);
    }

    void apply()
    {
        if (!applyHandler_) {
            statusLabel_->setText(QStringLiteral("设置不可用"));
            return;
        }

        auto settings = editorSettings();
        if (kind_ == ProbeKind::custom && channelCountSpinBox_ != nullptr)
            resizeCustomLayout(settings, static_cast<std::size_t>(channelCountSpinBox_->value()));
        if (!applyHandler_(settings)) {
            statusLabel_->setText(QStringLiteral("未能应用设置"));
            return;
        }

        refresh();
        statusLabel_->setText(QStringLiteral("已应用"));
    }

    ProbeKind kind_;
    SettingsProvider settingsProvider_;
    ApplyHandler applyHandler_;
    QLineEdit* clientNameEditor_ = nullptr;
    QCheckBox* autoResetCheckBox_ = nullptr;
    QSpinBox* channelCountSpinBox_ = nullptr;
    QLabel* fixedLayoutLabel_ = nullptr;
    QTableWidget* channelTable_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

class MeterProbeNode final : public WinJACKNexus::Node::INode {
public:
    explicit MeterProbeNode(std::uint32_t index)
        : definition_(probeDefinitions[index])
        , settings_(makeDefaultSettings(definition_))
        , realtimeState_(std::make_shared<MeterProbeRealtimeState>())
    {
    }

    ~MeterProbeNode() override
    {
        delete settingsPanel_.data();
        shutdown();
    }

    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {
            definition_.name,
            "Meter",
            "0.1.0",
            "WinJACKNexus",
            definition_.description
        };
    }

    bool init() noexcept override
    {
        if (initialized_)
            return true;

        try {
            if (!client_.open({ settings_.clientName.c_str(), true, false, nullptr }))
                return false;

            const auto* actualClientName = client_.name();
            if (actualClientName == nullptr || !isValidClientName(actualClientName)) {
                shutdown();
                return false;
            }
            actualClientName_ = actualClientName;
            sampleRate_ = client_.sampleRate();

            const auto layout = layoutFor(settings_);
            if (!layout.hasMeasuredChannels()
                || sampleRate_ == 0
                || !engine_.prepare({ static_cast<double>(sampleRate_), true }, layout)
                || !silenceReset_.configure(settings_.silenceThresholdDbfs, settings_.silenceDurationSeconds)) {
                shutdown();
                return false;
            }

            for (std::size_t index = 0; index < settings_.channelCount; ++index) {
                if (!ports_[index].registerPort(client_, settings_.channels[index].name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput)) {
                    shutdown();
                    return false;
                }
            }

            if (client_.setProcessCallback(&MeterProbeNode::process, this) != 0) {
                shutdown();
                return false;
            }

            initialized_ = true;
            if (!client_.activate()) {
                shutdown();
                return false;
            }
            settings_.clientName = actualClientName_;
            registerRealtimeState();
            return true;
        } catch (...) {
            shutdown();
            return false;
        }
    }

    void shutdown() noexcept override
    {
        shutdownRealtimeState(true);
    }

    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }

    void destroyWindow(HWND) noexcept override
    {
    }

    QWidget* createSettingsWidget(QWidget* parent) noexcept
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        try {
            if (settingsPanel_ == nullptr) {
                settingsPanel_ = new MeterProbeSettingsPanel(
                    definition_.kind,
                    [this] { return settings_; },
                    [this](const ProbeSettings& settings) { return applySettings(settings); },
                    parent);
            } else if (settingsPanel_->parentWidget() != parent) {
                settingsPanel_->setParent(parent);
            }
            settingsPanel_->refresh();
            settingsPanel_->show();
            return settingsPanel_.data();
        } catch (...) {
            return nullptr;
        }
    }

    void destroySettingsWidget(QWidget* widget) noexcept
    {
        if (settingsPanel_ == widget) {
            delete settingsPanel_.data();
            settingsPanel_ = nullptr;
            return;
        }
        delete widget;
    }

    const char* settingsJson() noexcept
    {
        try {
            if (realtimeState_ != nullptr) {
                settings_.autoReset = realtimeState_->automaticResetEnabled();
                settings_.silenceThresholdDbfs = realtimeState_->silenceThresholdDbfs();
                settings_.silenceDurationSeconds = realtimeState_->silenceDurationSeconds();
            }
            serializedSettings_ = QJsonDocument(settingsObject(settings_, definition_.kind))
                                      .toJson(QJsonDocument::Compact)
                                      .toStdString();
            return serializedSettings_.c_str();
        } catch (...) {
            serializedSettings_.clear();
            return "";
        }
    }

    bool restoreSettingsJson(const char* json) noexcept
    {
        try {
            ProbeSettings candidate;
            if (!parseSettingsJson(json, definition_.kind, settings_, candidate))
                return false;
            return applySettings(candidate);
        } catch (...) {
            return false;
        }
    }

    const char* clientId() const noexcept
    {
        return initialized_ && !actualClientName_.empty()
            ? actualClientName_.c_str()
            : settings_.clientName.c_str();
    }

    const char* instanceId() const noexcept
    {
        return settings_.instanceId.c_str();
    }

private:
    void shutdownRealtimeState(bool unregister) noexcept
    {
        if (realtimeState_ != nullptr)
            realtimeState_->setOnline(false);
        if (unregister)
            WinJACKNexus::Common::unregisterMeterProbe(settings_.instanceId, realtimeState_);
        client_.deactivate();
        for (auto& port : ports_)
            port.unregisterPort();
        client_.close();
        engine_.reset();
        silenceReset_.reset();
        actualClientName_.clear();
        sampleRate_ = 0;
        initialized_ = false;
    }

    static int process(jack_nframes_t frames, void* userData) noexcept
    {
        auto* node = static_cast<MeterProbeNode*>(userData);
        if (node != nullptr && node->initialized_)
            node->processAudio(frames);
        return 0;
    }

    void processAudio(jack_nframes_t frames) noexcept
    {
        std::array<const float*, maximumChannelCount> inputBuffers {};
        for (std::size_t index = 0; index < settings_.channelCount; ++index)
            inputBuffers[index] = ports_[index].audioBuffer(frames);

        if (realtimeState_ != nullptr
            && realtimeState_->consumeLoudnessResetRequest(resetRequestGeneration_)) {
            engine_.reset();
            silenceReset_.reset();
            realtimeState_->notifyLoudnessReset();
        }
        if (realtimeState_ != nullptr) {
            const auto silenceThreshold = static_cast<double>(realtimeState_->silenceThresholdDbfs());
            const auto silenceDuration = static_cast<double>(realtimeState_->silenceDurationSeconds());
            if (silenceThreshold != appliedSilenceThresholdDbfs_
                || silenceDuration != appliedSilenceDurationSeconds_) {
                silenceReset_.configure(silenceThreshold, silenceDuration);
                appliedSilenceThresholdDbfs_ = silenceThreshold;
                appliedSilenceDurationSeconds_ = silenceDuration;
            }
        }
        engine_.process(inputBuffers.data(), settings_.channelCount, frames);
        auto snapshot = engine_.snapshot();
        const auto automaticResetEnabled = realtimeState_ != nullptr
            ? realtimeState_->automaticResetEnabled()
            : settings_.autoReset;
        if (automaticResetEnabled && sampleRate_ != 0) {
            const auto elapsedSeconds = static_cast<double>(frames) / static_cast<double>(sampleRate_);
            if (silenceReset_.update(snapshot.samplePeakDbfs, elapsedSeconds)) {
                engine_.reset();
                engine_.process(inputBuffers.data(), settings_.channelCount, frames);
                snapshot = engine_.snapshot();
                if (realtimeState_ != nullptr)
                    realtimeState_->notifyLoudnessReset();
            }
        }
        if (realtimeState_ != nullptr)
            realtimeState_->publish(snapshot);
    }

    void registerRealtimeState() noexcept
    {
        if (realtimeState_ == nullptr)
            return;

        try {
            MeterProbeDescriptor descriptor;
            descriptor.instanceId = settings_.instanceId;
            descriptor.clientName = actualClientName_;
            descriptor.presetId = settings_.presetId;
            descriptor.channelCount = settings_.channelCount;
            descriptor.sampleRate = static_cast<double>(sampleRate_);
            descriptor.channelLayoutEditable = definition_.kind == ProbeKind::custom;
            descriptor.followGlobalPreset = settings_.followGlobalPreset;
            descriptor.silenceThresholdDbfs = static_cast<float>(settings_.silenceThresholdDbfs);
            for (std::size_t index = 0; index < descriptor.channelCount; ++index) {
                descriptor.channels[index] = {
                    settings_.channels[index].name,
                    settings_.channels[index].role,
                    settings_.channels[index].lfe
                };
            }
            realtimeState_->setAutomaticResetEnabled(settings_.autoReset);
            realtimeState_->setSilenceResetConfiguration(static_cast<float>(settings_.silenceThresholdDbfs),
                                                         static_cast<float>(settings_.silenceDurationSeconds));
            if (WinJACKNexus::Common::registerMeterProbe(std::move(descriptor), realtimeState_))
                realtimeState_->setOnline(true);
        } catch (...) {
            realtimeState_->setOnline(false);
        }
    }

    bool applySettings(const ProbeSettings& candidate) noexcept
    {
        if (!isValidSettings(candidate, definition_.kind))
            return false;
        if (!initialized_) {
            settings_ = candidate;
            return true;
        }

        try {
            const auto previousSettings = settings_;
            const auto oldClientName = actualClientName_;
            std::vector<JackConnection> connections;
            for (const auto& port : ports_)
                captureJackConnections(port, connections);

            shutdownRealtimeState(false);
            settings_ = candidate;
            if (init()) {
                restoreJackConnections(connections, oldClientName, actualClientName_, previousSettings, settings_, client_);
                return true;
            }

            settings_ = previousSettings;
            if (init())
                restoreJackConnections(connections, oldClientName, actualClientName_, previousSettings, settings_, client_);
            return false;
        } catch (...) {
            return false;
        }
    }

    ProbeDefinition definition_;
    ProbeSettings settings_;
    JackClient client_;
    std::array<JackPort, maximumChannelCount> ports_;
    LoudnessEngine engine_;
    SilenceResetController silenceReset_;
    std::string actualClientName_;
    std::string serializedSettings_;
    QPointer<MeterProbeSettingsPanel> settingsPanel_;
    std::shared_ptr<MeterProbeRealtimeState> realtimeState_;
    jack_nframes_t sampleRate_ {};
    std::uint64_t resetRequestGeneration_ {};
    double appliedSilenceThresholdDbfs_ { -70.0 };
    double appliedSilenceDurationSeconds_ { 3.0 };
    bool initialized_ = false;
};

}

const char* wjn_get_node_api_version(void)
{
    return "2";
}

std::uint32_t wjn_get_node_count(void)
{
    return static_cast<std::uint32_t>(probeDefinitions.size());
}

const char* wjn_get_node_name(std::uint32_t index)
{
    return index < probeDefinitions.size() ? probeDefinitions[index].name : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
    return index < probeDefinitions.size() ? "Meter" : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    if (index >= probeDefinitions.size())
        return nullptr;
    try {
        return new MeterProbeNode(index);
    } catch (...) {
        return nullptr;
    }
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
    delete node;
}

void* wjn_create_settings_view(WinJACKNexus::Node::INode*, void*)
{
    return nullptr;
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*)
{
}

QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode* node, QWidget* parent)
{
    auto* probe = dynamic_cast<MeterProbeNode*>(node);
    return probe == nullptr ? nullptr : probe->createSettingsWidget(parent);
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode* node, QWidget* widget)
{
    if (auto* probe = dynamic_cast<MeterProbeNode*>(node); probe != nullptr)
        probe->destroySettingsWidget(widget);
    else
        delete widget;
}

const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node)
{
    auto* probe = dynamic_cast<MeterProbeNode*>(node);
    return probe == nullptr ? "" : probe->settingsJson();
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node, const char* json)
{
    auto* probe = dynamic_cast<MeterProbeNode*>(node);
    return probe != nullptr && probe->restoreSettingsJson(json) ? 1 : 0;
}

extern "C" WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode* node)
{
    auto* probe = dynamic_cast<MeterProbeNode*>(node);
    const auto module = GetModuleHandleW(L"mod_meterbrigde.dll");
    using ShowForInstance = int (*)(const char*);
    const auto showForInstance = module == nullptr ? nullptr
        : reinterpret_cast<ShowForInstance>(GetProcAddress(module, "wjn_meterbridge_show_for_instance"));
    return probe != nullptr && showForInstance != nullptr && showForInstance(probe->instanceId()) != 0 ? 1 : 0;
}

extern "C" WINJACK_NODE_API const char* wjn_get_node_client_id(WinJACKNexus::Node::INode* node)
{
    const auto* probe = dynamic_cast<const MeterProbeNode*>(node);
    return probe == nullptr ? "" : probe->clientId();
}