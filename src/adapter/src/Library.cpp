#include <WinJACKNexus/Common/StandardChannelNames.hpp>
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

#include <WinJACKNexus/Node/NodeApi.h>

#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackMidiBuffer.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Audio/MidiDevice.hpp>
#include <WinJACKNexus/Audio/MidiStream.hpp>
#include <WinJACKNexus/Audio/WasapiDevice.hpp>
#include <WinJACKNexus/Audio/WasapiStream.hpp>
#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/MeterWidget.hpp>

#include <QApplication>
#include <QCompleter>
#include <QEvent>
#include <QDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct AdapterNodeDefinition final {
    const char* name;
    const char* category;
    const char* description;
};

constexpr std::array kAdapterNodes {
    AdapterNodeDefinition { "WASAPI In", "Windows Native Driver", "Routes a shared-mode WASAPI capture device into JACK." },
    AdapterNodeDefinition { "WASAPI Out", "Windows Native Driver", "Routes JACK audio to a shared-mode WASAPI render device." },
    AdapterNodeDefinition { "System MIDI In", "Windows Native Driver", "Routes a system MIDI input device into JACK." },
    AdapterNodeDefinition { "System MIDI Out", "Windows Native Driver", "Routes JACK MIDI events to a system MIDI output device." }
};

constexpr std::uint16_t maxAudioChannels = 16;
constexpr double clockCorrectionGain = 0.0008;
constexpr double maximumClockCorrection = 0.002;
constexpr std::uint32_t concealmentFrameCount = 128;

struct LevelFrame final {
    std::array<float, maxAudioChannels> peak {};
    std::uint16_t channels = 0;
};

int meterRefreshIntervalMilliseconds(int framesPerSecond) noexcept
{
    return std::max(1, 1000 / framesPerSecond);
}

constexpr std::size_t midiChannelCount = 16;

struct MidiVelocityFrame final {
    std::array<std::uint8_t, midiChannelCount> velocity {};
    std::uint16_t changedChannels = 0;
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

void captureJackConnections(const WinJACKNexus::Audio::JackPort& port,
                            std::vector<JackConnection>& connections) noexcept
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
                            std::string_view newClientName)
{
    if (!oldClientName.empty()
        && portId.size() > oldClientName.size()
        && portId.compare(0, oldClientName.size(), oldClientName.data(), oldClientName.size()) == 0
        && portId[oldClientName.size()] == ':')
        return std::string(newClientName) + portId.substr(oldClientName.size());
    return portId;
}

void restoreJackConnections(const std::vector<JackConnection>& connections,
                            const std::string& oldClientName,
                            const char* newClientName,
                            WinJACKNexus::Audio::JackClient& client) noexcept
{
    if (!client.isOpen() || oldClientName.empty() || newClientName == nullptr || *newClientName == '\0')
        return;

    auto& api = WinJACKNexus::Audio::JackApi::instance();
    for (const auto& connection : connections) {
        try {
            const auto sourcePortId = remapJackPortId(connection.sourcePortId, oldClientName, newClientName);
            const auto destinationPortId = remapJackPortId(connection.destinationPortId, oldClientName, newClientName);
            if (api.portByName(client.nativeHandle(), sourcePortId.c_str()) == nullptr
                || api.portByName(client.nativeHandle(), destinationPortId.c_str()) == nullptr)
                continue;
            api.connect(client.nativeHandle(), sourcePortId.c_str(), destinationPortId.c_str());
        } catch (...) {
        }
    }
}

enum class DeviceDomain : std::uint8_t {
    wasapiCapture,
    wasapiRender,
    midiInput,
    midiOutput
};

class DeviceRegistry final {
public:
    bool isAvailable(DeviceDomain domain, const std::wstring& deviceId, const void* owner) const noexcept
    {
        std::lock_guard lock(mutex_);
        const auto iterator = owners_.find({ domain, deviceId });
        return iterator == owners_.end() || iterator->second == owner;
    }

    bool acquire(DeviceDomain domain, const std::wstring& deviceId, const void* owner) noexcept
    {
        if (deviceId.empty() || owner == nullptr)
            return false;
        {
            std::lock_guard lock(mutex_);
            const auto key = std::make_pair(domain, deviceId);
            const auto iterator = owners_.find(key);
            if (iterator != owners_.end() && iterator->second != owner)
                return false;
            try {
                owners_[key] = owner;
            } catch (...) {
                return false;
            }
        }
        notifySettingsViews();
        return true;
    }

    void release(DeviceDomain domain, const std::wstring& deviceId, const void* owner) noexcept
    {
        if (deviceId.empty() || owner == nullptr)
            return;
        bool changed = false;
        {
            std::lock_guard lock(mutex_);
            const auto iterator = owners_.find({ domain, deviceId });
            if (iterator != owners_.end() && iterator->second == owner) {
                owners_.erase(iterator);
                changed = true;
            }
        }
        if (changed)
            notifySettingsViews();
    }

    std::uint64_t registerSettingsView(std::function<void()> refresh) noexcept
    {
        if (!refresh)
            return 0;
        std::lock_guard lock(mutex_);
        try {
            const auto id = nextSettingsViewId_++;
            settingsViews_.emplace(id, std::move(refresh));
            return id;
        } catch (...) {
            return 0;
        }
    }

    void unregisterSettingsView(std::uint64_t id) noexcept
    {
        if (id == 0)
            return;
        std::lock_guard lock(mutex_);
        settingsViews_.erase(id);
    }

private:
    void notifySettingsViews() noexcept
    {
        {
            try {
                std::vector<std::function<void()>> refreshers;
                {
                    std::lock_guard lock(mutex_);
                    refreshers.reserve(settingsViews_.size());
                    for (const auto& [id, refresh] : settingsViews_)
                        refreshers.push_back(refresh);
                }
                for (const auto& refresh : refreshers) {
                    try {
                        refresh();
                    } catch (...) {
                    }
                }
            } catch (...) {
            }
        }
    }

    mutable std::mutex mutex_;
    std::map<std::pair<DeviceDomain, std::wstring>, const void*> owners_;
    std::map<std::uint64_t, std::function<void()>> settingsViews_;
    std::uint64_t nextSettingsViewId_ = 1;
};

DeviceRegistry deviceRegistry;

struct SettingsDeviceEntry final {
    QString id;
    QString name;
};

struct SettingsValues final {
    QString deviceId;
    QString clientName;
    int channels = 0;
};

class AdapterNodeSettings {
public:
    virtual ~AdapterNodeSettings() = default;
    virtual QWidget* createSettingsWidget(QWidget* parent) noexcept = 0;
    virtual void destroySettingsWidget(QWidget* widget) noexcept = 0;
    virtual HWND createSettingsView() noexcept = 0;
    virtual void destroySettingsView() noexcept = 0;

    const char* settingsJson() noexcept
    {
        try {
            serializedSettings_ = QJsonDocument(settings()).toJson(QJsonDocument::Compact).toStdString();
            return serializedSettings_.c_str();
        } catch (...) {
            serializedSettings_.clear();
            return "";
        }
    }

    int restoreSettingsJson(const char* json) noexcept
    {
        if (json == nullptr)
            return 0;
        try {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
            return error.error == QJsonParseError::NoError && document.isObject() && restoreSettings(document.object()) ? 1 : 0;
        } catch (...) {
            return 0;
        }
    }

protected:
    virtual QJsonObject settings() const = 0;
    virtual bool restoreSettings(const QJsonObject& values) noexcept = 0;

private:
    std::string serializedSettings_;
};

class SettingsPanel final : public QWidget {
public:
    using DeviceProvider = std::function<std::vector<SettingsDeviceEntry>()>;
    using ValuesProvider = std::function<SettingsValues()>;
    using ApplyHandler = std::function<bool(const SettingsValues&)>;

