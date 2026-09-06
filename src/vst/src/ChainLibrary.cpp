/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackMidiBuffer.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Direction = WinJACKNexus::Patchbay::Core::Direction;
using PortInfo = WinJACKNexus::Patchbay::Core::PortInfo;
using PortKind = WinJACKNexus::Patchbay::Core::PortKind;
using Snapshot = WinJACKNexus::Patchbay::Core::Snapshot;

constexpr auto kNodeName = "VST Chain";
constexpr auto kNodeCategory = "VST";
constexpr auto kChainClientPrefix = "WinJACKNexus-Chain.";
constexpr auto kBridgePrefix = "bridge_";

QString kindName(PortKind kind)
{
    return kind == PortKind::midi ? QStringLiteral("MIDI") : QStringLiteral("Audio");
}

QString directionName(Direction direction)
{
    return direction == Direction::input ? QStringLiteral("In") : QStringLiteral("Out");
}

PortKind kindFromName(const QString& name)
{
    return name.compare(QStringLiteral("midi"), Qt::CaseInsensitive) == 0 ? PortKind::midi : PortKind::audio;
}

Direction directionFromName(const QString& name)
{
    return name.compare(QStringLiteral("output"), Qt::CaseInsensitive) == 0 ? Direction::output : Direction::input;
}

QString portToken(PortKind kind, Direction direction)
{
    return kind == PortKind::midi
        ? direction == Direction::input ? QStringLiteral("midi-input") : QStringLiteral("midi-output")
        : direction == Direction::input ? QStringLiteral("audio-input") : QStringLiteral("audio-output");
}

std::string fullPortName(const WinJACKNexus::Audio::JackPort& port)
{
    const auto* name = port.name();
    return name == nullptr ? std::string {} : std::string(name);
}

bool isSuccessfulJackConnection(int result) noexcept
{
    return result == 0 || result == EEXIST;
}

class LoadedVstSlot final {
public:
    LoadedVstSlot() = default;

    ~LoadedVstSlot()
    {
        unload();
    }

    LoadedVstSlot(const LoadedVstSlot&) = delete;
    LoadedVstSlot& operator=(const LoadedVstSlot&) = delete;

    bool load(const QJsonObject& settings) noexcept
    {
        unload();
        const auto modulePath = QCoreApplication::applicationDirPath()
            + QStringLiteral("/nodes/vst/mod_vst_host.dll");
        library_ = LoadLibraryW(reinterpret_cast<LPCWSTR>(modulePath.utf16()));
        if (library_ == nullptr)
            return false;

        getNodeCount_ = loadFunction<GetNodeCount>("wjn_get_node_count");
        createNode_ = loadFunction<CreateNode>("wjn_create_node");
        destroyNode_ = loadFunction<DestroyNode>("wjn_destroy_node");
        setSettings_ = loadFunction<SetSettings>("wjn_set_node_settings_json");
        getSettings_ = loadFunction<GetSettings>("wjn_get_node_settings_json");
        getClientId_ = loadFunction<GetClientId>("wjn_get_node_client_id");
        showGui_ = loadFunction<ShowGui>("wjn_show_node_gui");
        if (getNodeCount_ == nullptr || createNode_ == nullptr || destroyNode_ == nullptr
            || setSettings_ == nullptr || getSettings_ == nullptr || getClientId_ == nullptr
            || getNodeCount_() == 0) {
            unload();
            return false;
        }

        node_ = createNode_(0);
        if (node_ == nullptr) {
            unload();
            return false;
        }
        const auto json = QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();
        if (setSettings_(node_, json.c_str()) == 0 || !node_->init()) {
            unload();
            return false;
        }
        fallbackSettings_ = settings;
        return true;
    }

    void unload() noexcept
    {
        if (node_ != nullptr) {
            node_->shutdown();
            if (destroyNode_ != nullptr)
                destroyNode_(node_);
            node_ = nullptr;
        }
        if (library_ != nullptr) {
            FreeLibrary(library_);
            library_ = nullptr;
        }
        getNodeCount_ = nullptr;
        createNode_ = nullptr;
        destroyNode_ = nullptr;
        setSettings_ = nullptr;
        getSettings_ = nullptr;
        getClientId_ = nullptr;
        showGui_ = nullptr;
    }

    bool isLoaded() const noexcept
    {
        return node_ != nullptr;
    }

    QJsonObject settings() const
    {
        if (node_ == nullptr || getSettings_ == nullptr)
            return fallbackSettings_;
        const auto* json = getSettings_(node_);
        if (json == nullptr)
            return fallbackSettings_;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
        return error.error == QJsonParseError::NoError && document.isObject()
            ? document.object() : fallbackSettings_;
    }

    QString title() const
    {
        const auto values = settings();
        const auto clientName = values.value(QStringLiteral("clientName")).toString().trimmed();
        if (!clientName.isEmpty())
            return clientName;
        return QFileInfo(values.value(QStringLiteral("path")).toString()).completeBaseName();
    }

    std::string expectedClientId() const
    {
        if (node_ == nullptr || getClientId_ == nullptr)
            return {};
        const auto* id = getClientId_(node_);
        return id == nullptr ? std::string {} : std::string(id);
    }

    bool showGui() noexcept
    {
        return node_ != nullptr && showGui_ != nullptr && showGui_(node_) != 0;
    }

private:
    using GetNodeCount = std::uint32_t (*)();
    using CreateNode = WinJACKNexus::Node::INode* (*)(std::uint32_t);
    using DestroyNode = void (*)(WinJACKNexus::Node::INode*);
    using SetSettings = int (*)(WinJACKNexus::Node::INode*, const char*);
    using GetSettings = const char* (*)(WinJACKNexus::Node::INode*);
    using GetClientId = const char* (*)(WinJACKNexus::Node::INode*);
    using ShowGui = int (*)(WinJACKNexus::Node::INode*);