    SettingsPanel(QString title,
                  bool showChannels,
                  DeviceProvider deviceProvider,
                  ValuesProvider valuesProvider,
                  ApplyHandler applyHandler)
        : QWidget(nullptr)
        , deviceProvider_(std::move(deviceProvider))
        , valuesProvider_(std::move(valuesProvider))
        , applyHandler_(std::move(applyHandler))
    {
        namespace Controls = WinJACKNexus::UI::Controls;
        setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
        setAutoFillBackground(true);
        setWindowTitle(std::move(title));
        auto* form = new QFormLayout(this);
        form->setRowWrapPolicy(QFormLayout::WrapAllRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        const auto label = [this](const QString& text) {
            auto* result = new Controls::TextLabel(text, this);
            result->setWordWrap(true);
            // Form label columns use the width hint even when rows are wrapped.
            // Ignoring that hint collapses the label to zero width.
            result->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            return result;
        };

        clientNameEditor_ = new Controls::TextField(this);
        clientNameEditor_->setObjectName(QStringLiteral("clientNameEditor"));
        clientNameEditor_->setFixedHeight(Controls::sidebarControlHeight);
        clientNameEditor_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        form->addRow(label(QStringLiteral("Client name")), clientNameEditor_);

        auto* deviceRow = new QVBoxLayout;
        deviceCombo_ = new Controls::ChoiceField(this);
        deviceCombo_->setObjectName(QStringLiteral("deviceCombo"));
        deviceCombo_->setFixedHeight(Controls::sidebarControlHeight);
        deviceCombo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        deviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        deviceCombo_->setMinimumContentsLength(1);
        deviceCombo_->setEditable(true);
        deviceCombo_->setInsertPolicy(QComboBox::NoInsert);
        auto* deviceCompleter = new QCompleter(deviceCombo_->model(), deviceCombo_);
        deviceCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        deviceCompleter->setFilterMode(Qt::MatchContains);
        deviceCombo_->setCompleter(deviceCompleter);
        refreshButton_ = new Controls::ActionButton(QStringLiteral("Refresh"), this);
        refreshButton_->setObjectName(QStringLiteral("refreshButton"));
        refreshButton_->setFixedHeight(Controls::sidebarControlHeight);
        deviceRow->addWidget(deviceCombo_);
        deviceRow->addWidget(refreshButton_);
        form->addRow(label(QStringLiteral("Device")), deviceRow);

        if (showChannels) {
            channelsSpinBox_ = new Controls::IntegerField(this);
            channelsSpinBox_->setObjectName(QStringLiteral("channelsSpinBox"));
            channelsSpinBox_->setFixedHeight(Controls::sidebarControlHeight);
            channelsSpinBox_->setRange(1, static_cast<int>(maxAudioChannels));
            channelsSpinBox_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            form->addRow(label(QStringLiteral("JACK channels")), channelsSpinBox_);
            connect(channelsSpinBox_, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this](int) { apply(); });
        }

        auto* actionRow = new QVBoxLayout;
        applyButton_ = new Controls::ActionButton(QStringLiteral("Apply"), this);
        applyButton_->setObjectName(QStringLiteral("applyButton"));
        applyButton_->setFixedHeight(Controls::sidebarControlHeight);
        statusLabel_ = new WinJACKNexus::UI::Controls::TextLabel(this);
        statusLabel_->setObjectName(QStringLiteral("statusLabel"));
        statusLabel_->setProperty("wjnRole", QStringLiteral("success"));
        statusLabel_->setWordWrap(true);
        statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        actionRow->addWidget(applyButton_);
        actionRow->addWidget(statusLabel_);
        form->addRow(actionRow);

        connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshDevices(); });
        connect(applyButton_, &QPushButton::clicked, this, [this] { apply(); });
        refreshDevices();
        settingsViewId_ = deviceRegistry.registerSettingsView([this] { refreshDevices(); });
        const int preferredHeight = form->sizeHint().height();
        setMinimumHeight(preferredHeight);
        resize(460, preferredHeight);
    }

    ~SettingsPanel() override
    {
        deviceRegistry.unregisterSettingsView(settingsViewId_);
    }

    void refreshDevices()
    {
        const auto values = valuesProvider_();
        const QSignalBlocker blocker(deviceCombo_);
        deviceCombo_->clear();
        const auto entries = deviceProvider_();
        for (const auto& entry : entries) {
            deviceCombo_->addItem(entry.name, entry.id);
        }
        const auto currentIndex = deviceCombo_->findData(values.deviceId);
        if (currentIndex >= 0)
            deviceCombo_->setCurrentIndex(currentIndex);
        clientNameEditor_->setText(values.clientName);
        const QSignalBlocker channelsBlocker(channelsSpinBox_);
        if (channelsSpinBox_ != nullptr && values.channels > 0)
            channelsSpinBox_->setValue(std::clamp(values.channels, 1, static_cast<int>(maxAudioChannels)));
    }

private:
    void apply()
    {
        const auto currentIndex = deviceCombo_->currentIndex();
        const auto selectedIndex = currentIndex >= 0
                && deviceCombo_->itemText(currentIndex) == deviceCombo_->currentText()
            ? currentIndex
            : deviceCombo_->findText(deviceCombo_->currentText(), Qt::MatchFixedString);
        if (selectedIndex < 0) {
            statusLabel_->setText(QStringLiteral("Select a listed device"));
            return;
        }

        SettingsValues values;
        values.deviceId = deviceCombo_->itemData(selectedIndex).toString();
        values.clientName = clientNameEditor_->text();
        values.channels = channelsSpinBox_ != nullptr ? channelsSpinBox_->value() : 0;
        if (applyHandler_(values)) {
            statusLabel_->setText(QStringLiteral("Applied"));
            refreshDevices();
        } else {
            statusLabel_->setText(QStringLiteral("Unable to apply"));
            refreshDevices();
        }
    }

    DeviceProvider deviceProvider_;
    ValuesProvider valuesProvider_;
    ApplyHandler applyHandler_;
    QComboBox* deviceCombo_ = nullptr;
    QLineEdit* clientNameEditor_ = nullptr;
    QSpinBox* channelsSpinBox_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    std::uint64_t settingsViewId_ = 0;
};

struct WasapiDeviceListContext final {
    std::vector<WinJACKNexus::Audio::WasapiDeviceInfo> devices;
};