    template <typename Function>
    Function loadFunction(const char* name) const noexcept
    {
        return reinterpret_cast<Function>(GetProcAddress(library_, name));
    }

    HMODULE library_ = nullptr;
    WinJACKNexus::Node::INode* node_ = nullptr;
    GetNodeCount getNodeCount_ = nullptr;
    CreateNode createNode_ = nullptr;
    DestroyNode destroyNode_ = nullptr;
    SetSettings setSettings_ = nullptr;
    GetSettings getSettings_ = nullptr;
    GetClientId getClientId_ = nullptr;
    ShowGui showGui_ = nullptr;
    QJsonObject fallbackSettings_;
};

struct ChainPort final {
    QString name;
    QString alias;
    PortKind kind = PortKind::audio;
    Direction direction = Direction::input;
    WinJACKNexus::Audio::JackPort port;
    WinJACKNexus::Audio::JackPort bridge;
};

struct SlotRecord final {
    QJsonObject settings;
    QPointF position;
    bool bypassed = false;
    std::unique_ptr<LoadedVstSlot> host;
    std::string clientId;
};

class ChainHostNode final : public WinJACKNexus::Node::INode {
public:
    ChainHostNode()
        : instanceId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , clientName_(QStringLiteral("WinJACKNexus-Chain.%1").arg(instanceId_.left(8)))
    {
    }

    ~ChainHostNode() override
    {
        shutdown();
    }

    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return { kNodeName, kNodeCategory, "0.1.0", "WinJACKNexus",
                 "Hosts a linear VST chain with a dedicated child patchbay." };
    }

    bool init() noexcept override
    {
        if (initialized_)
            return true;
        ensureDefaultPorts();
        const auto clientName = clientName_.toUtf8();
        if (!jackClient_.open({ clientName.constData(), true, false, nullptr })) {
            status_ = QStringLiteral("Unable to open the JACK client for this chain.");
            return false;
        }
        const auto* actualClientName = jackClient_.name();
        chainClientId_ = actualClientName == nullptr ? std::string {} : std::string(actualClientName);
        if (chainClientId_.empty()
            || jackClient_.setProcessCallback(&ChainHostNode::process, this) != 0
            || !registerPorts()
            || !loadSlots()
            || !jackClient_.activate()) {
            status_ = QStringLiteral("Unable to initialize the JACK chain client.");
            shutdownRuntime();
            return false;
        }
        initialized_ = true;
        status_ = QStringLiteral("The VST chain is running.");
        rebuildRouting();
        return true;
    }

    void shutdown() noexcept override
    {
        destroyWindow(nullptr);
        shutdownRuntime();
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        if (QApplication::instance() == nullptr || !initialized_)
            return nullptr;
        if (window_ == nullptr)
            createChainWindow();
        if (window_ == nullptr)
            return nullptr;
        window_->showNormal();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        if (window_ != nullptr) {
            persistCanvasPositions();
            delete window_.data();
            window_ = nullptr;
            canvas_ = nullptr;
            portList_ = nullptr;
            slotList_ = nullptr;
            bypassCheck_ = nullptr;
        }
    }

    const char* settingsJson() noexcept
    {
        QJsonArray ports;
        for (const auto& port : ports_)
            ports.append(QJsonObject {
                { QStringLiteral("name"), port.name },
                { QStringLiteral("alias"), port.alias },
                { QStringLiteral("kind"), port.kind == PortKind::midi ? QStringLiteral("midi") : QStringLiteral("audio") },
                { QStringLiteral("direction"), port.direction == Direction::output ? QStringLiteral("output") : QStringLiteral("input") }
            });

        QJsonArray slotArray;
        for (const auto& slot : slots_) {
            const auto slotSettings = slot.host != nullptr ? slot.host->settings() : slot.settings;
            slotArray.append(QJsonObject {
                { QStringLiteral("settings"), slotSettings },
                { QStringLiteral("x"), slot.position.x() },
                { QStringLiteral("y"), slot.position.y() },
                { QStringLiteral("bypassed"), slot.bypassed }
            });
        }
        serializedSettings_ = QJsonDocument(QJsonObject {
            { QStringLiteral("version"), 1 },
            { QStringLiteral("clientName"), displayName_ },
            { QStringLiteral("instanceId"), instanceId_ },
            { QStringLiteral("ports"), ports },
            { QStringLiteral("slots"), slotArray }
        }).toJson(QJsonDocument::Compact).toStdString();
        return serializedSettings_.c_str();
    }

    int restoreSettingsJson(const char* json) noexcept
    {
        if (json == nullptr)
            return 0;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return 0;

        std::vector<ChainPort> restoredPorts;
        for (const auto& value : document.object().value(QStringLiteral("ports")).toArray()) {
            const auto item = value.toObject();
            ChainPort port;
            port.name = item.value(QStringLiteral("name")).toString().trimmed();
            port.alias = item.value(QStringLiteral("alias")).toString().trimmed();
            port.kind = kindFromName(item.value(QStringLiteral("kind")).toString());
            port.direction = directionFromName(item.value(QStringLiteral("direction")).toString());
            if (port.name.isEmpty())
                return 0;
            restoredPorts.push_back(std::move(port));
        }

        std::vector<SlotRecord> restoredSlots;
        for (const auto& value : document.object().value(QStringLiteral("slots")).toArray()) {
            const auto item = value.toObject();
            SlotRecord slot;
            slot.settings = item.value(QStringLiteral("settings")).toObject();
            slot.position = { item.value(QStringLiteral("x")).toDouble(360.0 * restoredSlots.size()),
                              item.value(QStringLiteral("y")).toDouble(100.0) };
            slot.bypassed = item.value(QStringLiteral("bypassed")).toBool(false);
            restoredSlots.push_back(std::move(slot));
        }

        const auto clientName = document.object().value(QStringLiteral("clientName")).toString().trimmed();
        const auto instanceId = document.object().value(QStringLiteral("instanceId")).toString().trimmed();
        const auto wasInitialized = initialized_;
        if (wasInitialized) {
            shutdownRuntime();
            initialized_ = false;
        }
        ports_ = std::move(restoredPorts);
        slots_ = std::move(restoredSlots);
        if (!clientName.isEmpty())
            displayName_ = clientName;
        if (!instanceId.isEmpty()) {
            instanceId_ = instanceId;
            clientName_ = QStringLiteral("WinJACKNexus-Chain.%1").arg(instanceId_.left(8));
        }
        if (!wasInitialized)
            return 1;
        return init() ? 1 : 0;
    }

    QWidget* createSettingsWidget(QWidget* parent) noexcept
    {
        if (QApplication::instance() == nullptr)
            return nullptr;
        auto* widget = new QWidget(parent);
        auto* layout = new QVBoxLayout(widget);
        auto* title = new QLabel(QStringLiteral("VST Chain"), widget);
        auto* status = new QLabel(status_, widget);
        status->setWordWrap(true);
        auto* open = new QPushButton(QStringLiteral("Open child patchbay"), widget);
        layout->addWidget(title);
        layout->addWidget(status);
        layout->addWidget(open);
        layout->addStretch(1);
        QObject::connect(open, &QPushButton::clicked, widget, [this, status] {
            if (createWindow(nullptr) == nullptr)
                status->setText(status_);
        });
        return widget;
    }

    QString status() const
    {
        return status_;
    }

    std::string expectedClientId() const
    {
        return chainClientId_;
    }

    bool showChainGui() noexcept
    {
        return createWindow(nullptr) != nullptr;
    }