bool collectWasapiDevice(const WinJACKNexus::Audio::WasapiDeviceInfo& device, void* userData) noexcept
{
    auto* context = static_cast<WasapiDeviceListContext*>(userData);
    if (context == nullptr)
        return false;
    try {
        context->devices.push_back(device);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<SettingsDeviceEntry> wasapiSettingsDevices(
    WinJACKNexus::Audio::WasapiDeviceEnumerator& enumerator,
    WinJACKNexus::Audio::WasapiStreamDirection direction,
    DeviceDomain domain,
    const void* owner)
{
    WasapiDeviceListContext context;
    enumerator.enumerate(direction, &collectWasapiDevice, &context);
    std::vector<SettingsDeviceEntry> result;
    result.reserve(context.devices.size());
    for (const auto& device : context.devices) {
        const std::wstring id(device.id);
        if (!deviceRegistry.isAvailable(domain, id, owner))
            continue;
        result.push_back({ QString::fromWCharArray(device.id),
                           QString::fromWCharArray(device.name) });
    }
    return result;
}

bool chooseAvailableWasapiDevice(
    WinJACKNexus::Audio::WasapiDeviceEnumerator& enumerator,
    WinJACKNexus::Audio::WasapiStreamDirection direction,
    DeviceDomain domain,
    const void* owner,
    std::wstring& result) noexcept
{
    try {
        WinJACKNexus::Audio::WasapiDeviceInfo defaultDevice;
        if (enumerator.defaultDevice(direction, defaultDevice)) {
            const std::wstring defaultId(defaultDevice.id);
            if (deviceRegistry.isAvailable(domain, defaultId, owner)) {
                result = defaultId;
                return true;
            }
        }

        WasapiDeviceListContext context;
        enumerator.enumerate(direction, &collectWasapiDevice, &context);
        for (const auto& device : context.devices) {
            const std::wstring id(device.id);
            if (deviceRegistry.isAvailable(domain, id, owner)) {
                result = id;
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

struct MidiDeviceListContext final {
    std::vector<WinJACKNexus::Audio::MidiDeviceInfo> devices;
};

bool collectMidiDevice(const WinJACKNexus::Audio::MidiDeviceInfo& device, void* userData) noexcept
{
    auto* context = static_cast<MidiDeviceListContext*>(userData);
    if (context == nullptr)
        return false;
    try {
        context->devices.push_back(device);
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<SettingsDeviceEntry> midiSettingsDevices(
    WinJACKNexus::Audio::MidiDeviceEnumerator& enumerator,
    WinJACKNexus::Audio::MidiDirection direction,
    DeviceDomain domain,
    const void* owner)
{
    MidiDeviceListContext context;
    enumerator.enumerate(direction, &collectMidiDevice, &context);
    std::vector<SettingsDeviceEntry> result;
    result.reserve(context.devices.size());
    for (const auto& device : context.devices) {
        const auto id = std::to_wstring(device.deviceId);
        if (!deviceRegistry.isAvailable(domain, id, owner))
            continue;
        result.push_back({ QString::number(device.deviceId),
                           QString::fromWCharArray(device.name) });
    }
    return result;
}

bool chooseAvailableMidiDevice(
    WinJACKNexus::Audio::MidiDeviceEnumerator& enumerator,
    WinJACKNexus::Audio::MidiDirection direction,
    DeviceDomain domain,
    const void* owner,
    std::uint32_t& result) noexcept
{
    try {
        MidiDeviceListContext context;
        enumerator.enumerate(direction, &collectMidiDevice, &context);
        for (const auto& device : context.devices) {
            if (deviceRegistry.isAvailable(domain, std::to_wstring(device.deviceId), owner)) {
                result = device.deviceId;
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

std::uint16_t updateMidiVelocity(std::array<std::uint8_t, midiChannelCount>& velocity,
                                 const std::uint8_t* data,
                                 std::size_t size) noexcept
{
    if (data == nullptr || size < 3)
        return 0;
    const auto status = data[0];
    if ((status & 0xf0u) != 0x90u || data[2] == 0)
        return 0;
    const auto channel = static_cast<std::uint8_t>(status & 0x0fu);
    velocity[channel] = data[2];
    return static_cast<std::uint16_t>(1u << channel);
}

class AdapterMeterDialog final : public QDialog {
public:
    enum class Mode : std::uint8_t {
        audio,
        midi
    };
    using LevelReader = std::function<bool(LevelFrame&)>;
    using VelocityReader = std::function<bool(MidiVelocityFrame&)>;

    AdapterMeterDialog(QString title,
                       Mode mode,
                       LevelReader levelReader,
                       VelocityReader velocityReader)
        : QDialog(nullptr)
        , mode_(mode)
        , levelReader_(std::move(levelReader))
        , velocityReader_(std::move(velocityReader))
        , timer_(this)
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setWindowTitle(title);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        auto* heading = new WinJACKNexus::UI::Controls::TextLabel(title, this);
        heading->setProperty("wjnRole", QStringLiteral("meter-title"));
        layout->addWidget(heading);

        if (mode_ == Mode::audio) {
            meterArray_ = new WinJACKNexus::UI::MeterArrayWidget(this);
            meterArray_->setRange(-70.0f, 12.0f);
            meterArray_->setThresholds(-18.0f, 0.0f);
            meterArray_->setScaleMarks({ -70.0f, -60.0f, -40.0f, -24.0f, -18.0f, -12.0f,
                                         -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f });
            meterArray_->setShowScaleLabels(true);
            meterArray_->setChannelCount(1);
        } else {
            meterArray_ = new WinJACKNexus::UI::MidiVelocityWidget(this);
        }
        layout->addWidget(meterArray_, 1);

        auto* footer = new WinJACKNexus::UI::Controls::TextLabel(mode_ == Mode::audio
                                      ? QStringLiteral("Peak dBFS  |  -70 to +12 dB")
                                      : QStringLiteral("Recent Note-On velocity  |  0 to 127"),
                                  this);
        footer->setProperty("wjnRole", QStringLiteral("meter-footer"));
        layout->addWidget(footer);

        timer_.setTimerType(Qt::PreciseTimer);
        timer_.setInterval(meterRefreshIntervalMilliseconds(refreshRate_));
        connect(&timer_, &QTimer::timeout, this, [this] { pollQueue(); });
        auto& applicationSettings = WinJACKNexus::UI::ApplicationSettings::instance();
        connect(&applicationSettings,
                &WinJACKNexus::UI::ApplicationSettings::frameRateChanged,
                this,
                [this](int framesPerSecond) { applyRefreshRate(framesPerSecond); });
        applyRefreshRate(applicationSettings.frameRate());
        resize(mode_ == Mode::audio ? 720 : 720, 270);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QDialog::showEvent(event);
        synchronizeTimer();
    }

    void hideEvent(QHideEvent* event) override
    {
        timer_.stop();
        QDialog::hideEvent(event);
    }

    void changeEvent(QEvent* event) override
    {
        QDialog::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange)
            synchronizeTimer();
    }

private:
    static float peakToDecibels(float peak) noexcept
    {
        if (!std::isfinite(peak) || peak <= 0.00031622777f)
            return -70.0f;
        return std::clamp(20.0f * std::log10(peak), -70.0f, 12.0f);
    }

    void pollQueue()
    {
        if (mode_ == Mode::audio) {
            LevelFrame latest;
            bool received = false;
            while (levelReader_ && levelReader_(latest))
                received = true;
            if (!received)
                return;

            std::array<float, maxAudioChannels> decibels {};
            const auto channels = static_cast<std::uint16_t>(std::clamp<std::uint16_t>(latest.channels, 1, maxAudioChannels));
            for (std::uint16_t channel = 0; channel < channels; ++channel)
                decibels[channel] = peakToDecibels(latest.peak[channel]);
            meterArray_->setValues(decibels, channels);
            return;
        }

        MidiVelocityFrame frame;
        while (velocityReader_ && velocityReader_(frame)) {
            for (std::uint16_t channel = 0; channel < midiChannelCount; ++channel) {
                if ((frame.changedChannels & static_cast<std::uint16_t>(1u << channel)) != 0)
                    meterArray_->setValue(channel, frame.velocity[channel]);
            }
        }
    }

    void applyRefreshRate(int framesPerSecond)
    {
        if (refreshRate_ == framesPerSecond)
            return;
        refreshRate_ = framesPerSecond;
        timer_.setInterval(meterRefreshIntervalMilliseconds(refreshRate_));
    }

    void synchronizeTimer()
    {
        if (isVisible() && !isMinimized()) {
            if (!timer_.isActive())
                timer_.start();
        } else {
            timer_.stop();
        }
    }

    Mode mode_;
    LevelReader levelReader_;
    VelocityReader velocityReader_;
    WinJACKNexus::UI::MeterArrayWidget* meterArray_ = nullptr;
    QTimer timer_;
    int refreshRate_ = 60;
};

class WasapiInNode final : public WinJACKNexus::Node::INode, public AdapterNodeSettings {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { "WASAPI In", "Audio Adapter", "0.1.0", "WinJACKNexus",
                 "Routes a shared-mode WASAPI capture device into JACK." };
    }

    bool init() noexcept override
    {
        if (initialized_)
            return true;

        if (deviceId_.empty()) {
            if (!chooseAvailableWasapiDevice(deviceEnumerator_,
                                             WinJACKNexus::Audio::WasapiStreamDirection::capture,
                                             DeviceDomain::wasapiCapture,
                                             this,
                                             deviceId_))
                return false;
        }
        if (!deviceRegistry.acquire(DeviceDomain::wasapiCapture, deviceId_, this))
            return false;

        if (!jackClient_.open({ clientName_.c_str(), true, false, nullptr })) {
            deviceRegistry.release(DeviceDomain::wasapiCapture, deviceId_, this);
            return false;
        }

        const WinJACKNexus::Audio::WasapiStreamOptions options { deviceId_.c_str(), 0, requestedChannels_ };
        if (!stream_.open(WinJACKNexus::Audio::WasapiStreamDirection::capture, options)) {
            jackClient_.close();
            deviceRegistry.release(DeviceDomain::wasapiCapture, deviceId_, this);
            return false;
        }

        const auto format = stream_.format();
        sourceChannels_ = format.channels;
        channels_ = std::clamp<std::uint16_t>(requestedChannels_ != 0 ? requestedChannels_ : format.channels,
                                              1,
                                              maxAudioChannels);
        requestedChannels_ = channels_;
        jackSampleRate_.store(jackClient_.sampleRate(), std::memory_order_release);

        try {
            const auto ringCapacity = static_cast<std::size_t>(std::max(format.sampleRate, jackClient_.sampleRate()))
                * channels_;
            samples_ = std::make_unique<WinJACKNexus::Common::AudioRingBuffer<float>>(ringCapacity);
            inputScratch_.resize(static_cast<std::size_t>(stream_.bufferFrames()) * channels_);
            resampleScratch_.resize(static_cast<std::size_t>(stream_.bufferFrames()) * channels_ * 32);
        } catch (...) {
            stream_.close();
            jackClient_.close();
            deviceRegistry.release(DeviceDomain::wasapiCapture, deviceId_, this);
            return false;
        }

        for (std::uint16_t channel = 0; channel < channels_; ++channel) {
            const auto portName = WinJACKNexus::Common::standardChannelName(channel, channels_);
            if (!ports_[channel].registerPort(jackClient_, portName.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput)) {
                shutdown();
                return false;
            }
        }

        if (jackClient_.setProcessCallback(&WasapiInNode::process, this) != 0
            || jackClient_.setSampleRateCallback(&WasapiInNode::sampleRateChanged, this) != 0
            || !jackClient_.activate()
            || !stream_.start(&WasapiInNode::capture, this)) {
            shutdown();
            return false;
        }

        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        stream_.stop();
        jackClient_.deactivate();
        for (auto& port : ports_)
            port.unregisterPort();
        stream_.close();
        jackClient_.close();
        samples_.reset();
        channels_ = 0;
        sourceChannels_ = 0;
        resamplePosition_ = 0.0;
        initialized_ = false;
        deviceRegistry.release(DeviceDomain::wasapiCapture, deviceId_, this);
    }

    HWND createSettingsView() noexcept override
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        if (settingsPanel_ == nullptr) {
            try {
                settingsPanel_ = new SettingsPanel(
                    QStringLiteral("WASAPI In settings"),
                    true,
                    [this] {
                        return wasapiSettingsDevices(deviceEnumerator_,
                                                     WinJACKNexus::Audio::WasapiStreamDirection::capture,
                                                     DeviceDomain::wasapiCapture,
                                                     this);
                    },
                    [this] {
                        return SettingsValues { QString::fromStdWString(deviceId_),
                                                QString::fromUtf8(clientName_.c_str()),
                                                static_cast<int>(channels_ != 0 ? channels_ : requestedChannels_) };
                    },
                    [this](const SettingsValues& values) { return applySettings(values); });
                settingsPanel_->setAttribute(Qt::WA_DeleteOnClose, false);
                settingsPanel_->setAttribute(Qt::WA_NativeWindow, true);
                settingsPanel_->show();
            } catch (...) {
                settingsPanel_ = nullptr;
                return nullptr;
            }
        }
        return reinterpret_cast<HWND>(static_cast<quintptr>(settingsPanel_->winId()));
    }

    QWidget* createSettingsWidget(QWidget* parent) noexcept override
    {
        if (createSettingsView() == nullptr || settingsPanel_ == nullptr)
            return nullptr;
        settingsPanel_->setParent(parent);
        settingsPanel_->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
        settingsPanel_->show();
        return settingsPanel_;
    }

    void destroySettingsWidget(QWidget* widget) noexcept override
    {
        if (widget == settingsPanel_.data())
            destroySettingsView();
    }

    void destroySettingsView() noexcept override
    {
        delete settingsPanel_;
        settingsPanel_ = nullptr;
    }

protected:
    QJsonObject settings() const override
    {
        return {
            { QStringLiteral("deviceId"), QString::fromStdWString(deviceId_) },
            { QStringLiteral("clientName"), QString::fromStdString(clientName_) },
            { QStringLiteral("channels"), static_cast<int>(requestedChannels_) }
        };
    }

    bool restoreSettings(const QJsonObject& values) noexcept override
    {
        try {
            if (values.contains(QStringLiteral("deviceId")))
                deviceId_ = values.value(QStringLiteral("deviceId")).toString().toStdWString();
            const auto clientName = values.value(QStringLiteral("clientName")).toString().trimmed().toUtf8().toStdString();
            if (!clientName.empty())
                clientName_ = clientName;
            if (values.contains(QStringLiteral("channels"))) {
                const auto channels = values.value(QStringLiteral("channels")).toInt();
                requestedChannels_ = channels <= 0 ? 0 : static_cast<std::uint16_t>(std::clamp(channels, 1, static_cast<int>(maxAudioChannels)));
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    HWND createWindow(HWND) noexcept override
    {
        if (window_ == nullptr) {
                try {
                    window_ = new AdapterMeterDialog(
                        QString::fromUtf8(clientName_.c_str()),
                        AdapterMeterDialog::Mode::audio,
                        [this](LevelFrame& frame) { return levelFrames_.try_pop(frame); },
                        AdapterMeterDialog::VelocityReader {});
                } catch (...) {
                    window_ = nullptr;
                    return nullptr;
                }
        }
        window_->show();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        delete window_;
        window_ = nullptr;
    }

private:
    static int process(jack_nframes_t frames, void* userData) noexcept
    {
        auto* node = static_cast<WasapiInNode*>(userData);
        if (node == nullptr || node->samples_ == nullptr)
            return 0;

        std::array<float, maxAudioChannels> frame {};
        for (jack_nframes_t index = 0; index < frames; ++index) {
            const bool hasFrame = node->samples_->read(frame.data(), node->channels_) == node->channels_;
            for (std::uint16_t channel = 0; channel < node->channels_; ++channel) {
                auto* output = node->ports_[channel].audioBuffer(frames);
                if (output != nullptr)
                    output[index] = hasFrame ? frame[channel] : 0.0f;
            }
        }
        return 0;
    }

    static int sampleRateChanged(jack_nframes_t sampleRate, void* userData) noexcept
    {
        auto* node = static_cast<WasapiInNode*>(userData);
        if (node != nullptr)
            node->jackSampleRate_.store(sampleRate, std::memory_order_release);
        return 0;
    }

    static void capture(const float* interleavedSamples, std::uint32_t frames, void* userData) noexcept
    {
        auto* node = static_cast<WasapiInNode*>(userData);
        if (node != nullptr)
            node->writeCapture(interleavedSamples, frames);
    }

    void writeCapture(const float* interleavedSamples, std::uint32_t frames) noexcept
    {
        if (samples_ == nullptr || interleavedSamples == nullptr || frames == 0 || channels_ == 0)
            return;

        const float* source = interleavedSamples;
        if (sourceChannels_ != channels_) {
            const auto framesToCopy = std::min<std::size_t>(frames, inputScratch_.size() / channels_);
            for (std::size_t frame = 0; frame < framesToCopy; ++frame) {
                    for (std::uint16_t channel = 0; channel < channels_; ++channel)
                        inputScratch_[frame * channels_ + channel] = channel < sourceChannels_
                            ? interleavedSamples[frame * sourceChannels_ + channel]
                            : 0.0f;
            }
            frames = static_cast<std::uint32_t>(framesToCopy);
            source = inputScratch_.data();
        }

        const auto targetSampleRate = jackSampleRate_.load(std::memory_order_acquire);
        const auto sourceSampleRate = stream_.format().sampleRate;
        if (targetSampleRate == 0 || sourceSampleRate == 0)
            return;

        if (targetSampleRate == sourceSampleRate) {
            const auto samplesToWrite = static_cast<std::size_t>(frames) * channels_;
            samples_->write(source, samplesToWrite);
            publishLevels(source, frames);
            return;
        }

        const auto step = static_cast<double>(sourceSampleRate) / targetSampleRate;
        const auto maximumFrames = resampleScratch_.size() / channels_;
        std::size_t outputFrames = 0;
        auto position = resamplePosition_;
        while (position < frames && outputFrames < maximumFrames) {
            const auto left = static_cast<std::uint32_t>(position);
            const auto right = std::min(left + 1, frames - 1);
            const auto fraction = static_cast<float>(position - left);
            for (std::uint16_t channel = 0; channel < channels_; ++channel) {
                const auto first = source[static_cast<std::size_t>(left) * channels_ + channel];
                const auto second = source[static_cast<std::size_t>(right) * channels_ + channel];
                resampleScratch_[outputFrames * channels_ + channel] = first + (second - first) * fraction;
            }
            ++outputFrames;
            position += step;
        }
        resamplePosition_ = position - frames;
        samples_->write(resampleScratch_.data(), outputFrames * channels_);
        publishLevels(resampleScratch_.data(), static_cast<std::uint32_t>(outputFrames));
    }

    void publishLevels(const float* samples, std::uint32_t frames) noexcept
    {
        LevelFrame levels;
        levels.channels = channels_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::uint16_t channel = 0; channel < channels_; ++channel)
                levels.peak[channel] = std::max(levels.peak[channel], std::abs(samples[static_cast<std::size_t>(frame) * channels_ + channel]));
        }
        levelFrames_.try_push(levels);
    }

    bool applySettings(const SettingsValues& values) noexcept
    {
        const auto newDeviceId = values.deviceId.toStdWString();
        const auto newClientName = values.clientName.trimmed().toUtf8().toStdString();
        const auto newChannels = std::clamp(values.channels, 1, static_cast<int>(maxAudioChannels));
        if (newDeviceId.empty() || newClientName.empty()
            || !deviceRegistry.isAvailable(DeviceDomain::wasapiCapture, newDeviceId, this))
            return false;

        const auto oldDeviceId = deviceId_;
        const auto oldClientName = clientName_;
        const auto oldChannels = requestedChannels_;
        const auto wasInitialized = initialized_;
        std::vector<JackConnection> connections;
        std::string oldJackClientName;
        if (wasInitialized) {
            try {
                if (const auto* name = jackClient_.name(); name != nullptr)
                    oldJackClientName = name;
                for (const auto& port : ports_)
                    captureJackConnections(port, connections);
            } catch (...) {
                connections.clear();
                oldJackClientName.clear();
            }
            shutdown();
        }
        deviceId_ = newDeviceId;
        clientName_ = newClientName;
        requestedChannels_ = static_cast<std::uint16_t>(newChannels);
        if (!wasInitialized || init()) {
            if (wasInitialized) {
                const auto* name = jackClient_.name();
                restoreJackConnections(connections,
                                       oldJackClientName,
                                       name != nullptr ? name : clientName_.c_str(),
                                       jackClient_);
            }
            if (window_ != nullptr)
                window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
            return true;
        }

        deviceId_ = oldDeviceId;
        clientName_ = oldClientName;
        requestedChannels_ = oldChannels;
        if (window_ != nullptr)
            window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
        if (init()) {
            const auto* name = jackClient_.name();
            restoreJackConnections(connections,
                                   oldJackClientName,
                                   name != nullptr ? name : clientName_.c_str(),
                                   jackClient_);
        }
        return false;
    }

    WinJACKNexus::Audio::WasapiDeviceEnumerator deviceEnumerator_;
    WinJACKNexus::Audio::WasapiStream stream_;
    WinJACKNexus::Audio::JackClient jackClient_;
    std::array<WinJACKNexus::Audio::JackPort, maxAudioChannels> ports_;
    std::unique_ptr<WinJACKNexus::Common::AudioRingBuffer<float>> samples_;
    WinJACKNexus::Common::LockFreeSpscQueue<LevelFrame, 64> levelFrames_;
    std::vector<float> inputScratch_;
    std::vector<float> resampleScratch_;
    std::atomic<jack_nframes_t> jackSampleRate_ { 0 };
    std::uint16_t sourceChannels_ = 0;
    std::uint16_t channels_ = 0;
    std::uint16_t requestedChannels_ = 0;
    std::wstring deviceId_;
    std::string clientName_ = "WASAPI-In";
    double resamplePosition_ = 0.0;
    bool initialized_ = false;
    QPointer<QDialog> window_;
    QPointer<SettingsPanel> settingsPanel_;
};

class WasapiOutNode final : public WinJACKNexus::Node::INode, public AdapterNodeSettings {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { "WASAPI Out", "Audio Adapter", "0.1.0", "WinJACKNexus",
                 "Routes JACK audio to a shared-mode WASAPI render device." };
    }

    bool init() noexcept override
    {
        if (initialized_)
            return true;

        if (deviceId_.empty()) {
            if (!chooseAvailableWasapiDevice(deviceEnumerator_,
                                             WinJACKNexus::Audio::WasapiStreamDirection::render,
                                             DeviceDomain::wasapiRender,
                                             this,
                                             deviceId_))
                return false;
        }
        if (!deviceRegistry.acquire(DeviceDomain::wasapiRender, deviceId_, this))
            return false;

        if (!jackClient_.open({ clientName_.c_str(), true, false, nullptr })) {
            deviceRegistry.release(DeviceDomain::wasapiRender, deviceId_, this);
            return false;
        }

        const WinJACKNexus::Audio::WasapiStreamOptions options { deviceId_.c_str(), 0, requestedChannels_ };
        if (!stream_.open(WinJACKNexus::Audio::WasapiStreamDirection::render, options)) {
            jackClient_.close();
            deviceRegistry.release(DeviceDomain::wasapiRender, deviceId_, this);
            return false;
        }

        const auto format = stream_.format();
        sourceChannels_ = format.channels;
        channels_ = std::clamp<std::uint16_t>(requestedChannels_ != 0 ? requestedChannels_ : format.channels,
                                              1,
                                              maxAudioChannels);
        requestedChannels_ = channels_;
        jackSampleRate_.store(jackClient_.sampleRate(), std::memory_order_release);

        try {
            const auto ringCapacity = static_cast<std::size_t>(std::max(format.sampleRate, jackClient_.sampleRate()))
                * channels_;
            samples_ = std::make_unique<WinJACKNexus::Common::AudioRingBuffer<float>>(ringCapacity);
            jackScratch_.resize(static_cast<std::size_t>(std::max(jackClient_.bufferSize(), stream_.bufferFrames()))
                                * channels_);
            renderScratch_.resize(static_cast<std::size_t>(stream_.bufferFrames()) * channels_);
            resampleScratch_.resize(jackScratch_.size() * 32);
            const auto availableCapacity = samples_->capacity() / channels_;
            const auto periodFrames = std::max(static_cast<std::size_t>(stream_.bufferFrames()),
                                               static_cast<std::size_t>(jackClient_.bufferSize()));
            targetBufferFrames_ = std::min(availableCapacity,
                                           std::max<std::size_t>(1, periodFrames * 4));
            underrunFrames_.store(0, std::memory_order_release);
            underrunEvents_.store(0, std::memory_order_release);
            overrunFrames_.store(0, std::memory_order_release);
            overrunEvents_.store(0, std::memory_order_release);
            minimumBufferFrames_.store(availableCapacity, std::memory_order_release);
            maximumBufferFrames_.store(0, std::memory_order_release);
            lastRenderSamples_.fill(0.0f);
            concealmentStartSamples_.fill(0.0f);
            concealmentPosition_ = 0;
            recoveryFramesRemaining_ = 0;
            concealmentActive_ = false;
        } catch (...) {
            stream_.close();
            jackClient_.close();
            deviceRegistry.release(DeviceDomain::wasapiRender, deviceId_, this);
            return false;
        }

        for (std::uint16_t channel = 0; channel < channels_; ++channel) {
            const auto portName = WinJACKNexus::Common::standardChannelName(channel, channels_);
            if (!ports_[channel].registerPort(jackClient_, portName.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput)) {
                shutdown();
                return false;
            }
        }

        if (jackClient_.setProcessCallback(&WasapiOutNode::process, this) != 0
            || jackClient_.setSampleRateCallback(&WasapiOutNode::sampleRateChanged, this) != 0
            || !jackClient_.activate()) {
            shutdown();
            return false;
        }

        const auto channelCount = static_cast<std::size_t>(channels_);
        const auto prebufferFrames = targetBufferFrames_;
        const auto prebufferSamples = prebufferFrames * channelCount;
        for (int attempt = 0; attempt < 200 && samples_->available() < prebufferSamples; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        if (!stream_.start(&WasapiOutNode::render, this)) {
            shutdown();
            return false;
        }

        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        stream_.stop();
        jackClient_.deactivate();
        logBridgeStats();
        for (auto& port : ports_)
            port.unregisterPort();
        stream_.close();
        jackClient_.close();
        samples_.reset();
        channels_ = 0;
        sourceChannels_ = 0;
        resamplePosition_ = 0.0;
        resamplePrevious_.fill(0.0f);
        resampleHasPrevious_ = false;
        targetBufferFrames_ = 0;
        concealmentPosition_ = 0;
        recoveryFramesRemaining_ = 0;
        concealmentActive_ = false;
        initialized_ = false;
        deviceRegistry.release(DeviceDomain::wasapiRender, deviceId_, this);
    }

    HWND createSettingsView() noexcept override
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        if (settingsPanel_ == nullptr) {
            try {
                settingsPanel_ = new SettingsPanel(
                    QStringLiteral("WASAPI Out settings"),
                    true,
                    [this] {
                        return wasapiSettingsDevices(deviceEnumerator_,
                                                     WinJACKNexus::Audio::WasapiStreamDirection::render,
                                                     DeviceDomain::wasapiRender,
                                                     this);
                    },
                    [this] {
                        return SettingsValues { QString::fromStdWString(deviceId_),
                                                QString::fromUtf8(clientName_.c_str()),
                                                static_cast<int>(channels_ != 0 ? channels_ : requestedChannels_) };
                    },
                    [this](const SettingsValues& values) { return applySettings(values); });
                settingsPanel_->setAttribute(Qt::WA_DeleteOnClose, false);
                settingsPanel_->setAttribute(Qt::WA_NativeWindow, true);
                settingsPanel_->show();
            } catch (...) {
                settingsPanel_ = nullptr;
                return nullptr;
            }
        }
        return reinterpret_cast<HWND>(static_cast<quintptr>(settingsPanel_->winId()));
    }

    QWidget* createSettingsWidget(QWidget* parent) noexcept override
    {
        if (createSettingsView() == nullptr || settingsPanel_ == nullptr)
            return nullptr;
        settingsPanel_->setParent(parent);
        settingsPanel_->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
        settingsPanel_->show();
        return settingsPanel_;
    }

    void destroySettingsWidget(QWidget* widget) noexcept override
    {
        if (widget == settingsPanel_.data())
            destroySettingsView();
    }

    void destroySettingsView() noexcept override
    {
        delete settingsPanel_;
        settingsPanel_ = nullptr;
    }

protected:
    QJsonObject settings() const override
    {
        return {
            { QStringLiteral("deviceId"), QString::fromStdWString(deviceId_) },
            { QStringLiteral("clientName"), QString::fromStdString(clientName_) },
            { QStringLiteral("channels"), static_cast<int>(requestedChannels_) }
        };
    }

    bool restoreSettings(const QJsonObject& values) noexcept override
    {
        try {
            if (values.contains(QStringLiteral("deviceId")))
                deviceId_ = values.value(QStringLiteral("deviceId")).toString().toStdWString();
            const auto clientName = values.value(QStringLiteral("clientName")).toString().trimmed().toUtf8().toStdString();
            if (!clientName.empty())
                clientName_ = clientName;
            if (values.contains(QStringLiteral("channels"))) {
                const auto channels = values.value(QStringLiteral("channels")).toInt();
                requestedChannels_ = channels <= 0 ? 0 : static_cast<std::uint16_t>(std::clamp(channels, 1, static_cast<int>(maxAudioChannels)));
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    HWND createWindow(HWND) noexcept override
    {
        if (window_ == nullptr) {
                try {
                    window_ = new AdapterMeterDialog(
                        QString::fromUtf8(clientName_.c_str()),
                        AdapterMeterDialog::Mode::audio,
                        [this](LevelFrame& frame) { return levelFrames_.try_pop(frame); },
                        AdapterMeterDialog::VelocityReader {});
                } catch (...) {
                    window_ = nullptr;
                    return nullptr;
                }
        }
        window_->show();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        delete window_;
        window_ = nullptr;
    }

private:
    static int process(jack_nframes_t frames, void* userData) noexcept
    {
        auto* node = static_cast<WasapiOutNode*>(userData);
        if (node != nullptr)
            node->writePlayback(frames);
        return 0;
    }

    static int sampleRateChanged(jack_nframes_t sampleRate, void* userData) noexcept
    {
        auto* node = static_cast<WasapiOutNode*>(userData);
        if (node != nullptr)
            node->jackSampleRate_.store(sampleRate, std::memory_order_release);
        return 0;
    }

    static void render(float* interleavedSamples, std::uint32_t frames, void* userData) noexcept
    {
        auto* node = static_cast<WasapiOutNode*>(userData);
        if (node != nullptr)
            node->readRender(interleavedSamples, frames);
    }

    static void updateMinimum(std::atomic<std::size_t>& value, std::size_t candidate) noexcept
    {
        auto current = value.load(std::memory_order_relaxed);
        while (candidate < current
               && !value.compare_exchange_weak(current,
                                                candidate,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
        }
    }

    void recordBufferAvailability() noexcept
    {
        if (samples_ == nullptr || channels_ == 0)
            return;
        const auto availableFrames = samples_->available() / channels_;
        updateMinimum(minimumBufferFrames_, availableFrames);
        auto maximum = maximumBufferFrames_.load(std::memory_order_relaxed);
        while (availableFrames > maximum
               && !maximumBufferFrames_.compare_exchange_weak(maximum,
                                                               availableFrames,
                                                               std::memory_order_relaxed,
                                                               std::memory_order_relaxed)) {
        }
    }

    void logBridgeStats() noexcept
    {
        if (samples_ == nullptr || channels_ == 0)
            return;
        WJN_LOG_INFO("WASAPI Out bridge: underrun=%llu frames/%llu events, overrun=%llu frames/%llu events, occupancy=%llu..%llu/%llu frames, target=%llu frames",
                     static_cast<unsigned long long>(underrunFrames_.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(underrunEvents_.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(overrunFrames_.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(overrunEvents_.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(minimumBufferFrames_.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(maximumBufferFrames_.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(samples_->capacity() / channels_),
                     static_cast<unsigned long long>(targetBufferFrames_));
    }

    void writePlayback(jack_nframes_t frames) noexcept
    {
        if (samples_ == nullptr || channels_ == 0 || frames == 0 || frames > jackScratch_.size() / channels_)
            return;

        for (jack_nframes_t frame = 0; frame < frames; ++frame) {
            for (std::uint16_t channel = 0; channel < channels_; ++channel) {
                const auto* input = ports_[channel].audioBuffer(frames);
                jackScratch_[static_cast<std::size_t>(frame) * channels_ + channel] = input != nullptr ? input[frame] : 0.0f;
            }
        }

        const auto sourceSampleRate = jackSampleRate_.load(std::memory_order_acquire);
        const auto targetSampleRate = stream_.format().sampleRate;
        if (sourceSampleRate == 0 || targetSampleRate == 0)
            return;

        recordBufferAvailability();
        const auto availableFrames = samples_->available() / channels_;
        const auto bufferError = static_cast<double>(availableFrames)
            - static_cast<double>(targetBufferFrames_);
        const auto normalizedError = targetBufferFrames_ != 0
            ? bufferError / static_cast<double>(targetBufferFrames_)
            : 0.0;
        const auto correction = std::clamp(normalizedError * clockCorrectionGain,
                                           -maximumClockCorrection,
                                           maximumClockCorrection);
        const auto step = (static_cast<double>(sourceSampleRate) / targetSampleRate) * (1.0 + correction);
        const auto maximumFrames = resampleScratch_.size() / channels_;
        std::size_t outputFrames = 0;
        auto position = resamplePosition_;
        const auto inputFrames = static_cast<double>(frames);
        while (position < inputFrames - 1.0 && outputFrames < maximumFrames) {
            const auto left = static_cast<std::int64_t>(std::floor(position));
            const auto right = left + 1;
            const auto fraction = static_cast<float>(position - left);
            for (std::uint16_t channel = 0; channel < channels_; ++channel) {
                const auto first = left < 0 && resampleHasPrevious_
                    ? resamplePrevious_[channel]
                    : jackScratch_[static_cast<std::size_t>(std::max<std::int64_t>(left, 0)) * channels_ + channel];
                const auto second = jackScratch_[static_cast<std::size_t>(std::max<std::int64_t>(right, 0)) * channels_ + channel];
                resampleScratch_[outputFrames * channels_ + channel] = first + (second - first) * fraction;
            }
            ++outputFrames;
            position += step;
        }
        resamplePosition_ = position - frames;
        for (std::uint16_t channel = 0; channel < channels_; ++channel)
            resamplePrevious_[channel] = jackScratch_[static_cast<std::size_t>(frames - 1) * channels_ + channel];
        resampleHasPrevious_ = true;

        const auto requestedSamples = outputFrames * channels_;
        const auto writtenSamples = samples_->write(resampleScratch_.data(), requestedSamples);
        if (writtenSamples < requestedSamples) {
            overrunFrames_.fetch_add((requestedSamples - writtenSamples + channels_ - 1) / channels_,
                                     std::memory_order_relaxed);
            overrunEvents_.fetch_add(1, std::memory_order_relaxed);
        }
        recordBufferAvailability();
        publishLevels(resampleScratch_.data(), static_cast<std::uint32_t>(outputFrames));
    }

    void readRender(float* interleavedSamples, std::uint32_t frames) noexcept
    {
        if (interleavedSamples == nullptr || channels_ == 0 || sourceChannels_ == 0
            || frames > renderScratch_.size() / channels_)
            return;

        const auto requestedSampleCount = static_cast<std::size_t>(frames) * channels_;
        const auto samplesRead = samples_ != nullptr ? samples_->read(renderScratch_.data(), requestedSampleCount) : 0;
        recordBufferAvailability();
        const auto framesRead = samplesRead / channels_;
        const auto channelsToCopy = std::min(channels_, sourceChannels_);
        if (framesRead < frames) {
            underrunFrames_.fetch_add(frames - framesRead, std::memory_order_relaxed);
            underrunEvents_.fetch_add(1, std::memory_order_relaxed);
        }
        if (framesRead > 0 && concealmentActive_) {
            concealmentActive_ = false;
            recoveryFramesRemaining_ = concealmentFrameCount;
        }

        std::fill(interleavedSamples,
                  interleavedSamples + static_cast<std::size_t>(frames) * sourceChannels_,
                  0.0f);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto hasInput = frame < framesRead;
            if (!hasInput && !concealmentActive_) {
                concealmentActive_ = true;
                concealmentPosition_ = 0;
                recoveryFramesRemaining_ = 0;
                concealmentStartSamples_ = lastRenderSamples_;
            }

            for (std::uint16_t channel = 0; channel < channelsToCopy; ++channel) {
                auto value = hasInput
                    ? renderScratch_[static_cast<std::size_t>(frame) * channels_ + channel]
                    : lastRenderSamples_[channel];
                if (hasInput && recoveryFramesRemaining_ > 0) {
                    const auto blend = 1.0f - static_cast<float>(recoveryFramesRemaining_)
                        / static_cast<float>(concealmentFrameCount);
                    value = lastRenderSamples_[channel] * (1.0f - blend) + value * blend;
                } else if (!hasInput) {
                    const auto progress = std::min(concealmentPosition_ + 1, concealmentFrameCount);
                    const auto gain = 1.0f - static_cast<float>(progress)
                        / static_cast<float>(concealmentFrameCount);
                    value = concealmentStartSamples_[channel] * gain;
                }
                interleavedSamples[static_cast<std::size_t>(frame) * sourceChannels_ + channel] = value;
                lastRenderSamples_[channel] = value;
            }
            if (!hasInput && concealmentActive_)
                concealmentPosition_ = std::min(concealmentPosition_ + 1, concealmentFrameCount);
            if (hasInput && recoveryFramesRemaining_ > 0)
                --recoveryFramesRemaining_;
        }
    }

    void publishLevels(const float* samples, std::uint32_t frames) noexcept
    {
        LevelFrame levels;
        levels.channels = channels_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::uint16_t channel = 0; channel < channels_; ++channel)
                levels.peak[channel] = std::max(levels.peak[channel], std::abs(samples[static_cast<std::size_t>(frame) * channels_ + channel]));
        }
        levelFrames_.try_push(levels);
    }

    bool applySettings(const SettingsValues& values) noexcept
    {
        const auto newDeviceId = values.deviceId.toStdWString();
        const auto newClientName = values.clientName.trimmed().toUtf8().toStdString();
        const auto newChannels = std::clamp(values.channels, 1, static_cast<int>(maxAudioChannels));
        if (newDeviceId.empty() || newClientName.empty()
            || !deviceRegistry.isAvailable(DeviceDomain::wasapiRender, newDeviceId, this))
            return false;

        const auto oldDeviceId = deviceId_;
        const auto oldClientName = clientName_;
        const auto oldChannels = requestedChannels_;
        const auto wasInitialized = initialized_;
        std::vector<JackConnection> connections;
        std::string oldJackClientName;
        if (wasInitialized) {
            try {
                if (const auto* name = jackClient_.name(); name != nullptr)
                    oldJackClientName = name;
                for (const auto& port : ports_)
                    captureJackConnections(port, connections);
            } catch (...) {
                connections.clear();
                oldJackClientName.clear();
            }
            shutdown();
        }
        deviceId_ = newDeviceId;
        clientName_ = newClientName;
        requestedChannels_ = static_cast<std::uint16_t>(newChannels);
        if (!wasInitialized || init()) {
            if (wasInitialized) {
                const auto* name = jackClient_.name();
                restoreJackConnections(connections,
                                       oldJackClientName,
                                       name != nullptr ? name : clientName_.c_str(),
                                       jackClient_);
            }
            if (window_ != nullptr)
                window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
            return true;
        }

        deviceId_ = oldDeviceId;
        clientName_ = oldClientName;
        requestedChannels_ = oldChannels;
        if (window_ != nullptr)
            window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
        if (init()) {
            const auto* name = jackClient_.name();
            restoreJackConnections(connections,
                                   oldJackClientName,
                                   name != nullptr ? name : clientName_.c_str(),
                                   jackClient_);
        }
        return false;
    }

    WinJACKNexus::Audio::WasapiDeviceEnumerator deviceEnumerator_;
    WinJACKNexus::Audio::WasapiStream stream_;
    WinJACKNexus::Audio::JackClient jackClient_;
    std::array<WinJACKNexus::Audio::JackPort, maxAudioChannels> ports_;
    std::unique_ptr<WinJACKNexus::Common::AudioRingBuffer<float>> samples_;
    WinJACKNexus::Common::LockFreeSpscQueue<LevelFrame, 64> levelFrames_;
    std::vector<float> jackScratch_;
    std::vector<float> renderScratch_;
    std::vector<float> resampleScratch_;
    std::atomic<jack_nframes_t> jackSampleRate_ { 0 };
    std::uint16_t sourceChannels_ = 0;
    std::uint16_t channels_ = 0;
    std::uint16_t requestedChannels_ = 0;
    std::wstring deviceId_;
    std::string clientName_ = "WASAPI-Out";
    double resamplePosition_ = 0.0;
    std::array<float, maxAudioChannels> resamplePrevious_ {};
    std::array<float, maxAudioChannels> lastRenderSamples_ {};
    std::array<float, maxAudioChannels> concealmentStartSamples_ {};
    std::atomic<std::uint64_t> underrunFrames_ { 0 };
    std::atomic<std::uint64_t> underrunEvents_ { 0 };
    std::atomic<std::uint64_t> overrunFrames_ { 0 };
    std::atomic<std::uint64_t> overrunEvents_ { 0 };
    std::atomic<std::size_t> minimumBufferFrames_ { 0 };
    std::atomic<std::size_t> maximumBufferFrames_ { 0 };
    std::size_t targetBufferFrames_ = 0;
    std::uint32_t concealmentPosition_ = 0;
    std::uint32_t recoveryFramesRemaining_ = 0;
    bool concealmentActive_ = false;
    bool resampleHasPrevious_ = false;
    bool initialized_ = false;
    QPointer<QDialog> window_;
    QPointer<SettingsPanel> settingsPanel_;
};

class MidiInNode final : public WinJACKNexus::Node::INode, public AdapterNodeSettings {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { "System MIDI In", "Audio Adapter", "0.1.0", "WinJACKNexus",
                 "Routes a system MIDI input device into JACK." };
    }

    bool init() noexcept override
    {
        if (initialized_)
            return true;

        if (!hasDevice_) {
            hasDevice_ = chooseAvailableMidiDevice(deviceEnumerator_,
                                                   WinJACKNexus::Audio::MidiDirection::input,
                                                   DeviceDomain::midiInput,
                                                   this,
                                                   deviceId_);
        }
        if (hasDevice_ && !deviceRegistry.acquire(DeviceDomain::midiInput, std::to_wstring(deviceId_), this))
            return false;

        if (!jackClient_.open({ clientName_.c_str(), true, false, nullptr })) {
            releaseDevice();
            return false;
        }
        if (!midiPort_.registerPort(jackClient_, "midi_out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput)) {
            jackClient_.close();
            releaseDevice();
            return false;
        }
        if (jackClient_.setProcessCallback(&MidiInNode::process, this) != 0 || !jackClient_.activate()) {
            shutdown();
            return false;
        }

        if (hasDevice_) {
            const WinJACKNexus::Audio::MidiStreamOptions options { deviceId_, 4 };
            if (!midiStream_.open(WinJACKNexus::Audio::MidiDirection::input, options)
                || !midiStream_.start())
                midiStream_.close();
        }

        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        midiStream_.stop();
        midiStream_.close();
        jackClient_.deactivate();
        midiPort_.unregisterPort();
        jackClient_.close();
        initialized_ = false;
        releaseDevice();
    }

    HWND createSettingsView() noexcept override
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        if (settingsPanel_ == nullptr) {
            try {
                settingsPanel_ = new SettingsPanel(
                    QStringLiteral("System MIDI In settings"),
                    false,
                    [this] {
                        return midiSettingsDevices(deviceEnumerator_,
                                                   WinJACKNexus::Audio::MidiDirection::input,
                                                   DeviceDomain::midiInput,
                                                   this);
                    },
                    [this] {
                        return SettingsValues { hasDevice_ ? QString::number(deviceId_) : QString {},
                                                QString::fromUtf8(clientName_.c_str()),
                                                0 };
                    },
                    [this](const SettingsValues& values) { return applySettings(values); });
                settingsPanel_->setAttribute(Qt::WA_DeleteOnClose, false);
                settingsPanel_->setAttribute(Qt::WA_NativeWindow, true);
                settingsPanel_->show();
            } catch (...) {
                settingsPanel_ = nullptr;
                return nullptr;
            }
        }
        return reinterpret_cast<HWND>(static_cast<quintptr>(settingsPanel_->winId()));
    }

    QWidget* createSettingsWidget(QWidget* parent) noexcept override
    {
        if (createSettingsView() == nullptr || settingsPanel_ == nullptr)
            return nullptr;
        settingsPanel_->setParent(parent);
        settingsPanel_->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
        settingsPanel_->show();
        return settingsPanel_;
    }

    void destroySettingsWidget(QWidget* widget) noexcept override
    {
        if (widget == settingsPanel_.data())
            destroySettingsView();
    }

    void destroySettingsView() noexcept override
    {
        delete settingsPanel_;
        settingsPanel_ = nullptr;
    }

protected:
    QJsonObject settings() const override
    {
        return {
            { QStringLiteral("deviceId"), hasDevice_ ? static_cast<int>(deviceId_) : 0 },
            { QStringLiteral("hasDevice"), hasDevice_ },
            { QStringLiteral("clientName"), QString::fromStdString(clientName_) }
        };
    }

    bool restoreSettings(const QJsonObject& values) noexcept override
    {
        try {
            if (values.contains(QStringLiteral("hasDevice")))
                hasDevice_ = values.value(QStringLiteral("hasDevice")).toBool();
            if (values.contains(QStringLiteral("deviceId"))) {
                const auto deviceId = values.value(QStringLiteral("deviceId")).toInt();
                if (deviceId < 0)
                    return false;
                deviceId_ = static_cast<std::uint32_t>(deviceId);
            }
            const auto clientName = values.value(QStringLiteral("clientName")).toString().trimmed().toUtf8().toStdString();
            if (!clientName.empty())
                clientName_ = clientName;
            return true;
        } catch (...) {
            return false;
        }
    }

    HWND createWindow(HWND) noexcept override
    {
        if (window_ == nullptr) {
                try {
                    window_ = new AdapterMeterDialog(
                        QString::fromUtf8(clientName_.c_str()),
                        AdapterMeterDialog::Mode::midi,
                        AdapterMeterDialog::LevelReader {},
                        [this](MidiVelocityFrame& frame) { return velocityFrames_.try_pop(frame); });
                } catch (...) {
                    window_ = nullptr;
                    return nullptr;
                }
        }
        window_->show();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        delete window_;
        window_ = nullptr;
    }

private:
    static int process(jack_nframes_t frames, void* userData) noexcept
    {
        auto* node = static_cast<MidiInNode*>(userData);
        if (node == nullptr || !node->midiPort_.isValid())
            return 0;

        WinJACKNexus::Audio::JackMidiBuffer output(node->midiPort_.midiBuffer(frames));
        output.clear();
        WinJACKNexus::Audio::MidiEvent event;
        while (node->midiStream_.popEvent(event)) {
            if (event.size == 0)
                continue;
            output.write(0, event.data, event.size);
                const auto changedChannels = updateMidiVelocity(node->velocity_, event.data, event.size);
                if (changedChannels != 0)
                    node->publishVelocity(changedChannels);
        }
        return 0;
    }

        void publishVelocity(std::uint16_t changedChannels) noexcept
    {
        MidiVelocityFrame frame;
        frame.velocity = velocity_;
            frame.changedChannels = changedChannels;
        velocityFrames_.try_push(frame);
    }

    void releaseDevice() noexcept
    {
        if (hasDevice_)
            deviceRegistry.release(DeviceDomain::midiInput, std::to_wstring(deviceId_), this);
    }

    bool applySettings(const SettingsValues& values) noexcept
    {
        bool newHasDevice = false;
        std::uint32_t newDeviceId = 0;
        if (!values.deviceId.isEmpty()) {
            bool valid = false;
            newDeviceId = values.deviceId.toUInt(&valid);
            if (!valid)
                return false;
            newHasDevice = true;
        }
        const auto newClientName = values.clientName.trimmed().toUtf8().toStdString();
        if (newClientName.empty()
            || (newHasDevice
                && !deviceRegistry.isAvailable(DeviceDomain::midiInput, std::to_wstring(newDeviceId), this)))
            return false;

        const auto oldHasDevice = hasDevice_;
        const auto oldDeviceId = deviceId_;
        const auto oldClientName = clientName_;
        const auto wasInitialized = initialized_;
        std::vector<JackConnection> connections;
        std::string oldJackClientName;
        if (wasInitialized) {
            try {
                if (const auto* name = jackClient_.name(); name != nullptr)
                    oldJackClientName = name;
                captureJackConnections(midiPort_, connections);
            } catch (...) {
                connections.clear();
                oldJackClientName.clear();
            }
            shutdown();
        }
        hasDevice_ = newHasDevice;
        deviceId_ = newDeviceId;
        clientName_ = newClientName;
        if (!wasInitialized || init()) {
            if (wasInitialized) {
                const auto* name = jackClient_.name();
                restoreJackConnections(connections,
                                       oldJackClientName,
                                       name != nullptr ? name : clientName_.c_str(),
                                       jackClient_);
            }
            if (window_ != nullptr)
                window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
            return true;
        }

        hasDevice_ = oldHasDevice;
        deviceId_ = oldDeviceId;
        clientName_ = oldClientName;
        if (window_ != nullptr)
            window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
        if (init()) {
            const auto* name = jackClient_.name();
            restoreJackConnections(connections,
                                   oldJackClientName,
                                   name != nullptr ? name : clientName_.c_str(),
                                   jackClient_);
        }
        return false;
    }

    WinJACKNexus::Audio::MidiDeviceEnumerator deviceEnumerator_;
    WinJACKNexus::Audio::MidiStream midiStream_;
    WinJACKNexus::Audio::JackClient jackClient_;
    WinJACKNexus::Audio::JackPort midiPort_;
    WinJACKNexus::Common::LockFreeSpscQueue<MidiVelocityFrame, 64> velocityFrames_;
    std::array<std::uint8_t, midiChannelCount> velocity_ {};
    std::uint32_t deviceId_ = 0;
    std::string clientName_ = "MIDI-In";
    bool hasDevice_ = false;
    bool initialized_ = false;
    QPointer<QDialog> window_;
    QPointer<SettingsPanel> settingsPanel_;
};

class MidiOutNode final : public WinJACKNexus::Node::INode, public AdapterNodeSettings {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { "System MIDI Out", "Audio Adapter", "0.1.0", "WinJACKNexus",
                 "Routes JACK MIDI events to a system MIDI output device." };
    }

    bool init() noexcept override
    {
        if (initialized_)
            return true;

        if (!hasDevice_) {
            hasDevice_ = chooseAvailableMidiDevice(deviceEnumerator_,
                                                   WinJACKNexus::Audio::MidiDirection::output,
                                                   DeviceDomain::midiOutput,
                                                   this,
                                                   deviceId_);
        }
        if (hasDevice_ && !deviceRegistry.acquire(DeviceDomain::midiOutput, std::to_wstring(deviceId_), this))
            return false;

        if (!jackClient_.open({ clientName_.c_str(), true, false, nullptr })) {
            releaseDevice();
            return false;
        }
        if (!midiPort_.registerPort(jackClient_, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput)) {
            jackClient_.close();
            releaseDevice();
            return false;
        }
        if (jackClient_.setProcessCallback(&MidiOutNode::process, this) != 0 || !jackClient_.activate()) {
            shutdown();
            return false;
        }

        if (hasDevice_) {
            const WinJACKNexus::Audio::MidiStreamOptions options { deviceId_, 4 };
            if (!midiStream_.open(WinJACKNexus::Audio::MidiDirection::output, options))
                midiStream_.close();
        }

        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        midiStream_.stop();
        midiStream_.close();
        jackClient_.deactivate();
        midiPort_.unregisterPort();
        jackClient_.close();
        initialized_ = false;
        releaseDevice();
    }

    HWND createSettingsView() noexcept override
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        if (settingsPanel_ == nullptr) {
            try {
                settingsPanel_ = new SettingsPanel(
                    QStringLiteral("System MIDI Out settings"),
                    false,
                    [this] {
                        return midiSettingsDevices(deviceEnumerator_,
                                                   WinJACKNexus::Audio::MidiDirection::output,
                                                   DeviceDomain::midiOutput,
                                                   this);
                    },
                    [this] {
                        return SettingsValues { hasDevice_ ? QString::number(deviceId_) : QString {},
                                                QString::fromUtf8(clientName_.c_str()),
                                                0 };
                    },
                    [this](const SettingsValues& values) { return applySettings(values); });
                settingsPanel_->setAttribute(Qt::WA_DeleteOnClose, false);
                settingsPanel_->setAttribute(Qt::WA_NativeWindow, true);
                settingsPanel_->show();
            } catch (...) {
                settingsPanel_ = nullptr;
                return nullptr;
            }
        }
        return reinterpret_cast<HWND>(static_cast<quintptr>(settingsPanel_->winId()));
    }

    QWidget* createSettingsWidget(QWidget* parent) noexcept override
    {
        if (createSettingsView() == nullptr || settingsPanel_ == nullptr)
            return nullptr;
        settingsPanel_->setParent(parent);
        settingsPanel_->setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
        settingsPanel_->show();
        return settingsPanel_;
    }

    void destroySettingsWidget(QWidget* widget) noexcept override
    {
        if (widget == settingsPanel_.data())
            destroySettingsView();
    }

    void destroySettingsView() noexcept override
    {
        delete settingsPanel_;
        settingsPanel_ = nullptr;
    }

protected:
    QJsonObject settings() const override
    {
        return {
            { QStringLiteral("deviceId"), hasDevice_ ? static_cast<int>(deviceId_) : 0 },
            { QStringLiteral("hasDevice"), hasDevice_ },
            { QStringLiteral("clientName"), QString::fromStdString(clientName_) }
        };
    }

    bool restoreSettings(const QJsonObject& values) noexcept override
    {
        try {
            if (values.contains(QStringLiteral("hasDevice")))
                hasDevice_ = values.value(QStringLiteral("hasDevice")).toBool();
            if (values.contains(QStringLiteral("deviceId"))) {
                const auto deviceId = values.value(QStringLiteral("deviceId")).toInt();
                if (deviceId < 0)
                    return false;
                deviceId_ = static_cast<std::uint32_t>(deviceId);
            }
            const auto clientName = values.value(QStringLiteral("clientName")).toString().trimmed().toUtf8().toStdString();
            if (!clientName.empty())
                clientName_ = clientName;
            return true;
        } catch (...) {
            return false;
        }
    }

    HWND createWindow(HWND) noexcept override
    {
        if (window_ == nullptr) {
            try {
                window_ = new AdapterMeterDialog(
                    QString::fromUtf8(clientName_.c_str()),
                    AdapterMeterDialog::Mode::midi,
                    AdapterMeterDialog::LevelReader {},
                    [this](MidiVelocityFrame& frame) { return velocityFrames_.try_pop(frame); });
            } catch (...) {
                window_ = nullptr;
                return nullptr;
            }
        }
        window_->show();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        delete window_;
        window_ = nullptr;
    }

private:
    static int process(jack_nframes_t frames, void* userData) noexcept
    {
        auto* node = static_cast<MidiOutNode*>(userData);
        if (node == nullptr || !node->midiPort_.isValid())
            return 0;

        WinJACKNexus::Audio::JackMidiBuffer input(node->midiPort_.midiBuffer(frames));
        for (std::uint32_t index = 0; index < input.eventCount(); ++index) {
            WinJACKNexus::Audio::JackMidiEventView event;
            if (!input.event(index, event) || event.data == nullptr || event.size == 0)
                continue;

            const auto changedChannels = updateMidiVelocity(node->velocity_, event.data, event.size);
            if (changedChannels != 0)
                node->publishVelocity(changedChannels);
            if (!node->midiStream_.isOpen() || event.size > 3)
                continue;

            std::uint32_t message = 0;
            for (std::size_t byte = 0; byte < event.size; ++byte)
                message |= static_cast<std::uint32_t>(event.data[byte]) << (byte * 8);
            node->midiStream_.sendShortMessage(message);
        }
        return 0;
    }

    void publishVelocity(std::uint16_t changedChannels) noexcept
    {
        MidiVelocityFrame frame;
        frame.velocity = velocity_;
        frame.changedChannels = changedChannels;
        velocityFrames_.try_push(frame);
    }

    void releaseDevice() noexcept
    {
        if (hasDevice_)
            deviceRegistry.release(DeviceDomain::midiOutput, std::to_wstring(deviceId_), this);
    }

    bool applySettings(const SettingsValues& values) noexcept
    {
        bool newHasDevice = false;
        std::uint32_t newDeviceId = 0;
        if (!values.deviceId.isEmpty()) {
            bool valid = false;
            newDeviceId = values.deviceId.toUInt(&valid);
            if (!valid)
                return false;
            newHasDevice = true;
        }
        const auto newClientName = values.clientName.trimmed().toUtf8().toStdString();
        if (newClientName.empty()
            || (newHasDevice
                && !deviceRegistry.isAvailable(DeviceDomain::midiOutput, std::to_wstring(newDeviceId), this)))
            return false;

        const auto oldHasDevice = hasDevice_;
        const auto oldDeviceId = deviceId_;
        const auto oldClientName = clientName_;
        const auto wasInitialized = initialized_;
        std::vector<JackConnection> connections;
        std::string oldJackClientName;
        if (wasInitialized) {
            try {
                if (const auto* name = jackClient_.name(); name != nullptr)
                    oldJackClientName = name;
                captureJackConnections(midiPort_, connections);
            } catch (...) {
                connections.clear();
                oldJackClientName.clear();
            }
            shutdown();
        }
        hasDevice_ = newHasDevice;
        deviceId_ = newDeviceId;
        clientName_ = newClientName;
        if (!wasInitialized || init()) {
            if (wasInitialized) {
                const auto* name = jackClient_.name();
                restoreJackConnections(connections,
                                       oldJackClientName,
                                       name != nullptr ? name : clientName_.c_str(),
                                       jackClient_);
            }
            if (window_ != nullptr)
                window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
            return true;
        }

        hasDevice_ = oldHasDevice;
        deviceId_ = oldDeviceId;
        clientName_ = oldClientName;
        if (window_ != nullptr)
            window_->setWindowTitle(QString::fromUtf8(clientName_.c_str()));
        if (init()) {
            const auto* name = jackClient_.name();
            restoreJackConnections(connections,
                                   oldJackClientName,
                                   name != nullptr ? name : clientName_.c_str(),
                                   jackClient_);
        }
        return false;
    }

    WinJACKNexus::Audio::MidiDeviceEnumerator deviceEnumerator_;
    WinJACKNexus::Audio::MidiStream midiStream_;
    WinJACKNexus::Audio::JackClient jackClient_;
    WinJACKNexus::Audio::JackPort midiPort_;
    WinJACKNexus::Common::LockFreeSpscQueue<MidiVelocityFrame, 64> velocityFrames_;
    std::array<std::uint8_t, midiChannelCount> velocity_ {};
    std::uint32_t deviceId_ = 0;
    std::string clientName_ = "MIDI-Out";
    bool hasDevice_ = false;
    bool initialized_ = false;
    QPointer<QDialog> window_;
    QPointer<SettingsPanel> settingsPanel_;
};

const AdapterNodeDefinition* definitionFor(std::uint32_t index) noexcept
{
    return index < kAdapterNodes.size() ? &kAdapterNodes[index] : nullptr;
}

}

const char* wjn_get_node_api_version(void)
{
    return "2";
}

std::uint32_t wjn_get_node_theme_api_version(void)
{
    return WJN_NODE_THEME_API_VERSION;
}

const char* wjn_get_node_theme_namespace_json(std::uint32_t index)
{
    static const std::array<QByteArray, 4> descriptors = [] {
        const std::array<const char*, 4> ids {
            "module-winjacknexus-wasapi-in", "module-winjacknexus-wasapi-out",
            "module-winjacknexus-system-midi-in", "module-winjacknexus-system-midi-out"
        };
        std::array<QByteArray, 4> result;
        for (std::size_t type = 0; type < result.size(); ++type) {
            const QString moduleId = QString::fromLatin1(ids[type]);
            const QString nameSpace = QStringLiteral("--wjn-theme-") + moduleId + '-';
            const QJsonObject token {{"name", nameSpace + "node-card-background"},
                {"type", "color"},
                {"default", "var(--wjn-theme-patchbay-node-card-background)"},
                {"description", "Host-drawn card background for this adapter node type"}};
            const QJsonObject descriptor {{"descriptorVersion", 1}, {"themeApiVersion", 1},
                {"moduleId", moduleId}, {"namespace", nameSpace},
                {"displayName", QString::fromUtf8(kAdapterNodes[type].name)},
                {"tokens", QJsonArray {token}}};
            result[type] = QJsonDocument(descriptor).toJson(QJsonDocument::Compact);
        }
        return result;
    }();
    return index < descriptors.size() ? descriptors[index].constData() : nullptr;
}

std::uint32_t wjn_get_node_count(void)
{
    return static_cast<std::uint32_t>(kAdapterNodes.size());
}

const char* wjn_get_node_name(std::uint32_t index)
{
    const auto* definition = definitionFor(index);
    return definition == nullptr ? "" : definition->name;
}

const char* wjn_get_node_category(std::uint32_t index)
{
    const auto* definition = definitionFor(index);
    return definition == nullptr ? "" : definition->category;
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    const auto* definition = definitionFor(index);
    if (definition == nullptr)
        return nullptr;
    if (index == 0)
        return new WasapiInNode;
    if (index == 1)
        return new WasapiOutNode;
    if (index == 2)
        return new MidiInNode;
    return new MidiOutNode;
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
    delete node;
}

void* wjn_create_settings_view(WinJACKNexus::Node::INode* node, void*)
{
    auto* settings = dynamic_cast<AdapterNodeSettings*>(node);
    return settings == nullptr ? nullptr : reinterpret_cast<void*>(settings->createSettingsView());
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode* node, void*)
{
    if (auto* settings = dynamic_cast<AdapterNodeSettings*>(node); settings != nullptr)
        settings->destroySettingsView();
}

QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode* node, QWidget* parent)
{
    auto* settings = dynamic_cast<AdapterNodeSettings*>(node);
    return settings == nullptr ? nullptr : settings->createSettingsWidget(parent);
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode* node, QWidget* widget)
{
    if (auto* settings = dynamic_cast<AdapterNodeSettings*>(node); settings != nullptr)
        settings->destroySettingsWidget(widget);
}

const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node)
{
    auto* settings = dynamic_cast<AdapterNodeSettings*>(node);
    return settings == nullptr ? "" : settings->settingsJson();
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node, const char* json)
{
    auto* settings = dynamic_cast<AdapterNodeSettings*>(node);
    return settings == nullptr ? 0 : settings->restoreSettingsJson(json);
}

extern "C" WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode* node)
{
    return node != nullptr && node->createWindow(nullptr) != nullptr ? 1 : 0;
}