private:
    bool registerPorts()
    {
        for (auto& port : ports_) {
            const auto portName = port.name.toUtf8();
            const auto type = port.kind == PortKind::midi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE;
            const auto flags = port.direction == Direction::input ? JackPortIsInput : JackPortIsOutput;
            if (!port.port.registerPort(jackClient_, portName.constData(), type, flags))
                return false;
            const auto bridgeName = QStringLiteral("%1%2").arg(QString::fromLatin1(kBridgePrefix), port.name).toUtf8();
            const auto bridgeFlags = port.direction == Direction::input ? JackPortIsOutput : JackPortIsInput;
            if (!port.bridge.registerPort(jackClient_, bridgeName.constData(), type, bridgeFlags))
                return false;
        }
        return true;
    }

    void unregisterPorts() noexcept
    {
        for (auto& port : ports_) {
            port.bridge.unregisterPort();
            port.port.unregisterPort();
        }
    }

    bool loadSlots()
    {
        bool allLoaded = true;
        for (auto& slot : slots_) {
            if (slot.settings.isEmpty())
                continue;
            slot.host = std::make_unique<LoadedVstSlot>();
            if (!slot.host->load(slot.settings)) {
                slot.host.reset();
                allLoaded = false;
            }
        }
        if (!allLoaded)
            status_ = QStringLiteral("Some VST chain slots could not be loaded.");
        return true;
    }

    void shutdownRuntime() noexcept
    {
        routeThroughSlots_.store(false, std::memory_order_release);
        disconnectRouting();
        if (jackClient_.isActive())
            jackClient_.deactivate();
        for (auto& slot : slots_) {
            slot.clientId.clear();
            slot.host.reset();
        }
        unregisterPorts();
        jackClient_.close();
        chainClientId_.clear();
    }

    void ensureDefaultPorts()
    {
        if (!ports_.empty())
            return;
        addPortDefinition(PortKind::audio, Direction::input);
        addPortDefinition(PortKind::audio, Direction::output);
        addPortDefinition(PortKind::midi, Direction::input);
        addPortDefinition(PortKind::midi, Direction::output);
    }

    QString nextPortName(PortKind kind, Direction direction) const
    {
        const auto base = kind == PortKind::midi
            ? direction == Direction::input ? QStringLiteral("chain_midi_in") : QStringLiteral("chain_midi_out")
            : direction == Direction::input ? QStringLiteral("chain_in") : QStringLiteral("chain_out");
        for (int number = 1;; ++number) {
            const auto candidate = number == 1 ? base : base + QStringLiteral("_%1").arg(number);
            if (std::none_of(ports_.cbegin(), ports_.cend(), [&candidate](const ChainPort& port) {
                    return port.name == candidate;
                }))
                return candidate;
        }
    }

    void addPortDefinition(PortKind kind, Direction direction)
    {
        ChainPort port;
        port.name = nextPortName(kind, direction);
        port.kind = kind;
        port.direction = direction;
        ports_.push_back(std::move(port));
    }

    bool reconfigurePorts(const std::function<bool()>& operation)
    {
        const auto wasActive = jackClient_.isActive();
        if (wasActive) {
            routeThroughSlots_.store(false, std::memory_order_release);
            jackClient_.deactivate();
        }
        const auto success = operation();
        if (wasActive && !jackClient_.activate())
            return false;
        return success;
    }

    bool addPort(PortKind kind, Direction direction)
    {
        if (!initialized_)
        {
            addPortDefinition(kind, direction);
            return true;
        }
        const auto success = reconfigurePorts([this, kind, direction] {
            addPortDefinition(kind, direction);
            auto& port = ports_.back();
            const auto portName = port.name.toUtf8();
            const auto type = port.kind == PortKind::midi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE;
            const auto flags = port.direction == Direction::input ? JackPortIsInput : JackPortIsOutput;
            if (!port.port.registerPort(jackClient_, portName.constData(), type, flags)) {
                ports_.pop_back();
                return false;
            }
            const auto bridgeName = QStringLiteral("%1%2").arg(QString::fromLatin1(kBridgePrefix), port.name).toUtf8();
            const auto bridgeFlags = port.direction == Direction::input ? JackPortIsOutput : JackPortIsInput;
            if (port.bridge.registerPort(jackClient_, bridgeName.constData(), type, bridgeFlags))
                return true;
            port.port.unregisterPort();
            ports_.pop_back();
            return false;
        });
        if (!success) {
            status_ = QStringLiteral("Unable to add the requested chain port.");
            return false;
        }
        rebuildRouting();
        refreshControls();
        return true;
    }

    bool removePort(const std::string& portId)
    {
        const auto found = std::find_if(ports_.begin(), ports_.end(), [&portId](const ChainPort& port) {
            return fullPortName(port.port) == portId;
        });
        if (found == ports_.end())
            return false;
        const auto success = reconfigurePorts([this, found] {
            found->bridge.unregisterPort();
            found->port.unregisterPort();
            ports_.erase(found);
            return true;
        });
        if (!success)
            return false;
        rebuildRouting();
        refreshControls();
        return true;
    }

    bool renamePort(const std::string& portId)
    {
        const auto found = std::find_if(ports_.begin(), ports_.end(), [&portId](const ChainPort& port) {
            return fullPortName(port.port) == portId;
        });
        if (found == ports_.end() || QApplication::instance() == nullptr)
            return false;
        bool accepted = false;
        const auto alias = QInputDialog::getText(window_, QStringLiteral("Rename chain port"),
                                                 QStringLiteral("Display name"), QLineEdit::Normal,
                                                 found->alias.isEmpty() ? found->name : found->alias, &accepted);
        if (!accepted || alias.trimmed().isEmpty())
            return false;
        found->alias = alias.trimmed();
        refreshGraph();
        refreshControls();
        return true;
    }

    bool addSlot()
    {
        if (QApplication::instance() == nullptr)
            return false;
        const auto path = QFileDialog::getOpenFileName(window_, QStringLiteral("Choose VST plugin"), {},
                                                       QStringLiteral("VST plugins (*.vst3 *.dll)"));
        if (path.isEmpty())
            return false;
        SlotRecord slot;
        slot.position = { 360.0 * slots_.size(), 100.0 };
        slot.settings = QJsonObject {
            { QStringLiteral("path"), path },
            { QStringLiteral("clientName"), QFileInfo(path).completeBaseName() },
            { QStringLiteral("bits"), QStringLiteral("auto") },
            { QStringLiteral("portDisplayMode"), 0 }
        };
        const auto wasActive = jackClient_.isActive();
        if (wasActive) {
            routeThroughSlots_.store(false, std::memory_order_release);
            jackClient_.deactivate();
        }
        slot.host = std::make_unique<LoadedVstSlot>();
        if (!slot.host->load(slot.settings)) {
            if (wasActive)
                jackClient_.activate();
            status_ = QStringLiteral("Unable to load the selected VST slot.");
            return false;
        }
        slots_.push_back(std::move(slot));
        if (wasActive)
            jackClient_.activate();
        rebuildRouting();
        refreshControls();
        return true;
    }

    bool removeSlot(const std::string& clientId)
    {
        const auto found = std::find_if(slots_.begin(), slots_.end(), [&clientId](const SlotRecord& slot) {
            return slot.clientId == clientId;
        });
        if (found == slots_.end())
            return false;
        const auto wasActive = jackClient_.isActive();
        if (wasActive) {
            routeThroughSlots_.store(false, std::memory_order_release);
            jackClient_.deactivate();
        }
        disconnectRouting();
        found->host.reset();
        slots_.erase(found);
        if (wasActive)
            jackClient_.activate();
        rebuildRouting();
        refreshControls();
        return true;
    }

    bool setSlotBypass(const std::string& clientId, bool bypassed)
    {
        const auto found = std::find_if(slots_.begin(), slots_.end(), [&clientId](const SlotRecord& slot) {
            return slot.clientId == clientId;
        });
        if (found == slots_.end())
            return false;
        found->bypassed = bypassed;
        rebuildRouting();
        refreshControls();
        return true;
    }

    bool showSlotGui(const std::string& clientId)
    {
        const auto found = std::find_if(slots_.cbegin(), slots_.cend(), [&clientId](const SlotRecord& slot) {
            return slot.clientId == clientId;
        });
        return found != slots_.cend() && found->host != nullptr && found->host->showGui();
    }

    bool connectJack(const std::string& source, const std::string& destination)
    {
        if (!jackClient_.isOpen())
            return false;
        return isSuccessfulJackConnection(WinJACKNexus::Audio::JackApi::instance().connect(
            jackClient_.nativeHandle(), source.c_str(), destination.c_str()));
    }

    void disconnectJack(const std::string& source, const std::string& destination)
    {
        if (!jackClient_.isOpen())
            return;
        WinJACKNexus::Audio::JackApi::instance().disconnect(jackClient_.nativeHandle(), source.c_str(), destination.c_str());
        std::erase_if(routingConnections_, [&source, &destination](const auto& connection) {
            return connection.first == source && connection.second == destination;
        });
    }

    void disconnectRouting() noexcept
    {
        if (jackClient_.isOpen()) {
            auto& api = WinJACKNexus::Audio::JackApi::instance();
            for (const auto& [source, destination] : routingConnections_)
                api.disconnect(jackClient_.nativeHandle(), source.c_str(), destination.c_str());
        }
        routingConnections_.clear();
    }

    void updateSlotClientIds(const Snapshot& snapshot)
    {
        for (auto& slot : slots_) {
            const auto expected = slot.host == nullptr ? std::string {} : slot.host->expectedClientId();
            std::string actual;
            if (!expected.empty()) {
                const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [&expected](const auto& item) {
                    return item.id == expected || item.name == expected || item.id.starts_with(expected + "-");
                });
                if (client != snapshot.clients.cend())
                    actual = client->id;
            }
            slot.clientId = std::move(actual);
        }
    }

    bool hasActiveSlot() const noexcept
    {
        return std::any_of(slots_.cbegin(), slots_.cend(), [](const SlotRecord& slot) {
            return slot.host != nullptr && slot.host->isLoaded() && !slot.bypassed && !slot.clientId.empty();
        });
    }

    std::vector<std::string> slotPorts(const Snapshot& snapshot,
                                       const std::string& clientId,
                                       PortKind kind,
                                       Direction direction) const
    {
        std::vector<const PortInfo*> matching;
        for (const auto& port : snapshot.ports) {
            if (port.clientId == clientId && port.kind == kind && port.direction == direction
                && !port.name.starts_with(kBridgePrefix))
                matching.push_back(&port);
        }
        std::sort(matching.begin(), matching.end(), [](const PortInfo* left, const PortInfo* right) {
            return left->ordinal < right->ordinal;
        });
        std::vector<std::string> result;
        result.reserve(matching.size());
        for (const auto* port : matching)
            result.push_back(port->id);
        return result;
    }

    std::vector<std::string> chainBridgePorts(PortKind kind, Direction direction) const
    {
        std::vector<std::string> result;
        for (const auto& port : ports_) {
            if (port.kind != kind || port.direction != direction || !port.bridge.isValid())
                continue;
            result.push_back(fullPortName(port.bridge));
        }
        return result;
    }

    static void appendConnections(const std::vector<std::string>& sources,
                                  const std::vector<std::string>& destinations,
                                  std::vector<std::pair<std::string, std::string>>& result)
    {
        const auto count = std::min(sources.size(), destinations.size());
        for (std::size_t index = 0; index < count; ++index)
            result.emplace_back(sources[index], destinations[index]);
    }

    std::vector<std::pair<std::string, std::string>> desiredRouting(const Snapshot& snapshot) const
    {
        std::vector<std::pair<std::string, std::string>> result;
        const auto appendKind = [this, &snapshot, &result](PortKind kind) {
            if (!hasActiveSlot())
                return;
            auto sources = chainBridgePorts(kind, Direction::input);
            for (const auto& slot : slots_) {
                if (slot.host == nullptr || slot.bypassed || slot.clientId.empty())
                    continue;
                const auto destinations = slotPorts(snapshot, slot.clientId, kind, Direction::input);
                appendConnections(sources, destinations, result);
                sources = slotPorts(snapshot, slot.clientId, kind, Direction::output);
            }
            appendConnections(sources, chainBridgePorts(kind, Direction::output), result);
        };
        appendKind(PortKind::audio);
        appendKind(PortKind::midi);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    void applyRouting(const Snapshot& snapshot)
    {
        const auto desired = desiredRouting(snapshot);
        if (desired == routingConnections_)
            return;
        disconnectRouting();
        for (const auto& [source, destination] : desired) {
            if (connectJack(source, destination))
                routingConnections_.emplace_back(source, destination);
        }
    }

    Snapshot visibleSnapshot(const Snapshot& source) const
    {
        Snapshot result;
        std::set<std::string> allowedClients { chainClientId_ };
        for (const auto& slot : slots_)
            if (!slot.clientId.empty())
                allowedClients.insert(slot.clientId);

        for (const auto& client : source.clients) {
            if (!allowedClients.contains(client.id))
                continue;
            auto visibleClient = client;
            if (client.id == chainClientId_) {
                visibleClient.alias = displayName_.toUtf8().toStdString();
                visibleClient.x = 0.0;
                visibleClient.y = 80.0;
            } else {
                const auto slot = std::find_if(slots_.cbegin(), slots_.cend(), [&client](const SlotRecord& item) {
                    return item.clientId == client.id;
                });
                if (slot != slots_.cend()) {
                    visibleClient.alias = slot->host == nullptr ? std::string {} : slot->host->title().toUtf8().toStdString();
                    visibleClient.x = slot->position.x();
                    visibleClient.y = slot->position.y();
                }
            }
            result.clients.push_back(std::move(visibleClient));
        }
        if (std::none_of(result.clients.cbegin(), result.clients.cend(), [this](const auto& client) {
                return client.id == chainClientId_;
            })) {
            result.clients.push_back({ chainClientId_, clientName_.toUtf8().toStdString(), displayName_.toUtf8().toStdString(), {}, {}, 0.0, 80.0, false, true });
        }

        std::set<std::string> visiblePorts;
        for (const auto& port : source.ports) {
            if (!allowedClients.contains(port.clientId) || (port.clientId == chainClientId_ && port.name.starts_with(kBridgePrefix)))
                continue;
            auto visiblePort = port;
            if (port.clientId == chainClientId_) {
                const auto chainPort = std::find_if(ports_.cbegin(), ports_.cend(), [&port](const ChainPort& item) {
                    return fullPortName(item.port) == port.id;
                });
                if (chainPort != ports_.cend())
                    visiblePort.alias = chainPort->alias.toUtf8().toStdString();
            }
            visiblePorts.insert(visiblePort.id);
            result.ports.push_back(std::move(visiblePort));
        }
        for (const auto& connection : source.connections) {
            if (visiblePorts.contains(connection.sourcePortId) && visiblePorts.contains(connection.destinationPortId))
                result.connections.push_back(connection);
        }
        return result;
    }

    void refreshGraph()
    {
        if (!initialized_ || !jackClient_.isOpen())
            return;
        persistCanvasPositions();
        auto snapshot = Snapshot::capture(jackClient_);
        updateSlotClientIds(snapshot);
        if (canvas_ != nullptr)
            canvas_->setSnapshot(visibleSnapshot(snapshot));
    }

    void rebuildRouting()
    {
        if (!initialized_ || !jackClient_.isOpen())
            return;
        persistCanvasPositions();
        auto snapshot = Snapshot::capture(jackClient_);
        updateSlotClientIds(snapshot);
        applyRouting(snapshot);
        routeThroughSlots_.store(hasActiveSlot(), std::memory_order_release);
        if (canvas_ != nullptr)
            canvas_->setSnapshot(visibleSnapshot(snapshot));
    }

    static void copyAudio(const float* source, float* destination, jack_nframes_t frames) noexcept
    {
        if (destination == nullptr)
            return;
        if (source == nullptr) {
            std::fill_n(destination, frames, 0.0F);
            return;
        }
        std::memcpy(destination, source, static_cast<std::size_t>(frames) * sizeof(float));
    }

    static void copyMidi(void* source, void* destination, jack_nframes_t frames) noexcept
    {
        if (destination == nullptr)
            return;
        WinJACKNexus::Audio::JackMidiBuffer output(destination);
        output.clear();
        if (source == nullptr)
            return;
        WinJACKNexus::Audio::JackMidiBuffer input(source);
        WinJACKNexus::Audio::JackMidiEventView event;
        for (std::uint32_t index = 0; index < input.eventCount(); ++index) {
            if (input.event(index, event))
                output.write(std::min(event.time, frames), event.data, event.size);
        }
    }

    static int process(jack_nframes_t frames, void* userData) noexcept
    {
        auto* node = static_cast<ChainHostNode*>(userData);
        if (node == nullptr)
            return 0;
        if (!node->routeThroughSlots_.load(std::memory_order_acquire)) {
            std::size_t audioInputOrdinal = 0;
            for (auto& output : node->ports_) {
                if (output.kind != PortKind::audio || output.direction != Direction::output)
                    continue;
                const float* source = nullptr;
                std::size_t currentOrdinal = 0;
                for (auto& input : node->ports_) {
                    if (input.kind != PortKind::audio || input.direction != Direction::input)
                        continue;
                    if (currentOrdinal++ == audioInputOrdinal) {
                        source = input.port.audioBuffer(frames);
                        break;
                    }
                }
                copyAudio(source, output.port.audioBuffer(frames), frames);
                ++audioInputOrdinal;
            }

            std::size_t midiInputOrdinal = 0;
            for (auto& output : node->ports_) {
                if (output.kind != PortKind::midi || output.direction != Direction::output)
                    continue;
                void* source = nullptr;
                std::size_t currentOrdinal = 0;
                for (auto& input : node->ports_) {
                    if (input.kind != PortKind::midi || input.direction != Direction::input)
                        continue;
                    if (currentOrdinal++ == midiInputOrdinal) {
                        source = input.port.midiBuffer(frames);
                        break;
                    }
                }
                copyMidi(source, output.port.midiBuffer(frames), frames);
                ++midiInputOrdinal;
            }
            return 0;
        }

        for (auto& port : node->ports_) {
            if (port.kind == PortKind::audio) {
                if (port.direction == Direction::input)
                    copyAudio(port.port.audioBuffer(frames), port.bridge.audioBuffer(frames), frames);
                else
                    copyAudio(port.bridge.audioBuffer(frames), port.port.audioBuffer(frames), frames);
            } else if (port.direction == Direction::input) {
                copyMidi(port.port.midiBuffer(frames), port.bridge.midiBuffer(frames), frames);
            } else {
                copyMidi(port.bridge.midiBuffer(frames), port.port.midiBuffer(frames), frames);
            }
        }
        return 0;
    }

    void persistCanvasPositions()
    {
        if (canvas_ == nullptr)
            return;
        const auto& snapshot = canvas_->snapshot();
        for (auto& slot : slots_) {
            if (slot.clientId.empty())
                continue;
            const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [&slot](const auto& item) {
                return item.id == slot.clientId;
            });
            if (client != snapshot.clients.cend())
                slot.position = { client->x, client->y };
        }
    }

    void refreshControls()
    {
        if (portList_ != nullptr) {
            const auto current = portList_->currentRow();
            portList_->clear();
            for (const auto& port : ports_) {
                auto* item = new QListWidgetItem(
                    QStringLiteral("%1 %2 - %3").arg(directionName(port.direction), kindName(port.kind),
                                                       port.alias.isEmpty() ? port.name : port.alias), portList_);
                item->setData(Qt::UserRole, QString::fromStdString(fullPortName(port.port)));
            }
            if (current >= 0 && current < portList_->count())
                portList_->setCurrentRow(current);
        }
        if (slotList_ != nullptr) {
            const auto current = slotList_->currentRow();
            slotList_->clear();
            for (const auto& slot : slots_) {
                const auto title = slot.host == nullptr ? QStringLiteral("Unavailable VST slot") : slot.host->title();
                auto* item = new QListWidgetItem(slot.bypassed ? title + QStringLiteral(" (Bypassed)") : title, slotList_);
                item->setData(Qt::UserRole, QString::fromStdString(slot.clientId));
            }
            if (current >= 0 && current < slotList_->count())
                slotList_->setCurrentRow(current);
        }
        refreshNodeList();
    }

    void refreshNodeList()
    {
        if (nodeList_ == nullptr)
            return;
        nodeList_->clear();
        auto* chainPorts = new QTreeWidgetItem(nodeList_, { QStringLiteral("Chain ports") });
        chainPorts->setExpanded(true);
        for (const auto& [kind, direction, label] : {
                 std::tuple { PortKind::audio, Direction::input, QStringLiteral("Chain In") },
                 std::tuple { PortKind::audio, Direction::output, QStringLiteral("Chain Out") },
                 std::tuple { PortKind::midi, Direction::input, QStringLiteral("Chain MIDI In") },
                 std::tuple { PortKind::midi, Direction::output, QStringLiteral("Chain MIDI Out") } }) {
            auto* item = new QTreeWidgetItem(chainPorts, { label });
            item->setData(0, Qt::UserRole, portToken(kind, direction));
        }
        auto* vstSlots = new QTreeWidgetItem(nodeList_, { QStringLiteral("VST plugins") });
        vstSlots->setExpanded(true);
        for (const auto& slot : slots_) {
            auto* item = new QTreeWidgetItem(vstSlots, { slot.host == nullptr ? QStringLiteral("Unavailable VST slot") : slot.host->title() });
            item->setData(0, Qt::UserRole, QStringLiteral("vst-slot"));
            item->setData(0, Qt::UserRole + 1, QString::fromStdString(slot.clientId));
        }
        auto* add = new QTreeWidgetItem(vstSlots, { QStringLiteral("Add VST plugin slot...") });
        add->setData(0, Qt::UserRole, QStringLiteral("add-vst-slot"));
    }

    void createChainWindow()
    {
        window_ = new QMainWindow;
        window_->setAttribute(Qt::WA_DeleteOnClose, false);
        window_->setWindowTitle(QStringLiteral("WinJACKNexus - VST Chain"));
        window_->resize(1280, 760);
        PatchbayCanvas::applyTheme(false);
        canvas_ = new PatchbayCanvas(window_);
        canvas_->setPortVisibilityFilter([](const PortInfo& port) {
            return !port.name.starts_with(kBridgePrefix);
        });
        canvas_->setConnectionConnectHandler([this](const std::string& source, const std::string& destination) {
            return connectJack(source, destination);
        });
        canvas_->setConnectionDisconnectHandler([this](const std::string& source, const std::string& destination) {
            disconnectJack(source, destination);
        });
        canvas_->setModuleNodeHandlers(
            [this](const std::string& clientId) {
                return std::any_of(slots_.cbegin(), slots_.cend(), [&clientId](const SlotRecord& slot) {
                    return slot.clientId == clientId;
                });
            },
            {},
            [this](const std::string& clientId) { return removeSlot(clientId); },
            [this](const std::string& clientId, QWidget*) { return showSlotGui(clientId); },
            {});
        window_->setCentralWidget(canvas_);

        auto* nodeDock = new QDockWidget(QStringLiteral("Chain nodes"), window_);
        nodeList_ = new QTreeWidget(nodeDock);
        nodeList_->setHeaderHidden(true);
        nodeDock->setWidget(nodeList_);
        window_->addDockWidget(Qt::LeftDockWidgetArea, nodeDock);
        QObject::connect(nodeList_, &QTreeWidget::itemDoubleClicked, window_, [this](QTreeWidgetItem* item, int) {
            const auto token = item->data(0, Qt::UserRole).toString();
            if (token == QStringLiteral("add-vst-slot")) {
                addSlot();
            } else if (token == QStringLiteral("vst-slot")) {
                canvas_->selectNode(item->data(0, Qt::UserRole + 1).toString().toStdString());
            } else if (token == QStringLiteral("audio-input")) {
                addPort(PortKind::audio, Direction::input);
            } else if (token == QStringLiteral("audio-output")) {
                addPort(PortKind::audio, Direction::output);
            } else if (token == QStringLiteral("midi-input")) {
                addPort(PortKind::midi, Direction::input);
            } else if (token == QStringLiteral("midi-output")) {
                addPort(PortKind::midi, Direction::output);
            }
        });

        auto* inspectorDock = new QDockWidget(QStringLiteral("Chain settings"), window_);
        auto* inspector = new QWidget(inspectorDock);
        auto* inspectorLayout = new QVBoxLayout(inspector);
        auto* portTitle = new QLabel(QStringLiteral("Ports"), inspector);
        portList_ = new QListWidget(inspector);
        auto* renamePortButton = new QPushButton(QStringLiteral("Rename port"), inspector);
        auto* deletePortButton = new QPushButton(QStringLiteral("Delete port"), inspector);
        auto* slotTitle = new QLabel(QStringLiteral("VST slots"), inspector);
        slotList_ = new QListWidget(inspector);
        bypassCheck_ = new QCheckBox(QStringLiteral("Bypass selected slot"), inspector);
        auto* addSlotButton = new QPushButton(QStringLiteral("Add VST slot"), inspector);
        auto* removeSlotButton = new QPushButton(QStringLiteral("Remove selected slot"), inspector);
        auto* showGuiButton = new QPushButton(QStringLiteral("Show selected VST GUI"), inspector);
        inspectorLayout->addWidget(portTitle);
        inspectorLayout->addWidget(portList_, 1);
        inspectorLayout->addWidget(renamePortButton);
        inspectorLayout->addWidget(deletePortButton);
        inspectorLayout->addWidget(slotTitle);
        inspectorLayout->addWidget(slotList_, 1);
        inspectorLayout->addWidget(bypassCheck_);
        inspectorLayout->addWidget(addSlotButton);
        inspectorLayout->addWidget(removeSlotButton);
        inspectorLayout->addWidget(showGuiButton);
        inspectorDock->setWidget(inspector);
        window_->addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

        QObject::connect(renamePortButton, &QPushButton::clicked, window_, [this] {
            if (portList_ != nullptr && portList_->currentItem() != nullptr)
                renamePort(portList_->currentItem()->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(deletePortButton, &QPushButton::clicked, window_, [this] {
            if (portList_ != nullptr && portList_->currentItem() != nullptr)
                removePort(portList_->currentItem()->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(addSlotButton, &QPushButton::clicked, window_, [this] { addSlot(); });
        QObject::connect(removeSlotButton, &QPushButton::clicked, window_, [this] {
            if (slotList_ != nullptr && slotList_->currentItem() != nullptr)
                removeSlot(slotList_->currentItem()->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(showGuiButton, &QPushButton::clicked, window_, [this] {
            if (slotList_ != nullptr && slotList_->currentItem() != nullptr)
                showSlotGui(slotList_->currentItem()->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(bypassCheck_, &QCheckBox::toggled, window_, [this](bool checked) {
            if (slotList_ != nullptr && slotList_->currentItem() != nullptr)
                setSlotBypass(slotList_->currentItem()->data(Qt::UserRole).toString().toStdString(), checked);
        });
        QObject::connect(slotList_, &QListWidget::currentRowChanged, window_, [this](int row) {
            if (bypassCheck_ == nullptr)
                return;
            const QSignalBlocker blocker(bypassCheck_);
            bypassCheck_->setEnabled(row >= 0 && row < static_cast<int>(slots_.size()));
            bypassCheck_->setChecked(row >= 0 && row < static_cast<int>(slots_.size()) && slots_[static_cast<std::size_t>(row)].bypassed);
        });
        refreshControls();
        refreshGraph();
        auto* timer = new QTimer(window_);
        timer->setInterval(500);
        QObject::connect(timer, &QTimer::timeout, window_, [this] { rebuildRouting(); });
        timer->start();
    }

    inline static std::uint64_t nextInstance_ = 1;
    bool initialized_ = false;
    QString instanceId_;
    QString clientName_;
    QString displayName_ { QStringLiteral("VST Chain") };
    std::string chainClientId_;
    QString status_ { QStringLiteral("Not started.") };
    std::vector<ChainPort> ports_;
    std::vector<SlotRecord> slots_;
    std::vector<std::pair<std::string, std::string>> routingConnections_;
    std::atomic_bool routeThroughSlots_ = false;
    WinJACKNexus::Audio::JackClient jackClient_;
    QPointer<QMainWindow> window_;
    QPointer<PatchbayCanvas> canvas_;
    QPointer<QTreeWidget> nodeList_;
    QPointer<QListWidget> portList_;
    QPointer<QListWidget> slotList_;
    QPointer<QCheckBox> bypassCheck_;
    std::string serializedSettings_;
};

}

const char* wjn_get_node_api_version(void)
{
    return "2";
}

std::uint32_t wjn_get_node_count(void)
{
    return 1;
}

const char* wjn_get_node_name(std::uint32_t index)
{
    return index == 0 ? kNodeName : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
    return index == 0 ? kNodeCategory : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    return index == 0 ? new ChainHostNode : nullptr;
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
    auto* chain = dynamic_cast<ChainHostNode*>(node);
    return chain == nullptr ? nullptr : chain->createSettingsWidget(parent);
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode*, QWidget* widget)
{
    delete widget;
}

const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node)
{
    auto* chain = dynamic_cast<ChainHostNode*>(node);
    return chain == nullptr ? "" : chain->settingsJson();
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node, const char* json)
{
    auto* chain = dynamic_cast<ChainHostNode*>(node);
    return chain == nullptr ? 0 : chain->restoreSettingsJson(json);
}

extern "C" WINJACK_NODE_API int wjn_prepare_node_for_canvas(WinJACKNexus::Node::INode* node)
{
    return dynamic_cast<ChainHostNode*>(node) == nullptr ? 0 : 1;
}

extern "C" WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode* node)
{
    auto* chain = dynamic_cast<ChainHostNode*>(node);
    return chain == nullptr || !chain->showChainGui() ? 0 : 1;
}

extern "C" WINJACK_NODE_API const char* wjn_get_node_client_id(WinJACKNexus::Node::INode* node)
{
    static thread_local std::string clientId;
    const auto* chain = dynamic_cast<ChainHostNode*>(node);
    clientId = chain == nullptr ? std::string {} : chain->expectedClientId();
    return clientId.c_str();
}
