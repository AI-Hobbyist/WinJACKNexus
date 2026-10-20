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
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackMidiBuffer.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/NativeWindowUtils.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>

#include <mod_patchbay/PatchbayCanvas.hpp>
#include <mod_patchbay/VirtualGraph.hpp>
#include <mod_patchbay/LocalizedDialogs.hpp>

#include <cerrno>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDockWidget>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QDirIterator>
#include <QSettings>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QTreeWidget>
#include <QToolBar>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Service.hpp"

namespace {

QString patchText(const char* key,const char* english,const QVariantMap& arguments={}) {
	return WinJACKNexus::UI::localizedText("patchbay",key,english,arguments);
}
void bindPatchText(QObject* object,const char* property,const char* key,const char* english) {
	WinJACKNexus::UI::bindLocalizedText(object,property,"patchbay",key,english);
}
WinJACKNexus::UI::Controls::ActionButton* patchButton(const char* key,const char* english,QWidget* parent) {
	auto* button=new WinJACKNexus::UI::Controls::ActionButton({},parent);
	bindPatchText(button,"text",key,english); return button;
}
template<class Owner> QMenu* patchMenu(Owner* owner,const char* key,const char* english) {
	auto* menu=owner->addMenu(patchText(key,english));
	bindPatchText(menu,"title",key,english); return menu;
}
template<class... Arguments> QAction* patchAction(QMenu* menu,const char* key,const char* english,Arguments&&... arguments) {
	auto* action=menu->addAction(patchText(key,english),std::forward<Arguments>(arguments)...);
	action->setData(QString::fromUtf8(key));
	bindPatchText(action,"text",key,english); return action;
}
QString categoryDisplayName(const QString& identity) {
	if (identity==QStringLiteral("Audio Adapter")) return patchText("category.adapter","Audio adapter");
	if (identity==QStringLiteral("Windows Native Driver")) return patchText("category.windowsDriver","Windows native drivers");
	if (identity==QStringLiteral("Sub Graph")) return patchText("category.subgraph","Subgraph");
	if (identity==QStringLiteral("Sub Graph Interface")) return patchText("category.interface","Subgraph interfaces");
	if (identity==QStringLiteral("Meter")) return patchText("category.meter","Meters");
	if (identity==QStringLiteral("Digital Mixer")) return patchText("category.mixer","Mixer");
    if (identity == QStringLiteral("Classic Mixer"))
        return WinJACKNexus::UI::localizedText("classicMixer", "name", "Classic Mixer");
	if (identity==QStringLiteral("Patchbay")) return patchText("category.patchbay","Patchbay");
	if (identity==QStringLiteral("jackclient")) return patchText("category.jackClient","JACK client");
	if (identity==QStringLiteral("根目录")) return patchText("category.root","Root directory");
	if (identity==QStringLiteral("VST Plugins")) return patchText("category.vst","VST plugins");
	return identity;
}
constexpr int libraryIdentityRole=Qt::UserRole+20;
constexpr int nodeCapabilitiesRole = Qt::UserRole + 23;
constexpr int nodeFeaturesRole = Qt::UserRole + 24;
constexpr int nodeTooltipBaseRole = Qt::UserRole + 25;

QString capabilityTooltip(const QString &description, uint32_t capabilities, uint32_t features)
{
    const auto answer = [](bool value) {
        return value ? patchText("capability.yes", "Yes") : patchText("capability.no", "No");
    };
    const QString lines = patchText("capability.jack", "Main graph JACK node") + ": " +
                          answer(capabilities & WJN_NODE_JACK_NODE) + '\n' +
                          patchText("capability.virt", "Subgraph virtual node") + ": " +
                          answer(capabilities & WJN_NODE_VIRT_NODE) + '\n' +
                          patchText("capability.gui", "Built-in GUI") + ": " +
                          answer(features & WJN_NODE_HAS_GUI);
    return description.isEmpty() ? lines : description + '\n' + lines;
}

void setItemCapabilities(QTreeWidgetItem *item, uint32_t capabilities, uint32_t features)
{
    item->setData(0, nodeCapabilitiesRole, capabilities);
    item->setData(0, nodeFeaturesRole, features);
    item->setData(0, nodeTooltipBaseRole, item->toolTip(0));
    item->setToolTip(0, capabilityTooltip(item->toolTip(0), capabilities, features));
}

QString libraryItemIdentity(const QTreeWidgetItem* item) {
	return item->data(0,libraryIdentityRole).toString();
}

QSettings patchbaySettings()
{
	return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
	                 QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay"));
}

bool restoreMeterbridgeProjectState(const QJsonObject& state)
{
	const auto module = GetModuleHandleW(L"mod_meterbrigde.dll");
	using RestoreProjectState = int (*)(const char*);
	const auto restore = module == nullptr ? nullptr
		: reinterpret_cast<RestoreProjectState>(GetProcAddress(module, "wjn_meterbridge_restore_project_state_json"));
	if (restore == nullptr)
		return state.isEmpty();
	const auto serializedState = QJsonDocument(state).toJson(QJsonDocument::Compact);
	return restore(serializedState.constData()) != 0;
}

class PatchbayWindow final : public QMainWindow {
public:
	explicit PatchbayWindow(QWidget* parent = nullptr)
		: QMainWindow(parent)
	{
	}

	void setCloseConfirmation(std::function<bool()> confirmation)
	{
		closeConfirmation_ = std::move(confirmation);
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		if (closeConfirmation_ && !closeConfirmation_()) {
			event->ignore();
			return;
		}
		QMainWindow::closeEvent(event);
	}

private:
	std::function<bool()> closeConfirmation_;
};

QMimeData* moduleNodeMimeData(const QTreeWidgetItem* item)
{
	const auto modulePath = item->data(0, Qt::UserRole).toString();
	const auto vstSettings = item->data(0, Qt::UserRole + 4).toString();
	const auto subGraphName = item->data(0, Qt::UserRole + 2).toString();
	const auto interfaceType = item->data(0, Qt::UserRole + 3).toString();
	if (modulePath.isEmpty() && subGraphName.isEmpty() && interfaceType.isEmpty())
		return nullptr;

	auto* mimeData = new QMimeData;
	if (!modulePath.isEmpty()) {
		const auto payload = modulePath.toUtf8() + '\n' + QByteArray::number(item->data(0, Qt::UserRole + 1).toUInt());
		if (!vstSettings.isEmpty())
			mimeData->setData("application/x-winjacknexus-vst-node", payload + '\n' + vstSettings.toUtf8());
		else
			mimeData->setData("application/x-winjacknexus-module-node", payload);
	} else {
		mimeData->setData(interfaceType.isEmpty() ? "application/x-winjacknexus-subgraph-node"
		                                        : "application/x-winjacknexus-interface-node",
		                 (interfaceType.isEmpty() ? subGraphName : interfaceType).toUtf8());
	}
	return mimeData;
}

QTreeWidgetItem* addCategory(QTreeWidget* tree, const QString& name)
{
	auto* category = new QTreeWidgetItem(tree, { name });
	category->setData(0,libraryIdentityRole,name);
	category->setText(0,categoryDisplayName(name));
	category->setToolTip(0,category->text(0));
	category->setExpanded(true);
	return category;
}

QTreeWidgetItem* addCategory(QTreeWidgetItem* parent, const QString& name, bool expanded = true)
{
	auto* category = new QTreeWidgetItem(parent, { name });
	category->setData(0,libraryIdentityRole,name);
	category->setText(0,categoryDisplayName(name));
	category->setToolTip(0,category->text(0));
	category->setExpanded(expanded);
	return category;
}

void captureExpandedCategories(const QTreeWidgetItem* parent, const QString& parentPath, QVariantMap& states)
{
	for (int index = 0; index < parent->childCount(); ++index) {
		const auto* item = parent->child(index);
		if (item->childCount() == 0)
			continue;
			const auto path = parentPath + QChar(0x1f) + libraryItemIdentity(item);
		states.insert(path, item->isExpanded());
		captureExpandedCategories(item, path, states);
	}
}

QVariantMap captureExpandedCategories(const QTreeWidget* tree)
{
	QVariantMap states;
	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		const auto* item = tree->topLevelItem(index);
		if (item->childCount() == 0)
			continue;
		const auto path = libraryItemIdentity(item);
		states.insert(path, item->isExpanded());
		captureExpandedCategories(item, path, states);
	}
	return states;
}

void restoreExpandedCategories(QTreeWidgetItem* parent, const QString& parentPath, const QVariantMap& states)
{
	for (int index = 0; index < parent->childCount(); ++index) {
		auto* item = parent->child(index);
		if (item->childCount() == 0)
			continue;
		const auto path = parentPath + QChar(0x1f) + libraryItemIdentity(item);
		if (const auto state = states.constFind(path); state != states.cend())
			item->setExpanded(state->toBool());
		restoreExpandedCategories(item, path, states);
	}
}

void restoreExpandedCategories(QTreeWidget* tree, const QVariantMap& states)
{
	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		auto* item = tree->topLevelItem(index);
		if (item->childCount() == 0)
			continue;
		const auto path = libraryItemIdentity(item);
		if (const auto state = states.constFind(path); state != states.cend())
			item->setExpanded(state->toBool());
		restoreExpandedCategories(item, path, states);
	}
}

void addSubGraphItem(QTreeWidgetItem* category)
{
	auto* item = new QTreeWidgetItem(category, { patchText("graph.defaultName","Subgraph") });
	item->setData(0,Qt::UserRole+21,QVariantMap{{"domain",QByteArray("patchbay")},{"key",QByteArray("graph.defaultName")},{"english",QByteArray("Subgraph")}});
	item->setData(0, Qt::UserRole + 2, QStringLiteral("Sub Graph"));
    setItemCapabilities(item, WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE, 0);
}

void addInterfaceItem(QTreeWidgetItem* category, const QString& type, const QString& name)
{
	const auto key=QByteArray("interface.")+type.toUtf8();
	const QVariantMap display{{"domain",QByteArray("patchbay")},{"key",key},{"english",name.toUtf8()}};
	auto* item = new QTreeWidgetItem(category, { WinJACKNexus::UI::localizedText("patchbay",key.constData(),name.toUtf8().constData()) });
	item->setData(0,Qt::UserRole+21,display);
	item->setData(0, Qt::UserRole + 3, type);
    setItemCapabilities(item, WJN_NODE_VIRT_NODE, 0);
}

bool clientCanSplit(const WinJACKNexus::Patchbay::Core::Snapshot& snapshot, const std::string& clientId)
{
	bool hasInput = false;
	bool hasOutput = false;
	for (const auto& port : snapshot.ports) {
		if (!port.alive || port.clientId != clientId)
			continue;
		hasInput = hasInput || port.direction == WinJACKNexus::Patchbay::Core::Direction::input;
		hasOutput = hasOutput || port.direction == WinJACKNexus::Patchbay::Core::Direction::output;
	}
	return hasInput && hasOutput;
}

void applyNodeLibraryFilter(QTreeWidget* tree, const QString& query)
{
	const auto needle = query.trimmed();
	const auto applyFilter = [&needle](auto&& self, QTreeWidgetItem* item, bool ancestorMatches) -> bool {
		const bool itemMatches = ancestorMatches || needle.isEmpty()
			|| item->text(0).contains(needle, Qt::CaseInsensitive);
		bool childMatches = false;
		for (int childIndex = 0; childIndex < item->childCount(); ++childIndex)
			childMatches = self(self, item->child(childIndex), itemMatches) || childMatches;
		const bool visible = itemMatches || childMatches;
		item->setHidden(!visible);
		if (!needle.isEmpty() && visible && item->childCount() != 0)
			item->setExpanded(true);
		return visible;
	};
	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		auto* category = tree->topLevelItem(index);
		applyFilter(applyFilter, category, false);
	}
}

struct ModuleNodeInfo final {
	QString category;
	QString name;
	QString modulePath;
	std::uint32_t index = 0;
	QVariantMap display;
	std::shared_ptr<WinJACKNexus::UI::I18nDomainLease> languageLease;
    uint32_t capabilities{};
    uint32_t features{};
};

QVariantMap builtinNodeDisplay(const QString &path, std::uint32_t, const QString &english)
{
    struct Display
    {
        const char *file;
        const char *domain;
        const char *key;
        const char *description;
    };
    static constexpr Display displays[]{
        {"mod_wasapi_in.dll", "adapter", "node.wasapiIn",
         "Routes a shared-mode WASAPI capture device into JACK."},
        {"mod_wasapi_out.dll", "adapter", "node.wasapiOut",
         "Routes JACK audio to a shared-mode WASAPI render device."},
        {"mod_system_midi_in.dll", "adapter", "node.midiIn",
         "Routes a system MIDI input device into JACK."},
        {"mod_system_midi_out.dll", "adapter", "node.midiOut",
         "Routes JACK MIDI events to a system MIDI output device."},
        {"mod_meter_mono.dll", "patchbay", "node.meterMono", "Single-channel Meterbridge probe."},
        {"mod_meter_stereo.dll", "patchbay", "node.meterStereo", "Two-channel Meterbridge probe."},
        {"mod_meter_5_1.dll", "patchbay", "node.meter51", "5.1-channel Meterbridge probe."},
        {"mod_meter_7_1.dll", "patchbay", "node.meter71", "7.1-channel Meterbridge probe."},
        {"mod_meter_custom.dll", "patchbay", "node.meterCustom",
         "Custom-channel Meterbridge probe."}};
    const auto file = QFileInfo(path).fileName();
    for (const auto &display : displays)
    {
        if (file.compare(QString::fromLatin1(display.file), Qt::CaseInsensitive) == 0)
            return {{"domain", QByteArray(display.domain)},
                    {"key", QByteArray(display.key)},
                    {"english", english.toUtf8()},
                    {"descriptionKey", QByteArray(display.key) + ".description"},
                    {"descriptionEnglish", QByteArray(display.description)}};
    }
    return {};
}
QString nodeDisplayText(const QVariantMap& display,const QString& original) {
	return display.isEmpty()?original:WinJACKNexus::UI::localizedText(display.value("domain").toByteArray().constData(),
		display.value("key").toByteArray().constData(),display.value("english").toByteArray().constData());
}
QString nodeDisplayTooltip(const QVariantMap& display,const QString& original) {
	const auto name=nodeDisplayText(display,original);
	const auto key=display.value("descriptionKey").toByteArray();
	return key.isEmpty()?name:name+QLatin1Char('\n')+WinJACKNexus::UI::localizedText(display.value("domain").toByteArray().constData(),key.constData(),display.value("descriptionEnglish").toByteArray().constData());
}
std::shared_ptr<WinJACKNexus::UI::I18nDomainLease> moduleLanguageLease(const QString& path,std::uint32_t index) {
	static std::map<QString,std::weak_ptr<WinJACKNexus::UI::I18nDomainLease>> leases;
	const auto identity=QFileInfo(path).canonicalFilePath()+QLatin1Char('#')+QString::number(index);
	if (auto existing=leases[identity].lock()) return existing;
	if (auto* manager=WinJACKNexus::UI::I18nManager::installed()) {
		auto lease=manager->registerLibrary(path,WinJACKNexus::UI::I18nManager::Extension::node,index);
		if (lease) {
			auto result=std::make_shared<WinJACKNexus::UI::I18nDomainLease>(std::move(lease));
			leases[identity]=result; return result;
		}
	}
	return {};
}
class NodeLibraryLanguageLeases final:public QObject {
public:
	explicit NodeLibraryLanguageLeases(QObject* parent):QObject(parent) {setObjectName(QStringLiteral("nodeLibraryI18nLeases"));}
	std::vector<std::shared_ptr<WinJACKNexus::UI::I18nDomainLease>> leases;
};

struct VstPluginInfo final {
	QString name;
	QString vendor;
	QString version;
	QString type;
	QString path;
	QString category;
	QString subcategory;
	QString internalPlugin;
	QString infoFile;
	int bits = 0;
	bool instrument = false;
	bool verified = true;
};

QString infoString(const QJsonObject& object, const QString& primaryKey, const QString& fallbackKey = {})
{
	auto value = object.value(primaryKey);
	if (value.isUndefined() && !fallbackKey.isEmpty())
		value = object.value(fallbackKey);
	if (value.isString())
		return value.toString().trimmed();
	if (value.isDouble())
		return QString::number(static_cast<qlonglong>(value.toDouble()));
	return {};
}

struct SubGraphInterfacePort final {
	WinJACKNexus::Audio::JackPort port;
	std::string id;
	WinJACKNexus::Patchbay::Core::PortKind kind = WinJACKNexus::Patchbay::Core::PortKind::unknown;
	bool isInput = false;
    std::size_t graphSlot = WinJACKNexus::Patchbay::VirtualGraph::invalidSlot;
};

struct SubGraphPortSet final
{
    using Graph = WinJACKNexus::Patchbay::VirtualGraph;
    WinJACKNexus::Audio::JackClient client;
    std::vector<SubGraphInterfacePort> ports;
    std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> routes;
    std::vector<PatchbayInterfaceInfo> interfaces;
    std::vector<Graph::Port> modulePorts;
    std::vector<Graph::Processor> processors;
    Graph graph;
    struct BoundaryConnection
    {
        std::string ownedPort;
        std::string source;
        std::string destination;
    };
    std::vector<BoundaryConnection> suspendedConnections;
    uint32_t maxFrames = 8192;
    uint64_t samplePosition{};
    double sampleRate = 48000.0;
    std::atomic<uint32_t> requestedRate{48000};
    std::atomic<uint32_t> requestedFrames{8192};
    std::atomic_bool clockReady{true};

    static int rateChanged(jack_nframes_t value, void *context) noexcept
    {
        auto &root = *static_cast<SubGraphPortSet *>(context);
        // JACK also notifies the current clock when a client is activated.
        // Treat only a changed value as a request to stop and prepare the graph.
        if (root.requestedRate.load(std::memory_order_acquire) == value)
            return 0;
        root.clockReady.store(false, std::memory_order_release);
        root.requestedRate.store(value, std::memory_order_release);
        return 0;
    }

    static int sizeChanged(jack_nframes_t value, void *context) noexcept
    {
        auto &root = *static_cast<SubGraphPortSet *>(context);
        if (root.requestedFrames.load(std::memory_order_acquire) == value)
            return 0;
        root.clockReady.store(false, std::memory_order_release);
        root.requestedFrames.store(value, std::memory_order_release);
        return 0;
    }

    bool openBoundary(const std::string &name)
    {
        if (offline || client.isOpen())
            return true;
        if (!client.open({name.c_str(), false, false, nullptr}) ||
            client.setProcessCallback(&SubGraphPortSet::process, this) != 0 ||
            client.setSampleRateCallback(&SubGraphPortSet::rateChanged, this) != 0 ||
            client.setBufferSizeCallback(&SubGraphPortSet::sizeChanged, this) != 0)
        {
            client.close();
            return false;
        }
        sampleRate = client.sampleRate();
        maxFrames = client.bufferSize();
        requestedRate.store(uint32_t(sampleRate));
        requestedFrames.store(maxFrames);
        clockReady.store(false);
        if (!synchronizeClock() || !client.activate())
        {
            client.close();
            return false;
        }
        return true;
    }

    bool synchronizeClock()
    {
        if (clockReady.load(std::memory_order_acquire))
            return true;
        const bool active = client.isActive();
        if (active && !pause())
            return false;
        const auto rate = requestedRate.load(std::memory_order_acquire);
        const auto frames = requestedFrames.load(std::memory_order_acquire);
        if (rate == 0 || frames == 0 || frames > 65536)
            return resume(active) && false;
        WjnNodePrepareV1 prepare{sizeof(prepare), frames, double(rate), nullptr};
        bool prepared = true;
        for (const auto &processor : processors)
        {
            processor.api->stop(processor.instance);
            prepared = processor.api->prepare(processor.instance, &prepare) != 0 && prepared;
        }
        sampleRate = rate;
        maxFrames = frames;
        samplePosition = 0;
        const bool valid = prepared && rebuild();
        clockReady.store(valid, std::memory_order_release);
        return resume(active) && valid;
    }
    bool offline{};

    ~SubGraphPortSet()
    {
        client.close();
    }

    std::vector<PatchbayInterfaceInfo> contextInterfaces(const std::string &context) const
    {
        std::vector<PatchbayInterfaceInfo> result;
        for (const auto &port : interfaces)
        {
            if (port.id.substr(0, port.id.rfind('/')) == context)
                result.push_back(port);
        }
        return result;
    }

    Graph::Topology topology() const
    {
        Graph::Topology result;
        result.ports = modulePorts;
        result.processors = processors;
        for (const auto &interfaceNode : interfaces)
        {
            const bool audio = interfaceNode.kind == WinJACKNexus::Patchbay::Core::PortKind::audio;
            result.ports.push_back(
                {interfaceNode.portId,
                 {},
                 0,
                 static_cast<uint32_t>(audio ? WJN_NODE_PORT_AUDIO : WJN_NODE_PORT_MIDI),
                 audio ? 1u : 0u,
                 WJN_NODE_PORT_INPUT,
                 true});
        }
        for (const auto &route : routes)
        {
            result.routes.push_back({route.sourcePortId, route.destinationPortId});
        }
        return result;
    }

    bool rebuild()
    {
        std::string error;
        if (!graph.rebuild(topology(), maxFrames, error))
        {
            qWarning().noquote() << "Subgraph topology rejected:" << QString::fromStdString(error);
            return false;
        }
        for (auto &port : ports)
        {
            port.graphSlot = graph.slot(port.id);
        }
        return true;
    }

    bool pause()
    {
        if (offline || !client.isActive())
            return true;
        auto &api = WinJACKNexus::Audio::JackApi::instance();
        std::vector<BoundaryConnection> connections;
        for (const auto &port : ports)
        {
            const auto *name = port.port.name();
            const auto **peers = api.portGetConnections(port.port.nativeHandle());
            if (name && peers)
            {
                for (size_t index = 0; peers[index]; ++index)
                {
                    connections.push_back({name, port.isInput ? peers[index] : name,
                                           port.isInput ? name : peers[index]});
                }
            }
            api.freeMemory(peers);
        }
        if (!client.deactivate())
            return false;
        suspendedConnections = std::move(connections);
        return true;
    }

    bool resume(bool active)
    {
        if (!active || offline)
            return true;
        if (!client.activate())
            return false;
        auto &api = WinJACKNexus::Audio::JackApi::instance();
        for (const auto &connection : suspendedConnections)
        {
            const bool retained = std::any_of(ports.begin(), ports.end(), [&](const auto &port) {
                const auto *name = port.port.name();
                return name && connection.ownedPort == name;
            });
            // Removed interfaces and clients that disappeared during the edit
            // cannot be reconnected. Other boundary connections must survive.
            if (!retained || !api.portByName(client.nativeHandle(), connection.source.c_str()) ||
                !api.portByName(client.nativeHandle(), connection.destination.c_str()))
                continue;
            const auto result = api.connect(client.nativeHandle(), connection.source.c_str(),
                                            connection.destination.c_str());
            if (result != 0 && result != EEXIST)
            {
                client.deactivate();
                return false;
            }
        }
        suspendedConnections.clear();
        return true;
    }

    bool connect(const std::string &source, const std::string &destination)
    {
        if (std::any_of(routes.begin(), routes.end(), [&](const auto &route) {
                return route.sourcePortId == source && route.destinationPortId == destination;
            }))
        {
            return true;
        }
        const bool active = client.isActive();
        if (active && !pause())
            return false;
        routes.push_back({source, destination});
        if (!rebuild())
        {
            routes.pop_back();
            resume(active);
            return false;
        }
        if (resume(active))
            return true;
        routes.pop_back();
        rebuild();
        resume(active);
        return false;
    }

    bool disconnect(const std::string &source, const std::string &destination)
    {
        const bool active = client.isActive();
        if (active && !pause())
            return false;
        const auto previous = routes;
        const auto removed = std::erase_if(routes, [&](const auto &route) {
            return route.sourcePortId == source && route.destinationPortId == destination;
        });
        if (removed && rebuild() && resume(active))
            return true;
        routes = previous;
        rebuild();
        resume(active);
        return false;
    }

    bool attach(Graph::Processor processor, const std::vector<Graph::Port> &descriptions)
    {
        const bool active = client.isActive();
        if (active && !pause())
            return false;
        const auto previousCount = modulePorts.size();
        processors.push_back(std::move(processor));
        modulePorts.insert(modulePorts.end(), descriptions.begin(), descriptions.end());
        if (rebuild() && resume(active))
            return true;
        processors.pop_back();
        modulePorts.resize(previousCount);
        rebuild();
        resume(active);
        return false;
    }

    bool detach(const std::string &id)
    {
        const bool active = client.isActive();
        if (active && !pause())
            return false;
        const auto previousRoutes = routes;
        const auto previousPorts = modulePorts;
        const auto previousProcessors = processors;
        std::unordered_set<std::string> removed;
        for (const auto &port : modulePorts)
        {
            if (port.owner == id)
                removed.insert(port.id);
        }
        std::erase_if(routes, [&](const auto &route) {
            return removed.contains(route.sourcePortId) ||
                   removed.contains(route.destinationPortId);
        });
        std::erase_if(modulePorts, [&](const auto &port) { return port.owner == id; });
        std::erase_if(processors, [&](const auto &processor) { return processor.id == id; });
        if (rebuild() && resume(active))
            return true;
        routes = previousRoutes;
        modulePorts = previousPorts;
        processors = previousProcessors;
        rebuild();
        resume(active);
        return false;
    }

    static int process(jack_nframes_t frames, void *context) noexcept
    {
        auto &root = *static_cast<SubGraphPortSet *>(context);
        for (auto &port : root.ports)
        {
            if (port.isInput)
                continue;
            if (port.kind == WinJACKNexus::Patchbay::Core::PortKind::audio)
            {
                if (auto *output = port.port.audioBuffer(frames))
                    std::fill_n(output, frames, 0.0f);
            }
            else
            {
                WinJACKNexus::Audio::JackMidiBuffer(port.port.midiBuffer(frames)).clear();
            }
        }
        if (!root.clockReady.load(std::memory_order_acquire) || !root.graph.begin(frames))
            return 0;
        for (auto &port : root.ports)
        {
            if (!port.isInput)
                continue;
            if (port.kind == WinJACKNexus::Patchbay::Core::PortKind::audio)
            {
                const auto *input = port.port.audioBuffer(frames);
                auto *buffer = root.graph.audio(port.graphSlot);
                if (input && buffer)
                    std::copy_n(input, frames, buffer);
            }
            else
            {
                WinJACKNexus::Audio::JackMidiBuffer input(port.port.midiBuffer(frames));
                for (uint32_t index = 0; index < input.eventCount(); ++index)
                {
                    WinJACKNexus::Audio::JackMidiEventView event;
                    if (input.event(index, event))
                    {
                        root.graph.appendMidi(
                            port.graphSlot,
                            {event.time, static_cast<uint32_t>(event.size), event.data}, frames);
                    }
                }
            }
        }
        root.graph.process(frames, root.samplePosition);
        root.samplePosition += frames;
        for (auto &port : root.ports)
        {
            if (port.isInput)
                continue;
            if (port.kind == WinJACKNexus::Patchbay::Core::PortKind::audio)
            {
                const auto *buffer = root.graph.audio(port.graphSlot);
                auto *output = port.port.audioBuffer(frames);
                if (buffer && output)
                    std::copy_n(buffer, frames, output);
            }
            else if (const auto *buffer = root.graph.midi(port.graphSlot))
            {
                WinJACKNexus::Audio::JackMidiBuffer output(port.port.midiBuffer(frames));
                for (uint32_t index = 0; index < buffer->eventCount; ++index)
                {
                    const auto &event = buffer->events[index];
                    output.write(event.frame, event.data, event.size);
                }
            }
        }
        return 0;
    }
};

struct SubGraphInterfaceDefinition final {
	const char* type;
	const char* jackType;
	unsigned long flags;
};

const std::array<SubGraphInterfaceDefinition, 4>& subGraphInterfaceDefinitions()
{
	static const std::array definitions {
			SubGraphInterfaceDefinition { "audio_in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput },
			SubGraphInterfaceDefinition { "audio_out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput },
			SubGraphInterfaceDefinition { "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput },
			SubGraphInterfaceDefinition { "midi_out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput }
	};
	return definitions;
}

std::string jackSafeToken(std::string value)
{
	for (auto& character : value) {
		if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_' && character != '-')
			character = '_';
	}
	return value;
}

QString clientBaseName(const std::string& clientId)
{
	const auto name = QString::fromStdString(clientId);
	const auto separator = name.lastIndexOf(QLatin1Char('-'));
	if (separator <= 0 || separator == name.size() - 1)
		return name;
	const auto suffix = name.sliced(separator + 1);
	const auto hasRandomSuffix = std::all_of(suffix.cbegin(), suffix.cend(), [](QChar character) {
		return character.isDigit() || (character >= QLatin1Char('a') && character <= QLatin1Char('f'))
			|| (character >= QLatin1Char('A') && character <= QLatin1Char('F'));
	});
	return hasRandomSuffix ? name.first(separator) : name;
}

bool isTopLevelSubGraphContext(const std::string& contextId)
{
	const auto firstSeparator = contextId.find('/');
	return firstSeparator != std::string::npos
		&& contextId.find('/', firstSeparator + 1) == std::string::npos;
}

std::string rootSubGraphContext(const std::string& contextId)
{
	const auto firstSeparator = contextId.find('/');
	if (firstSeparator == std::string::npos)
		return contextId;
	const auto secondSeparator = contextId.find('/', firstSeparator + 1);
	return secondSeparator == std::string::npos ? contextId : contextId.substr(0, secondSeparator);
}

bool moduleRecordsEqual(const std::vector<PatchbayProjectModule>& left,
	                       const std::vector<PatchbayProjectModule>& right)
{
	if (left.size() != right.size())
		return false;
	return std::equal(left.cbegin(), left.cend(), right.cbegin(), [](const auto& leftModule, const auto& rightModule) {
		return leftModule.modulePath == rightModule.modulePath
			&& leftModule.index == rightModule.index
			&& leftModule.clientId == rightModule.clientId
			&& leftModule.settingsJson == rightModule.settingsJson
			&& leftModule.contextId == rightModule.contextId;
	});
}

QString categoryFromModuleOrFolder(const char* exportedCategory, const QString& folderCategory)
{
	if (exportedCategory == nullptr)
		return folderCategory;

	const auto category = QString::fromUtf8(exportedCategory).trimmed();
	return category.isEmpty() ? folderCategory : category;
}

bool validNativeFxNodeModule(HMODULE library, const QString& path, bool* nativeFx)
{
	using Version = std::uint32_t (*)();
	using Count = std::uint32_t (*)();
	using Meta = const WjnFxMeta* (*)(std::uint32_t);
	const auto normalized = QDir::fromNativeSeparators(path);
	const bool inNativeFxDirectory = normalized.contains(QStringLiteral("/nodes/native_fx/"), Qt::CaseInsensitive);
	const auto fxVersion = reinterpret_cast<Version>(GetProcAddress(library, "wjn_fx_api_version"));
	*nativeFx = inNativeFxDirectory || fxVersion != nullptr;
	if (!*nativeFx) return true;
	const auto count = reinterpret_cast<Count>(GetProcAddress(library, "wjn_fx_count"));
	const auto meta = reinterpret_cast<Meta>(GetProcAddress(library, "wjn_fx_meta"));
	const auto boundaryVersion = reinterpret_cast<Version>(GetProcAddress(library, "wjn_fx_node_boundary_version"));
    return fxVersion != nullptr && fxVersion() == WJN_FX_API_VERSION && count != nullptr &&
           count() == 1 && meta != nullptr && meta(0) != nullptr &&
           (meta(0)->placements & WJN_FX_NODE) && boundaryVersion != nullptr &&
           boundaryVersion() == WJN_FX_NODE_BOUNDARY_VERSION &&
           GetProcAddress(library, "wjn_fx_node_validate_layout") != nullptr &&
           GetProcAddress(library, "wjn_fx_node_validate_layout_mode") != nullptr &&
           GetProcAddress(library, "wjn_fx_node_client_name") != nullptr &&
           GetProcAddress(library, "wjn_fx_node_set_client_name") != nullptr &&
           GetProcAddress(library, "wjn_show_node_gui") != nullptr &&
           GetProcAddress(library, "wjn_get_node_client_id") != nullptr;
}

bool isClassicMixerModule(const QString &path)
{
    const auto file = QFileInfo(path).fileName().toLower();
    return file == QStringLiteral("mod_classic_mixer.dll") ||
           file == QStringLiteral("mod_classic_mixer_8.dll") ||
           file == QStringLiteral("mod_classic_mixer_16.dll") ||
           file == QStringLiteral("mod_classic_mixer_24.dll");
}

std::vector<ModuleNodeInfo> scanNodeModules()
{
    using NodeName = const char *(*)(void);
    std::vector<ModuleNodeInfo> nodes;
    const QDir directory(QCoreApplication::applicationDirPath() + QStringLiteral("/nodes"));
    QDirIterator files(directory.absolutePath(), {QStringLiteral("*.dll")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        const auto path = files.next();
        // The legacy chooser DLL is superseded by the three fixed-layout modules.
        if (QFileInfo(path).fileName().compare(QStringLiteral("mod_classic_mixer.dll"),
                                             Qt::CaseInsensitive) == 0)
            continue;
        const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
        if (!library)
            continue;
        const auto name = reinterpret_cast<NodeName>(GetProcAddress(library, "wjn_get_node_name"));
        const auto category =
            reinterpret_cast<NodeName>(GetProcAddress(library, "wjn_get_node_category"));
        bool nativeFx = false;
        WinJACKNexus::Node::ModuleContract contract;
        if (WinJACKNexus::Node::inspectNodeModule(library, contract) ==
                WinJACKNexus::Node::ModuleError::none &&
            name && validNativeFxNodeModule(library, path, &nativeFx))
        {
            const auto *text = name();
            if (text && *text)
            {
                auto folder = directory.relativeFilePath(QFileInfo(path).absolutePath());
                if (folder.isEmpty() || folder == QStringLiteral("."))
                    folder = QStringLiteral("根目录");
                else
                    folder.replace(QDir::separator(), QStringLiteral(" / "));
                const auto categoryName =
                    nativeFx ? QStringLiteral("Native Fx")
                             : categoryFromModuleOrFolder(category ? category() : nullptr, folder);
                ModuleNodeInfo info{categoryName, QString::fromUtf8(text), path, 0};
                info.capabilities = contract.metadata.capabilities;
                info.features = contract.metadata.features;
                info.display = builtinNodeDisplay(path, 0, info.name);
                info.languageLease = moduleLanguageLease(path, 0);
                if (info.languageLease)
                {
                    using Descriptor = const char *(*)(void);
                    const auto descriptor = reinterpret_cast<Descriptor>(
                        GetProcAddress(library, "wjn_get_node_i18n_descriptor_json"));
                    const auto *json = descriptor ? descriptor() : nullptr;
                    const auto metadata =
                        json ? QJsonDocument::fromJson(json).object() : QJsonObject{};
                    const auto key = metadata.value("displayNameKey").toObject();
                    if (!key.isEmpty())
                        info.display = {{"domain", key.value("domain").toString().toUtf8()},
                                        {"key", key.value("key").toString().toUtf8()},
                                        {"english", info.name.toUtf8()}};
                }
                nodes.push_back(std::move(info));
            }
        }
        FreeLibrary(library);
    }
    return nodes;
}

std::vector<VstPluginInfo> scanVstPlugins()
{
	std::vector<VstPluginInfo> plugins;
	const QDir pluginsDirectory(QCoreApplication::applicationDirPath() + QStringLiteral("/nodes/VSTPlugins"));
	if (!pluginsDirectory.exists())
		return plugins;

	QDirIterator iterator(pluginsDirectory.absolutePath(), { QStringLiteral("*.vstinfo"), QStringLiteral("*.vst3info") },
	                     QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		QFile file(iterator.next());
		if (!file.open(QIODevice::ReadOnly))
			continue;
		QJsonParseError error;
		const auto document = QJsonDocument::fromJson(file.readAll(), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject())
			continue;
		const auto object = document.object();
		VstPluginInfo plugin;
		plugin.name = object.value(QStringLiteral("name")).toString().trimmed();
		plugin.vendor = object.value(QStringLiteral("vendor")).toString().trimmed();
		plugin.version = object.value(QStringLiteral("version")).toString().trimmed();
		plugin.type = object.value(QStringLiteral("type")).toString().trimmed().toLower();
		plugin.path = object.value(QStringLiteral("path")).toString().trimmed();
		plugin.category = object.value(QStringLiteral("category")).toString().trimmed();
		plugin.subcategory = object.value(QStringLiteral("subcategory")).toString().trimmed();
		plugin.internalPlugin = infoString(object, QStringLiteral("internal_plugin"), QStringLiteral("internalPlugin"));
		if (plugin.internalPlugin.isEmpty() && plugin.type == QStringLiteral("vst3"))
			plugin.internalPlugin = plugin.name;
		plugin.infoFile = file.fileName();
		plugin.bits = object.value(QStringLiteral("bits")).toInt();
		plugin.instrument = object.value(QStringLiteral("isInstrument")).toBool();
		plugin.verified = object.value(QStringLiteral("verified")).toBool(true);
		if (plugin.name.isEmpty() || plugin.path.isEmpty()
			|| (plugin.type != QStringLiteral("vst2") && plugin.type != QStringLiteral("vst3"))
			|| (plugin.bits != 32 && plugin.bits != 64))
			continue;
		plugins.push_back(std::move(plugin));
	}
	std::sort(plugins.begin(), plugins.end(), [](const auto& left, const auto& right) {
		return std::tie(left.type, left.instrument, left.category, left.subcategory, left.name)
			< std::tie(right.type, right.instrument, right.category, right.subcategory, right.name);
	});
	return plugins;
}

QString vstPluginCategory(const VstPluginInfo& plugin, const QDir& pluginsDirectory)
{
	auto relativeDirectory = pluginsDirectory.relativeFilePath(QFileInfo(plugin.infoFile).absolutePath());
	if (!relativeDirectory.isEmpty() && relativeDirectory != QStringLiteral("."))
		return QStringLiteral("VST Plugins / ") + relativeDirectory.replace(QDir::separator(), QStringLiteral(" / "));
	return QStringLiteral("VST Plugins / ")
		+ (plugin.type == QStringLiteral("vst2") ? QStringLiteral("VST2") : QStringLiteral("VST3"))
		+ QStringLiteral(" / ") + (plugin.instrument ? QStringLiteral("Instruments") : QStringLiteral("Effects"));
}

QString vstPluginSettings(const VstPluginInfo& plugin)
{
	return QString::fromUtf8(QJsonDocument(QJsonObject {
		{ QStringLiteral("path"), plugin.path },
		{ QStringLiteral("bits"), QString::number(plugin.bits) },
		{ QStringLiteral("internalPlugin"), plugin.internalPlugin },
		{ QStringLiteral("internal_plugin"), plugin.internalPlugin },
		{ QStringLiteral("internalPluginName"), plugin.name }
	}).toJson(QJsonDocument::Compact));
}

void addVstPluginItems(QTreeWidget *tree, const std::vector<ModuleNodeInfo> &modules, uint32_t mode)
{
	const auto vstHost = std::find_if(modules.cbegin(), modules.cend(), [](const auto& module) {
		return QFileInfo(module.modulePath).fileName().compare(QStringLiteral("mod_vst_host.dll"),Qt::CaseInsensitive)==0;
	});
    if (vstHost == modules.cend() || !(vstHost->capabilities & mode))
        return;

    const QDir pluginsDirectory(QCoreApplication::applicationDirPath() +
                                QStringLiteral("/nodes/VSTPlugins"));
    auto* vstCategory = addCategory(tree, QStringLiteral("VST Plugins"));
	vstCategory->setExpanded(false);
	std::map<QString, QTreeWidgetItem*> categories { { QStringLiteral("VST Plugins"), vstCategory } };
	for (const auto& plugin : scanVstPlugins()) {
		auto relativeDirectory = pluginsDirectory.relativeFilePath(QFileInfo(plugin.infoFile).absolutePath());
		const bool generatedDirectory=relativeDirectory.isEmpty() || relativeDirectory == QStringLiteral(".");
		if (generatedDirectory) {
			relativeDirectory = plugin.type == QStringLiteral("vst2") ? QStringLiteral("VST2") : QStringLiteral("VST3");
			relativeDirectory += plugin.instrument ? QStringLiteral("/Instruments") : QStringLiteral("/Effects");
		}
		QString categoryPath = QStringLiteral("VST Plugins");
		QTreeWidgetItem* category = vstCategory;
		for (const auto& segment : QDir::fromNativeSeparators(relativeDirectory).split('/', Qt::SkipEmptyParts)) {
			categoryPath += QChar(0x1f) + segment;
			auto [childCategory, inserted] = categories.try_emplace(categoryPath, nullptr);
			if (inserted) {
				childCategory->second = addCategory(category, segment, false);
				if (generatedDirectory && (segment==QStringLiteral("Instruments") || segment==QStringLiteral("Effects"))) {
					const auto key=segment==QStringLiteral("Instruments")?QByteArray("category.instruments"):QByteArray("category.effects");
					const QVariantMap display{{"domain",QByteArray("patchbay")},{"key",key},{"english",segment.toUtf8()}};
					childCategory->second->setData(0,Qt::UserRole+21,display);
					childCategory->second->setText(0,nodeDisplayText(display,segment));
					childCategory->second->setToolTip(0,childCategory->second->text(0));
				}
			}
			category = childCategory->second;
		}
		auto* item = new QTreeWidgetItem(category, { plugin.name });
		item->setData(0, Qt::UserRole, vstHost->modulePath);
		item->setData(0, Qt::UserRole + 1, vstHost->index);
		item->setData(0, Qt::UserRole + 4, vstPluginSettings(plugin));
		item->setToolTip(0, QStringLiteral("%1\n%2\n%3 %4-bit\n%5")
			.arg(plugin.vendor, plugin.version, plugin.type.toUpper(), QString::number(plugin.bits), plugin.path));
        setItemCapabilities(item, vstHost->capabilities, vstHost->features);
        if (!plugin.verified)
            item->setForeground(0, QColor(QStringLiteral("#8193a5")));
    }
}

void populateNodeLibrary(QTreeWidget* tree,
                         const QString& filterQuery = {},
                         bool subGraphContext = false)
{
	const auto expandedCategories = captureExpandedCategories(tree);
	tree->clear();
	const auto modules = scanNodeModules();
	auto* leaseOwner=static_cast<NodeLibraryLanguageLeases*>(tree->findChild<QObject*>(QStringLiteral("nodeLibraryI18nLeases")));
	if (!leaseOwner) leaseOwner=new NodeLibraryLanguageLeases(tree);
	std::vector<std::shared_ptr<WinJACKNexus::UI::I18nDomainLease>> leases;
	for (const auto& module:modules) if (module.languageLease) leases.push_back(module.languageLease);
	leaseOwner->leases=std::move(leases);
	auto* subGraphs = addCategory(tree, QStringLiteral("Sub Graph"));
	addSubGraphItem(subGraphs);
	if (subGraphContext) {
		auto* interfaces = addCategory(tree, QStringLiteral("Sub Graph Interface"));
		addInterfaceItem(interfaces, QStringLiteral("audio_in"), QStringLiteral("Sub Graph Audio In"));
		addInterfaceItem(interfaces, QStringLiteral("audio_out"), QStringLiteral("Sub Graph Audio Out"));
		addInterfaceItem(interfaces, QStringLiteral("midi_in"), QStringLiteral("Sub Graph MIDI In"));
		addInterfaceItem(interfaces, QStringLiteral("midi_out"), QStringLiteral("Sub Graph MIDI Out"));
	}

    const uint32_t mode = subGraphContext ? WJN_NODE_MODE_VIRT : WJN_NODE_MODE_JACK;

    std::map<QString, QTreeWidgetItem *> moduleCategories;
    for (const auto &node : modules)
    {
        if (!(node.capabilities & mode))
            continue;
        if (QFileInfo(node.modulePath)
                .fileName()
                .compare(QStringLiteral("mod_digital_mixer.dll"), Qt::CaseInsensitive) == 0)
            continue;
        auto [category, inserted] = moduleCategories.try_emplace(node.category, nullptr);
		if (inserted)
			category->second = addCategory(tree, node.category);
		auto* item = new QTreeWidgetItem(category->second, { nodeDisplayText(node.display,node.name) });
		item->setData(0,Qt::UserRole+21,node.display);
		item->setToolTip(0,nodeDisplayTooltip(node.display,node.name));
		item->setData(0, Qt::UserRole, node.modulePath);
		item->setData(0, Qt::UserRole + 1, node.index);
        setItemCapabilities(item, node.capabilities, node.features);
    }
    addVstPluginItems(tree, modules, mode);
    applyNodeLibraryFilter(tree, filterQuery);
    if (filterQuery.trimmed().isEmpty())
        restoreExpandedCategories(tree, expandedCategories);
}

class LoadedModuleNode final {
public:
  static std::unique_ptr<LoadedModuleNode> create(QString modulePath, std::uint32_t index,
                                                  const std::string &settingsJson = {},
                                                  uint32_t mode = WJN_NODE_MODE_JACK,
                                                  double sampleRate = 48000.0,
                                                  uint32_t maxFrames = 8192) noexcept
  {
      using ApiVersion = const char *(*)();
      using CreateNode = WinJACKNexus::Node::INode *(*)(uint32_t mode);
      using DestroyNode = void (*)(WinJACKNexus::Node::INode *node);
      using SetNodeSettingsJson = int (*)(WinJACKNexus::Node::INode *node, const char *json);
      using PrepareNodeForCanvas = int (*)(WinJACKNexus::Node::INode *node);
      using ShowNodeGui = int (*)(WinJACKNexus::Node::INode *node);
      using GetNodeClientId = const char *(*)(WinJACKNexus::Node::INode * node);
      using CreateSettingsWidget = QWidget *(*)(WinJACKNexus::Node::INode * node, QWidget * parent);
      using DestroySettingsWidget = void (*)(WinJACKNexus::Node::INode *node, QWidget *widget);
      using CreateSettingsView = void *(*)(WinJACKNexus::Node::INode * node, void *parentHwnd);
      using DestroySettingsView = void (*)(WinJACKNexus::Node::INode *node, void *viewHwnd);

      if (QFileInfo(modulePath).fileName().compare(QStringLiteral("mod_classic_mixer.dll"),
                                                  Qt::CaseInsensitive) == 0)
      {
          const auto saved = QJsonDocument::fromJson(QByteArray::fromStdString(settingsJson)).object();
          const auto layout = saved.value("layoutId").toDouble(-1);
          if (layout != 8 && layout != 16 && layout != 24)
              return nullptr;
          const auto folder = QStringLiteral("classic_mixer_%1").arg(static_cast<int>(layout));
          modulePath = QFileInfo(modulePath).dir().absoluteFilePath(
              QStringLiteral("../%1/mod_%1.dll").arg(folder));
          modulePath = QDir::cleanPath(modulePath);
      }

      const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(modulePath.utf16()));
      if (library == nullptr)
          return nullptr;
      const auto apiVersion =
          reinterpret_cast<ApiVersion>(GetProcAddress(library, "wjn_get_node_api_version"));
      const auto createNode =
          reinterpret_cast<CreateNode>(GetProcAddress(library, "wjn_create_node_v4"));
      const auto destroyNode =
          reinterpret_cast<DestroyNode>(GetProcAddress(library, "wjn_destroy_node"));
      const auto setNodeSettingsJson = reinterpret_cast<SetNodeSettingsJson>(
          GetProcAddress(library, "wjn_set_node_settings_json"));
      const auto prepareNodeForCanvas = reinterpret_cast<PrepareNodeForCanvas>(
          GetProcAddress(library, "wjn_prepare_node_for_canvas"));
      const auto showNodeGui =
          reinterpret_cast<ShowNodeGui>(GetProcAddress(library, "wjn_show_node_gui"));
      const auto getNodeClientId =
          reinterpret_cast<GetNodeClientId>(GetProcAddress(library, "wjn_get_node_client_id"));
      const auto createSettingsWidget = reinterpret_cast<CreateSettingsWidget>(
          GetProcAddress(library, "wjn_create_settings_widget"));
      const auto destroySettingsWidget = reinterpret_cast<DestroySettingsWidget>(
          GetProcAddress(library, "wjn_destroy_settings_widget"));
      const auto createSettingsView =
          reinterpret_cast<CreateSettingsView>(GetProcAddress(library, "wjn_create_settings_view"));
      const auto destroySettingsView = reinterpret_cast<DestroySettingsView>(
          GetProcAddress(library, "wjn_destroy_settings_view"));
      WinJACKNexus::Node::ModuleContract contract;
      const auto contractError = WinJACKNexus::Node::inspectNodeModule(library, contract);
      if (apiVersion == nullptr || createNode == nullptr || destroyNode == nullptr ||
          contractError != WinJACKNexus::Node::ModuleError::none || index != 0 ||
          !WinJACKNexus::Node::supportsMode(contract, mode))
      {
          qWarning().noquote() << modulePath
                               << (contractError == WinJACKNexus::Node::ModuleError::none
                                       ? "Node mode is unsupported in this graph"
                                       : WinJACKNexus::Node::moduleErrorText(contractError));
          FreeLibrary(library);
          return nullptr;
      }
      bool nativeFx = false;
      if (!validNativeFxNodeModule(library, modulePath, &nativeFx))
      {
          FreeLibrary(library);
          return nullptr;
      }
      auto languageLease = moduleLanguageLease(modulePath, index);
      WinJACKNexus::UI::ThemeNamespaceRegistry::Lease themeLease;
      QString themeError;
      WinJACKNexus::UI::ThemeNamespaceRegistry::instance().registerNodeModule(
          [library](const char *name) -> QFunctionPointer {
              return reinterpret_cast<QFunctionPointer>(GetProcAddress(library, name));
          },
          modulePath, &themeLease, &themeError);
      if (!themeError.isEmpty())
          qWarning().noquote() << "Node theme descriptor:" << themeError;

      auto *node = WinJACKNexus::Node::createNodeForMode(library, mode);
      if (node == nullptr)
      {
          FreeLibrary(library);
          return nullptr;
      }
      if (setNodeSettingsJson != nullptr && !settingsJson.empty() &&
          setNodeSettingsJson(node, settingsJson.c_str()) == 0)
      {
          destroyNode(node);
          FreeLibrary(library);
          return nullptr;
      }
      if (prepareNodeForCanvas != nullptr && prepareNodeForCanvas(node) == 0)
      {
          destroyNode(node);
          FreeLibrary(library);
          return nullptr;
      }
      WjnNodePrepareV1 prepareContext{sizeof(prepareContext), maxFrames, sampleRate, nullptr};
      const bool prepared = mode == WJN_NODE_MODE_JACK
                                ? node->init()
                                : contract.processing->prepare(node, &prepareContext) != 0;
      if (!prepared)
      {
          if (mode == WJN_NODE_MODE_VIRT)
              contract.processing->stop(node);
          node->shutdown();
          if (node != nullptr)
              destroyNode(node);
          FreeLibrary(library);
          return nullptr;
      }
      if (nativeFx)
      {
          using ValidateLayout = int (*)(void *, uint32_t);
          using ClientName = const char *(*)(void *);
          const auto validateLayout = reinterpret_cast<ValidateLayout>(
              GetProcAddress(library, "wjn_fx_node_validate_layout_mode"));
          const auto clientName =
              reinterpret_cast<ClientName>(GetProcAddress(library, "wjn_fx_node_client_name"));
          const auto *name = clientName(node);
          if (validateLayout(node, mode) == 0 || name == nullptr || name[0] == '\0')
          {
              if (mode == WJN_NODE_MODE_VIRT)
                  contract.processing->stop(node);
              node->shutdown();
              destroyNode(node);
              FreeLibrary(library);
              return nullptr;
          }
      }
      const auto expectedClientId = mode != WJN_NODE_MODE_JACK || getNodeClientId == nullptr
                                        ? nullptr
                                        : getNodeClientId(node);
      const auto info = node->info();
      QString category;
      QVariantMap display = builtinNodeDisplay(modulePath, index, QString::fromUtf8(info.name));
      // A saved project may load a DLL outside the scanned node directory.
      // Its validated optional descriptor must still provide the display key.
      if (languageLease)
      {
          using Descriptor = const char *(*)(void);
          const auto descriptor = reinterpret_cast<Descriptor>(
              GetProcAddress(library, "wjn_get_node_i18n_descriptor_json"));
          const auto *json = descriptor ? descriptor() : nullptr;
          const auto nameKey = json ? QJsonDocument::fromJson(QByteArray(json))
                                          .object()
                                          .value("displayNameKey")
                                          .toObject()
                                    : QJsonObject{};
          if (!nameKey.isEmpty())
              display = {{"domain", nameKey.value("domain").toString().toUtf8()},
                         {"key", nameKey.value("key").toString().toUtf8()},
                         {"english", QByteArray(info.name)}};
      }
      for (const auto &module : scanNodeModules())
      {
          if (module.modulePath == modulePath && module.index == index)
          {
              category = module.category;
              display = module.display;
              break;
          }
      }
      return std::unique_ptr<LoadedModuleNode>(new LoadedModuleNode(
          library, node, destroyNode, createSettingsWidget, destroySettingsWidget,
          createSettingsView, destroySettingsView, showNodeGui, nativeFx,
          expectedClientId == nullptr ? std::string{} : std::string(expectedClientId), modulePath,
          index, category, QString::fromUtf8(info.name), std::move(themeLease),
          std::move(languageLease), std::move(display), mode, contract.processing));
  }

  ~LoadedModuleNode()
  {
      clearSettingsView();
      node_->destroyWindow(nullptr);
      if (isVirtual())
      {
          processing_->stop(node_);
      }
      node_->shutdown();
      destroyNode_(node_);
      themeLease_.reset();
      languageLease_.reset();
      FreeLibrary(library_);
  }

	LoadedModuleNode(const LoadedModuleNode&) = delete;
	LoadedModuleNode& operator=(const LoadedModuleNode&) = delete;

	const QString& modulePath() const noexcept
	{
		return modulePath_;
	}

		const QString& category() const noexcept
		{
			return category_;
		}

		const QString& name() const noexcept
		{
			return name_;
		}

		bool hasCustomGui() const noexcept
		{
            WinJACKNexus::Node::ModuleContract contract;
            return WinJACKNexus::Node::inspectNodeModule(library_, contract) ==
                       WinJACKNexus::Node::ModuleError::none &&
                   (contract.metadata.features & WJN_NODE_HAS_GUI);
        }
        bool isMeterProbe() const noexcept
        {
            const auto file = QFileInfo(modulePath_).fileName();
            return file.startsWith(QStringLiteral("mod_meter_"), Qt::CaseInsensitive) &&
                   file.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive);
        }
        QString displayName() const {return nodeDisplayText(display_,name_);}

	std::uint32_t index() const noexcept
	{
		return index_;
	}

	std::string settingsJson() const noexcept
	{
		try {
			if (getNodeSettingsJson_ == nullptr)
				return {};
			const auto* json = getNodeSettingsJson_(node_);
			return json == nullptr ? std::string {} : std::string(json);
		} catch (...) {
			return {};
		}
	}

	bool restoreSettingsJson(const std::string& json) const noexcept
	{
		if (json.empty()) return true;
		using SetNodeSettingsJson = int (*)(WinJACKNexus::Node::INode*, const char*);
		const auto restore = reinterpret_cast<SetNodeSettingsJson>(
			GetProcAddress(library_, "wjn_set_node_settings_json"));
		return restore != nullptr && restore(node_, json.c_str()) != 0;
	}

	void setClientId(std::string clientId)
	{
		clientId_ = std::move(clientId);
            auto &endpoints = WinJACKNexus::SignalSwitch::Service::instance().endpoints;
            if (auto endpoint = endpoints.find(node_); endpoint != endpoints.end())
                endpoint->graphClient = QString::fromStdString(clientId_);
	}

		const std::string& clientId() const noexcept
		{
            if (mode_ == WJN_NODE_MODE_JACK && getNodeClientId_ != nullptr)
            {
                try
                {
                    const auto *current = getNodeClientId_(node_);
                    if (current != nullptr && clientId_ != current)
						clientId_ = current;
                }
                catch (...)
                {
                }
            }
            return clientId_;
        }

        void setContextId(std::string contextId)
		{
			contextId_ = std::move(contextId);
            if (mode_ == WJN_NODE_MODE_VIRT && clientId_.empty())
            {
                clientId_ = contextId_ + "/node-" +
                            QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            }
            auto &endpoints = WinJACKNexus::SignalSwitch::Service::instance().endpoints;
            if (auto endpoint = endpoints.find(node_); endpoint != endpoints.end())
            {
                endpoint->context = QString::fromStdString(contextId_);
                endpoint->graphClient = QString::fromStdString(clientId());
            }
        }

        const std::string &contextId() const noexcept
        {
			return contextId_;
		}

        bool isVirtual() const noexcept
        {
            return mode_ == WJN_NODE_MODE_VIRT;
        }

        void synchronizeSignalSwitch(PatchbayCanvas *canvas) const
        {
            auto &service = WinJACKNexus::SignalSwitch::Service::instance();
            auto endpoint = service.endpoints.find(node_);
            if (endpoint == service.endpoints.end() || !canvas)
                return;
            endpoint->context = QString::fromStdString(contextId_);
            const auto &clients = canvas->snapshot().clients;
            const auto client = std::find_if(clients.begin(), clients.end(), [this](const auto &value) {
                return value.id == clientId();
            });
            if (endpoint->userNamed && client != clients.end() && !client->alias.empty())
                endpoint->name = QString::fromStdString(client->alias);
            const QPointer<PatchbayCanvas> owner(canvas);
            const auto identity = clientId();
            const auto switchId = endpoint->id;
            endpoint->rename = [owner, identity, switchId](const QString &value) {
                if (!owner || value.trimmed().isEmpty())
                    return false;
                const auto &clients = owner->snapshot().clients;
                if (std::none_of(clients.begin(), clients.end(), [&](const auto &client) {
                        return client.id == identity;
                    }))
                    return false;
                owner->setClientAlias(identity, value);
                auto &service = WinJACKNexus::SignalSwitch::Service::instance();
                if (auto *endpoint = service.find(switchId))
                    endpoint->name = value.trimmed();
                return true;
            };
        }

        WinJACKNexus::Patchbay::VirtualGraph::Processor virtualProcessor() const
        {
            return {clientId(), node_, processing_};
        }

        std::vector<WinJACKNexus::Patchbay::VirtualGraph::Port> virtualPorts() const
        {
            std::vector<WinJACKNexus::Patchbay::VirtualGraph::Port> result;
            if (!isVirtual())
                return result;
            const auto count = processing_->portCount(node_);
            if (count > 4096)
                return result;
            for (uint32_t index = 0; index < count; ++index)
            {
                WjnNodePortV1 port{};
                port.structSize = sizeof(port);
                if (!processing_->port(node_, index, &port) || port.reserved ||
                    port.structSize != sizeof(port))
                    return {};
                result.push_back({clientId() + ":port-" + std::to_string(port.id), clientId(),
                                  port.id, port.type, port.channels, port.direction, false});
            }
            return result;
        }

        std::string classicMixerRevision() const
        {
            using ReadRevision = uint64_t (*)(WinJACKNexus::Node::INode *);
            const auto read = reinterpret_cast<ReadRevision>(
                GetProcAddress(library_, "wjn_classic_mixer_state_revision"));
            return read ? std::to_string(read(node_)) : std::string{};
        }

        void appendVirtualSnapshot(WinJACKNexus::Patchbay::Core::Snapshot &snapshot) const
        {
            if (!isVirtual())
                return;
            WinJACKNexus::Patchbay::Core::ClientInfo client;
            client.id = clientId();
            client.name = name_.toStdString();
            const auto &switchEndpoints = WinJACKNexus::SignalSwitch::Service::instance().endpoints;
            if (const auto endpoint = switchEndpoints.constFind(node_); endpoint != switchEndpoints.cend())
                client.alias = WinJACKNexus::SignalSwitch::Service::instance().label(*endpoint).toStdString();
            if (isClassicMixerModule(modulePath_) &&
                getNodeClientId_ != nullptr)
            {
                const auto *displayName = getNodeClientId_(node_);
                if (displayName != nullptr && displayName[0] != '\0')
                    client.name = displayName;
            }
            snapshot.clients.push_back(std::move(client));
            const auto descriptions = virtualPorts();
            for (uint32_t index = 0; index < descriptions.size(); ++index)
            {
                const auto &description = descriptions[index];
                WjnNodePortV1 port{};
                port.structSize = sizeof(port);
                if (!processing_->port(node_, index, &port))
                    continue;
                WinJACKNexus::Patchbay::Core::PortInfo item;
                item.id = description.id;
                item.clientId = clientId();
                item.name = port.name ? port.name : "";
                item.kind = port.type == WJN_NODE_PORT_AUDIO
                                ? WinJACKNexus::Patchbay::Core::PortKind::audio
                                : WinJACKNexus::Patchbay::Core::PortKind::midi;
                item.direction = port.direction == WJN_NODE_PORT_INPUT
                                     ? WinJACKNexus::Patchbay::Core::Direction::input
                                     : WinJACKNexus::Patchbay::Core::Direction::output;
                item.ordinal = index;
                snapshot.ports.push_back(std::move(item));
            }
        }

        bool hasClient() const noexcept
        {
            return !clientId().empty();
        }

        bool ownsClient(const std::string &clientId) const noexcept
        {
            return this->clientId() == clientId;
        }

	bool showMixerView(int view) noexcept
	{
		using ShowView=int (*)(WinJACKNexus::Node::INode*,int);
		const auto show=reinterpret_cast<ShowView>(GetProcAddress(library_,"wjn_digital_mixer_show_view"));
		return show?show(node_,view)!=0:view==0&&showGui(nullptr);
	}

	bool showGui(QWidget* parent) noexcept
	{
        if (!hasCustomGui())
            return false;
        if (showNodeGui_ != nullptr)
            return showNodeGui_(node_) != 0;
        const auto parentWindow = parent == nullptr ? nullptr : reinterpret_cast<HWND>(static_cast<quintptr>(parent->winId()));
		const auto window = node_->createWindow(parentWindow);
		if (window == nullptr)
			return false;
		ShowWindow(window, SW_SHOW);
		SetForegroundWindow(window);
		return true;
	}
	QWidget* showSettings(QWidget* parent) noexcept
	{
		if (settingsWidget_ != nullptr || settingsContainer_ != nullptr)
			return settingsWidget_ != nullptr ? settingsWidget_.data() : settingsContainer_.data();
		if (nativeFx_) {
			try {
			using ClientName = const char* (*)(void*);
			using SetClientName = int (*)(void*, const char*);
			const auto clientName = reinterpret_cast<ClientName>(GetProcAddress(library_, "wjn_fx_node_client_name"));
			const auto setClientName = reinterpret_cast<SetClientName>(GetProcAddress(library_, "wjn_fx_node_set_client_name"));
			const auto* current = clientName(node_);
			if (current == nullptr) return nullptr;
			settingsWidget_ = new WinJACKNexus::UI::GateClientNameWidget(QString::fromUtf8(current),
				[this, setClientName](const QString& requested) {
					const auto utf8 = requested.toUtf8();
					return setClientName(node_, utf8.constData()) != 0;
				}, parent);
			return settingsWidget_;
			} catch (...) { return nullptr; }
		}

		if (createSettingsWidget_ != nullptr && destroySettingsWidget_ != nullptr) {
			settingsWidget_ = createSettingsWidget_(node_, parent);
			if (settingsWidget_ != nullptr) {
				settingsWidget_->show();
				return settingsWidget_;
			}
		}

		if (createSettingsView_ == nullptr || destroySettingsView_ == nullptr)
			return nullptr;

		const auto parentHandle = WinJACKNexus::UI::NativeWindowUtils::obtainNativeHandle(parent);
		const auto view = createSettingsView_(node_, reinterpret_cast<void*>(parentHandle));
		if (view == nullptr)
			return nullptr;
		settingsView_ = reinterpret_cast<HWND>(view);
		settingsContainer_ = WinJACKNexus::UI::NativeWindowUtils::embedNativeWindow(settingsView_, parent);
		if (settingsContainer_ == nullptr) {
			destroySettingsView_(node_, view);
			settingsView_ = nullptr;
			return nullptr;
		}
		ShowWindow(settingsView_, SW_SHOW);
		return settingsContainer_;
	}

	void clearSettingsView() noexcept
	{
		if (settingsWidget_ != nullptr) {
			if (nativeFx_)
				delete settingsWidget_.data();
			else if (destroySettingsWidget_ != nullptr)
				destroySettingsWidget_(node_, settingsWidget_.data());
			settingsWidget_ = nullptr;
		}
		if (settingsContainer_ != nullptr) {
			WinJACKNexus::UI::NativeWindowUtils::detachNativeWindow(settingsContainer_);
			delete settingsContainer_.data();
			settingsContainer_ = nullptr;
		}
		if (settingsView_ != nullptr && destroySettingsView_ != nullptr) {
			destroySettingsView_(node_, reinterpret_cast<void*>(settingsView_));
			settingsView_ = nullptr;
		}
	}

private:
	using DestroyNode = void (*)(WinJACKNexus::Node::INode* node);
	using GetNodeSettingsJson = const char* (*)(WinJACKNexus::Node::INode* node);
	using GetNodeClientId = const char* (*)(WinJACKNexus::Node::INode* node);
		using CreateSettingsWidget = QWidget* (*)(WinJACKNexus::Node::INode* node, QWidget* parent);
		using DestroySettingsWidget = void (*)(WinJACKNexus::Node::INode* node, QWidget* widget);
		using CreateSettingsView = void* (*)(WinJACKNexus::Node::INode* node, void* parentHwnd);
		using DestroySettingsView = void (*)(WinJACKNexus::Node::INode* node, void* viewHwnd);
		using ShowNodeGui = int (*)(WinJACKNexus::Node::INode* node);

        LoadedModuleNode(HMODULE library, WinJACKNexus::Node::INode *node, DestroyNode destroyNode,
                         CreateSettingsWidget createSettingsWidget,
                         DestroySettingsWidget destroySettingsWidget,
                         CreateSettingsView createSettingsView,
                         DestroySettingsView destroySettingsView, ShowNodeGui showNodeGui,
                         bool nativeFx, std::string clientId, const QString &modulePath,
                         std::uint32_t index, const QString &category, const QString &name,
                         WinJACKNexus::UI::ThemeNamespaceRegistry::Lease themeLease,
                         std::shared_ptr<WinJACKNexus::UI::I18nDomainLease> languageLease,
                         QVariantMap display, uint32_t mode,
                         const WjnNodeVirtualApiV1 *processing) noexcept
            : mode_(mode), processing_(processing), library_(library), node_(node),
              destroyNode_(destroyNode), createSettingsWidget_(createSettingsWidget),
              destroySettingsWidget_(destroySettingsWidget),
              createSettingsView_(createSettingsView), destroySettingsView_(destroySettingsView),
              showNodeGui_(showNodeGui), nativeFx_(nativeFx), clientId_(std::move(clientId)),
              getNodeClientId_(reinterpret_cast<GetNodeClientId>(
                  GetProcAddress(library, "wjn_get_node_client_id"))),
              getNodeSettingsJson_(reinterpret_cast<GetNodeSettingsJson>(
                  GetProcAddress(library, "wjn_get_node_settings_json"))),
              modulePath_(modulePath), index_(index), category_(category), name_(name),
              themeLease_(std::move(themeLease)), languageLease_(std::move(languageLease)),
              display_(std::move(display))
        {
        }

        uint32_t mode_ = WJN_NODE_MODE_JACK;
        const WjnNodeVirtualApiV1 *processing_{};
        HMODULE library_ = nullptr;
        WinJACKNexus::Node::INode *node_ = nullptr;
        DestroyNode destroyNode_ = nullptr;
        CreateSettingsWidget createSettingsWidget_ = nullptr;
        DestroySettingsWidget destroySettingsWidget_ = nullptr;
		CreateSettingsView createSettingsView_ = nullptr;
		DestroySettingsView destroySettingsView_ = nullptr;
		ShowNodeGui showNodeGui_ = nullptr;
		bool nativeFx_ = false;
		GetNodeSettingsJson getNodeSettingsJson_ = nullptr;
	QString modulePath_;
	std::uint32_t index_ = 0;
	QString category_;
	QString name_;
	WinJACKNexus::UI::ThemeNamespaceRegistry::Lease themeLease_;
	std::shared_ptr<WinJACKNexus::UI::I18nDomainLease> languageLease_;
	QVariantMap display_;
	mutable std::string clientId_;
	GetNodeClientId getNodeClientId_ = nullptr;
	std::string contextId_ = "main";
		HWND settingsView_ = nullptr;
		QPointer<QWidget> settingsWidget_;
		QPointer<QWidget> settingsContainer_;
};

void ensureSystemClient(WinJACKNexus::Patchbay::Core::Snapshot& snapshot)
{
	const auto system = std::find_if(snapshot.clients.begin(), snapshot.clients.end(), [](const auto& client) {
		return client.id == "system";
	});
	if (system == snapshot.clients.end())
		snapshot.clients.push_back({ "system", "system", {}, "computer" });
	else
		system->alive = true;
}

using Snapshot = WinJACKNexus::Patchbay::Core::Snapshot;
using ClientRebindings = std::vector<std::pair<std::string, std::string>>;

bool hasSameObservedJackGraph(const Snapshot& left, const Snapshot& right)
{
	auto clientSignature = [](const Snapshot& snapshot) {
		std::vector<std::string> signature;
		signature.reserve(snapshot.clients.size());
		for (const auto& client : snapshot.clients)
			signature.push_back(client.id + '\0' + client.name);
		std::sort(signature.begin(), signature.end());
		return signature;
	};
	auto portSignature = [](const Snapshot& snapshot) {
		std::vector<std::string> signature;
		signature.reserve(snapshot.ports.size());
		for (const auto& port : snapshot.ports) {
			signature.push_back(port.id + '\0' + port.clientId + '\0' + port.name + '\0'
				+ std::to_string(static_cast<unsigned>(port.kind)) + '\0'
				+ std::to_string(static_cast<unsigned>(port.direction)));
		}
		std::sort(signature.begin(), signature.end());
		return signature;
	};
	auto connectionSignature = [](const Snapshot& snapshot) {
		std::vector<std::string> signature;
		signature.reserve(snapshot.connections.size());
		for (const auto& connection : snapshot.connections)
			signature.push_back(connection.sourcePortId + '\0' + connection.destinationPortId);
		std::sort(signature.begin(), signature.end());
		return signature;
	};
	return clientSignature(left) == clientSignature(right)
		&& portSignature(left) == portSignature(right)
		&& connectionSignature(left) == connectionSignature(right);
}

bool isBuiltInClient(const std::string& clientId)
{
	return clientId == "system" || clientId == "WinJACKNexus-Patchbay";
}

std::vector<std::string> clientPortSignature(const Snapshot& snapshot, const std::string& clientId)
{
	std::vector<std::string> signature;
	for (const auto& port : snapshot.ports) {
		if (port.clientId != clientId)
			continue;
		signature.push_back(std::to_string(static_cast<unsigned>(port.kind)) + ':'
			+ std::to_string(static_cast<unsigned>(port.direction)) + ':' + port.name);
	}
	std::sort(signature.begin(), signature.end());
	return signature;
}

ClientRebindings inferClientRebindings(const Snapshot& previous,
	                                      const Snapshot& current,
	                                      const ClientRebindings& knownRebindings)
{
	ClientRebindings rebindings = knownRebindings;
	std::set<std::string> reboundOldClients;
	std::set<std::string> reboundNewClients;
	for (const auto& [oldClientId, newClientId] : rebindings) {
		reboundOldClients.insert(oldClientId);
		reboundNewClients.insert(newClientId);
	}

	for (const auto& oldClient : previous.clients) {
		if (isBuiltInClient(oldClient.id) || reboundOldClients.contains(oldClient.id))
			continue;
		const auto currentClient = std::find_if(current.clients.cbegin(), current.clients.cend(), [&oldClient](const auto& client) {
			return client.id == oldClient.id;
		});
		if (currentClient != current.clients.cend())
			continue;

		const auto oldSignature = clientPortSignature(previous, oldClient.id);
		if (oldSignature.empty())
			continue;

		std::vector<std::string> candidates;
		for (const auto& newClient : current.clients) {
			if (!newClient.alive || isBuiltInClient(newClient.id)
				|| reboundNewClients.contains(newClient.id)
				|| std::any_of(previous.clients.cbegin(), previous.clients.cend(), [&newClient](const auto& client) {
					return client.id == newClient.id;
				}))
				continue;
			if (clientPortSignature(current, newClient.id) == oldSignature)
				candidates.push_back(newClient.id);
		}
		if (candidates.size() != 1)
			continue;

		rebindings.emplace_back(oldClient.id, candidates.front());
		reboundOldClients.insert(oldClient.id);
		reboundNewClients.insert(candidates.front());
	}
	return rebindings;
}

std::string remapPortId(const std::string& portId, const ClientRebindings& rebindings)
{
	for (const auto& [oldClientId, newClientId] : rebindings) {
		const auto prefix = oldClientId + ':';
		if (portId.starts_with(prefix))
			return newClientId + portId.substr(oldClientId.size());
	}
	return portId;
}

void mergeReboundClientMetadata(const Snapshot& previous,
	                              Snapshot& current,
	                              const ClientRebindings& rebindings)
{
	for (const auto& [oldClientId, newClientId] : rebindings) {
		const auto oldClient = std::find_if(previous.clients.cbegin(), previous.clients.cend(), [&oldClientId](const auto& client) {
			return client.id == oldClientId;
		});
		const auto newClient = std::find_if(current.clients.begin(), current.clients.end(), [&newClientId](const auto& client) {
			return client.id == newClientId;
		});
		if (oldClient == previous.clients.cend() || newClient == current.clients.end())
			continue;
		newClient->alias = oldClient->alias;
		newClient->iconName = oldClient->iconName;
		newClient->uri = oldClient->uri;
		newClient->x = oldClient->x;
		newClient->y = oldClient->y;
		newClient->split = oldClient->split;
	}
}

void mergeReboundConnections(const Snapshot& previous,
	                            Snapshot& current,
	                            const ClientRebindings& rebindings)
{
	for (const auto& connection : previous.connections) {
		const auto sourcePortId = remapPortId(connection.sourcePortId, rebindings);
		const auto destinationPortId = remapPortId(connection.destinationPortId, rebindings);
		const auto exists = std::any_of(current.connections.cbegin(), current.connections.cend(), [&sourcePortId, &destinationPortId](const auto& currentConnection) {
			return currentConnection.sourcePortId == sourcePortId
				&& currentConnection.destinationPortId == destinationPortId;
		});
		if (!exists)
			current.connections.push_back({ sourcePortId, destinationPortId, connection.alive });
	}
}

void preserveLoadedModuleClients(const Snapshot& previous,
	                               Snapshot& current,
	                               const std::vector<std::unique_ptr<LoadedModuleNode>>& loadedModules)
{
	for (const auto& node : loadedModules) {
		if (!node->hasClient())
			continue;
		const auto currentClient = std::find_if(current.clients.cbegin(), current.clients.cend(), [&node](const auto& client) {
			return client.id == node->clientId();
		});
		if (currentClient != current.clients.cend())
			continue;
		const auto previousClient = std::find_if(previous.clients.cbegin(), previous.clients.cend(), [&node](const auto& client) {
			return client.id == node->clientId();
		});
		if (previousClient == previous.clients.cend())
			continue;

		current.clients.push_back(*previousClient);
		for (const auto& port : previous.ports) {
			if (port.alive && port.clientId == node->clientId())
				current.ports.push_back(port);
		}
	}
}

void preserveOfflineJackRecords(const Snapshot& previous,
	                              Snapshot& current,
	                              const ClientRebindings& rebindings)
{
	for (const auto& oldClient : previous.clients) {
		if (isBuiltInClient(oldClient.id)
			|| std::any_of(rebindings.cbegin(), rebindings.cend(), [&oldClient](const auto& rebinding) {
				return rebinding.first == oldClient.id;
			})
			|| std::any_of(current.clients.cbegin(), current.clients.cend(), [&oldClient](const auto& client) {
				return client.id == oldClient.id;
			}))
			continue;

		WinJACKNexus::Patchbay::Core::ClientInfo offlineClient = oldClient;
		offlineClient.alive = false;
		current.clients.push_back(std::move(offlineClient));
		for (const auto& oldPort : previous.ports) {
			if (oldPort.clientId != oldClient.id)
				continue;
			WinJACKNexus::Patchbay::Core::PortInfo offlinePort = oldPort;
			offlinePort.alive = false;
			current.ports.push_back(std::move(offlinePort));
		}
	}
}

class PatchbayNode;
PatchbayNode* livePatchbayNode = nullptr;

bool isDigitalMixerModule(const QString& path)
{
    return QFileInfo(path).fileName().compare(QStringLiteral("mod_digital_mixer.dll"), Qt::CaseInsensitive) == 0;
}

class PatchbayNode final : public WinJACKNexus::Node::INode {
public:
	explicit PatchbayNode(bool offline=false):offline_(offline) {}
	bool hasLoadedDigitalMixer() const noexcept
	{
		return std::any_of(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [](const auto& node) {
			return isDigitalMixerModule(node->modulePath()) && node->index() == 0;
		});
	}

	bool removeLoadedDigitalMixer() noexcept
	{
		return std::erase_if(loadedModuleNodes_, [](const auto& node) {
			return isDigitalMixerModule(node->modulePath()) && node->index() == 0;
		}) != 0;
	}

	int showLoadedDigitalMixer(int view = 0) noexcept
	{
		const auto found = std::find_if(loadedModuleNodes_.begin(), loadedModuleNodes_.end(), [](const auto& node) {
			return isDigitalMixerModule(node->modulePath()) && node->index() == 0;
		});
		if (found == loadedModuleNodes_.end()) return 0;
		return (*found)->showMixerView(view) ? 1 : -1;
	}

	WinJACKNexus::Node::NodeInfo info() const noexcept override
	{
		return {
			"Patchbay",
			"Patchbay",
			"0.1.0",
			"WinJACKNexus",
			"JACK multi-client patchbay module."
		};
	}

	bool init() noexcept override
	{
		initialized_ = true;
		livePatchbayNode = this;
        QString switchError;
        if (!WinJACKNexus::SignalSwitch::Service::instance().installUI(&switchError))
            qWarning().noquote() << "Signal Switch:" << switchError;
		return true;
	}

	void shutdown() noexcept override
	{
		if (livePatchbayNode == this) livePatchbayNode = nullptr;
		destroyWindow(nullptr);
		WinJACKNexus::UI::TrayController::clearNodeGuiEntries();
        subGraphPorts_->clear();
        loadedModuleNodes_.clear();
        WinJACKNexus::SignalSwitch::Service::instance().shutdownUI();
        observedJackClients_.clear();
        graphRefreshPending_.store(false, std::memory_order_release);
        graphOrderCallbackRegistered_ = false;
		jackObserver_.close();
		initialized_ = false;
	}

	HWND createWindow(HWND) noexcept override
	{
		if (!initialized_)
			return nullptr;

		if (window_ == nullptr) {
			window_ = new PatchbayWindow;
			window_->setAttribute(Qt::WA_DeleteOnClose, false);
			bindPatchText(window_,"windowTitle","window.title","WinJACKNexus - Patchbay");
			window_->resize(1440, 860);
			PatchbayCanvas::applyTheme(false);

			auto connectJackPort = [this](const std::string& source, const std::string& destination) {
				return jackObserver_.isOpen()
					&& WinJACKNexus::Audio::JackApi::instance().connect(jackObserver_.nativeHandle(), source.c_str(), destination.c_str()) == 0;
			};
			auto disconnectJackPort = [this](const std::string& source, const std::string& destination) {
				if (jackObserver_.isOpen())
					WinJACKNexus::Audio::JackApi::instance().disconnect(jackObserver_.nativeHandle(), source.c_str(), destination.c_str());
			};
			auto* canvas = new PatchbayCanvas(window_);
            WinJACKNexus::SignalSwitch::Service::instance().edited = [owner = QPointer<PatchbayCanvas>(canvas)] {
                if (owner)
                    owner->markProjectDirty();
            };
			canvas->setConnectionConnectHandler(connectJackPort);
			canvas->setConnectionDisconnectHandler([this](const std::string& source, const std::string& destination) {
				return jackObserver_.isOpen()
					&& WinJACKNexus::Audio::JackApi::instance().disconnect(jackObserver_.nativeHandle(), source.c_str(), destination.c_str()) == 0;
			});
			auto* canvasTabs = new WinJACKNexus::UI::Controls::TabField(window_);
			canvasTabs->setTabsClosable(true);
			canvasTabs->addTab(canvas, patchText("graph.main","Main graph"));
			if (auto* manager=WinJACKNexus::UI::I18nManager::installed()) manager->bind(canvasTabs,[canvasTabs,canvas] { canvasTabs->setTabText(canvasTabs->indexOf(canvas),patchText("graph.main","Main graph")); });
			window_->setCentralWidget(canvasTabs);
			window_->setDockNestingEnabled(true);

			auto* nodeDock = new WinJACKNexus::UI::Controls::DockPanel(
				patchText("library.title","Nodes"), Qt::LeftDockWidgetArea, window_);
			bindPatchText(nodeDock,"windowTitle","library.title","Nodes");
			nodeDock->setObjectName(QStringLiteral("patchbay.nodeLibrary"));
			auto* nodePanel = new QWidget(nodeDock);
			auto* nodeLayout = new QVBoxLayout(nodePanel);
			// The dock's 24 px resize grip overlays its canvas-facing edge.
			nodeLayout->setContentsMargins(8, 8, 32, 8);
			auto* nodeFilter = new WinJACKNexus::UI::Controls::SearchField(nodePanel);
			nodeFilter->setObjectName(QStringLiteral("patchbay.nodeFilter"));
			bindPatchText(nodeFilter,"placeholderText","library.filter","Filter nodes");
			nodeFilter->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			auto* nodeTree = new WinJACKNexus::UI::Controls::DragTreeWidget(nodePanel);
			nodeTree->setObjectName(QStringLiteral("patchbay.nodeTree"));
			nodeTree->setMimeDataFactory(moduleNodeMimeData);
			nodeTree->setHeaderHidden(true);
			nodeTree->setIndentation(18);
			nodeTree->setAnimated(true);
			nodeLayout->addWidget(nodeFilter);
			nodeLayout->addWidget(nodeTree, 1);
			nodeDock->setWidget(nodePanel);
			window_->addDockWidget(Qt::LeftDockWidgetArea, nodeDock);
			auto loadedModuleClientIds = [this] {
				std::set<std::string> clientIds;
				for (const auto& node : loadedModuleNodes_)
					if (node->hasClient())
						clientIds.insert(node->clientId());
				return clientIds;
			};
			nodeTree->setProperty("filterQuery", nodeFilter->text());
			auto subGraphCanvases = std::make_shared<std::map<std::string, QPointer<PatchbayCanvas>>>();
            auto subGraphPorts = subGraphPorts_;
            auto restoredSubGraphs =
                std::make_shared<std::map<std::string, PatchbaySubGraphProject>>();
            auto refreshNodeLibrary = std::make_shared<std::function<void()>>();
            WinJACKNexus::SignalSwitch::Service::instance().synchronize =
                [this, canvas, subGraphCanvases] {
                    for (const auto &node : loadedModuleNodes_)
                    {
                        auto *owner = canvas;
                        const auto child = subGraphCanvases->find(node->contextId());
                        if (child != subGraphCanvases->end() && child->second)
                            owner = child->second;
                        node->synchronizeSignalSwitch(owner);
                    }
                };
            *refreshNodeLibrary = [canvasTabs, nodeTree] {
				auto* activeCanvas = dynamic_cast<PatchbayCanvas*>(canvasTabs->currentWidget());
				if (activeCanvas == nullptr)
					return;
				populateNodeLibrary(nodeTree, nodeTree->property("filterQuery").toString(),
                                   activeCanvas->sceneContext().isSubGraph);
			};
			auto refreshTrayNodeGuiEntries = std::make_shared<std::function<void()>>();
			*refreshTrayNodeGuiEntries = [this] {
				std::vector<WinJACKNexus::UI::TrayNodeGuiEntry> entries;
				QHash<QString,QString> displayNames;
				bool hasMeterProbe = false;
				for (const auto& node : loadedModuleNodes_) {
					if (!node->hasCustomGui())
						continue;
					if (node->isMeterProbe()) {
						hasMeterProbe = true;
						continue;
					}
					auto* nodePointer = node.get();
					displayNames.insert(QString::number(reinterpret_cast<quintptr>(nodePointer),16),nodePointer->displayName());
					entries.push_back({
						QString::number(reinterpret_cast<quintptr>(nodePointer), 16),
						nodePointer->category(),
						clientBaseName(nodePointer->clientId()),
						nodePointer->name(),
						[nodePointer] { return nodePointer->showGui(nullptr); }
					});
				}
				if (hasMeterProbe) {
					entries.push_back({
						QStringLiteral("tool:meterbridge"),
						QStringLiteral("Meter"),
						patchText("action.meterbridge","Meter bridge"),
						patchText("action.meterbridge","Meter bridge"),
						[] {
							const auto module = GetModuleHandleW(L"mod_meterbrigde.dll");
							using ShowMeterbridge = int (*)(const char*);
							const auto show = module == nullptr ? nullptr
								: reinterpret_cast<ShowMeterbridge>(GetProcAddress(module, "wjn_meterbridge_show_for_instance"));
							return show != nullptr && show(nullptr) != 0;
						}
					});
				}
				WinJACKNexus::UI::TrayController::setNodeGuiEntries(std::move(entries),displayNames);
			};
			(*refreshNodeLibrary)();
            if (auto* manager=WinJACKNexus::UI::I18nManager::installed()) manager->bind(window_,[refreshTrayNodeGuiEntries] {(*refreshTrayNodeGuiEntries)();});
            if (auto* manager=WinJACKNexus::UI::I18nManager::installed()) manager->bind(nodeTree,[nodeTree] {
                const QSignalBlocker blocked(nodeTree);
                const auto expanded=captureExpandedCategories(nodeTree);
                const auto vertical=nodeTree->verticalScrollBar()->value();
                const auto horizontal=nodeTree->horizontalScrollBar()->value();
                const auto update=[&](auto&& self,QTreeWidgetItem* item)->void {
                    if (item->data(0,libraryIdentityRole).isValid()) {
                        const auto full=categoryDisplayName(libraryItemIdentity(item));
                        item->setText(0,full); item->setToolTip(0,full);
                    }
                    const auto display=item->data(0,Qt::UserRole+21).toMap();
                    if (!display.isEmpty()) {
                        const auto full=nodeDisplayText(display,{}); item->setText(0,full); item->setToolTip(0,nodeDisplayTooltip(display,{}));
                    }
                    if (item->data(0, nodeCapabilitiesRole).isValid())
                    {
                        const auto base = display.isEmpty()
                                              ? item->data(0, nodeTooltipBaseRole).toString()
                                              : nodeDisplayTooltip(display, {});
                        item->setToolTip(
                            0, capabilityTooltip(base, item->data(0, nodeCapabilitiesRole).toUInt(),
                                                 item->data(0, nodeFeaturesRole).toUInt()));
                    }
                    for (int i=0;i<item->childCount();++i) self(self,item->child(i));
                };
                for (int i=0;i<nodeTree->topLevelItemCount();++i) update(update,nodeTree->topLevelItem(i));
                applyNodeLibraryFilter(nodeTree,nodeTree->property("filterQuery").toString());
                restoreExpandedCategories(nodeTree,expanded);
                nodeTree->verticalScrollBar()->setValue(vertical);
                nodeTree->horizontalScrollBar()->setValue(horizontal);
            });
            QObject::connect(canvasTabs, &QTabWidget::currentChanged, window_, [canvasTabs, refreshNodeLibrary](int) {
				QTimer::singleShot(0, canvasTabs, [refreshNodeLibrary] {
					(*refreshNodeLibrary)();
				});
			});
			auto updateSubGraphTabTitle = std::make_shared<std::function<void(PatchbayCanvas*, const std::string&, const QString&)>>();
			*updateSubGraphTabTitle = [canvasTabs, subGraphCanvases](PatchbayCanvas* parentCanvas, const std::string& subGraphId, const QString& name) {
				if (parentCanvas == nullptr)
					return;
				const auto contextId = parentCanvas->sceneContext().id + "/" + subGraphId;
				const auto child = subGraphCanvases->find(contextId);
				if (child == subGraphCanvases->end() || child->second.isNull())
					return;
				const auto tabIndex = canvasTabs->indexOf(child->second.data());
				if (tabIndex >= 0)
					canvasTabs->setTabText(tabIndex, name.isEmpty() ? patchText("graph.defaultName","Subgraph") : name);
			};
			auto removeSubGraph = std::make_shared<std::function<bool(PatchbayCanvas*, const std::string&)>>();
            *removeSubGraph = [this, canvasTabs, subGraphCanvases, subGraphPorts](
                                  PatchbayCanvas *parentCanvas, const std::string &subGraphId) {
                if (parentCanvas == nullptr)
                    return false;
                const auto contextId = parentCanvas->sceneContext().id + "/" + subGraphId;
				const auto rootContextId = rootSubGraphContext(contextId);
				const auto belongsToSubTree = [&contextId](const std::string& value) {
					return value == contextId || value.starts_with(contextId + "/");
				};
				std::unordered_set<std::string> removedPortIds;
                for (const auto &[id, portSet] : *subGraphPorts)
                {
                    if (!belongsToSubTree(id) || portSet == nullptr)
                        continue;
					for (const auto& interfaceNode : portSet->interfaces)
						removedPortIds.insert(interfaceNode.portId);
                }
                const auto root = subGraphPorts->find(rootContextId);
                if (root != subGraphPorts->end() && root->second)
                {
                    auto &portSet = *root->second;
                    const bool active = portSet.client.isActive();
                    if (active && !portSet.pause())
                        return false;
                    const auto previousRoutes = portSet.routes;
                    const auto previousInterfaces = portSet.interfaces;
                    const auto previousPorts = portSet.modulePorts;
                    const auto previousProcessors = portSet.processors;
                    std::unordered_set<std::string> removedNodes;
                    for (const auto &node : loadedModuleNodes_)
                    {
                        if (belongsToSubTree(node->contextId()))
                            removedNodes.insert(node->clientId());
                    }
                    for (const auto &port : portSet.modulePorts)
                    {
                        if (removedNodes.contains(port.owner))
                            removedPortIds.insert(port.id);
                    }
                    std::erase_if(portSet.routes, [&](const auto &route) {
                        return removedPortIds.contains(route.sourcePortId) ||
                               removedPortIds.contains(route.destinationPortId);
                    });
                    std::erase_if(portSet.interfaces, [&](const auto &port) {
                        return removedPortIds.contains(port.portId);
                    });
                    std::erase_if(portSet.modulePorts, [&](const auto &port) {
                        return removedNodes.contains(port.owner);
                    });
                    std::erase_if(portSet.processors, [&](const auto &processor) {
                        return removedNodes.contains(processor.id);
                    });
                    if (!portSet.rebuild() ||
                        (contextId != rootContextId && !portSet.resume(active)))
                    {
                        portSet.routes = previousRoutes;
                        portSet.interfaces = previousInterfaces;
                        portSet.modulePorts = previousPorts;
                        portSet.processors = previousProcessors;
                        portSet.rebuild();
                        portSet.resume(active);
                        return false;
                    }
                    if (contextId == rootContextId)
                        portSet.client.close();
                }
                std::erase_if(loadedModuleNodes_, [&](const auto &node) {
                    return belongsToSubTree(node->contextId());
                });
                std::erase_if(unloadedModuleRecords_,
                              [&](const auto &node) { return belongsToSubTree(node.contextId); });
                for (auto iterator = subGraphCanvases->begin();
                     iterator != subGraphCanvases->end();)
                {
                    if (!belongsToSubTree(iterator->first))
                    {
                        ++iterator;
                        continue;
                    }
                    if (!iterator->second.isNull())
                    {
                        const auto tabIndex = canvasTabs->indexOf(iterator->second.data());
                        if (tabIndex >= 0)
							canvasTabs->removeTab(tabIndex);
						iterator->second->deleteLater();
                    }
                    iterator = subGraphCanvases->erase(iterator);
                }
                for (auto iterator = subGraphPorts->begin(); iterator != subGraphPorts->end();)
                {
                    if (belongsToSubTree(iterator->first))
                        iterator = subGraphPorts->erase(iterator);
					else
						++iterator;
                }
                return true;
            };
            auto clearSubGraphs = [canvas, removeSubGraph] {
                const auto nodes = canvas->subGraphNodes();
                for (const auto& node : nodes)
					(*removeSubGraph)(canvas, node.id);
				canvas->restoreSubGraphNodes({});
            };
            using CreateModuleNode =
                std::function<bool(PatchbayCanvas *, const QString &, std::uint32_t,
                                   const QPointF &, const QString &)>;
            auto createModuleNode = std::make_shared<CreateModuleNode>();
			auto configureCanvasInteractions = std::make_shared<std::function<void(PatchbayCanvas*)>>();
			auto openSubGraph = std::make_shared<std::function<void(PatchbayCanvas*, const std::string&)>>();
			const std::weak_ptr<std::function<void(PatchbayCanvas*, const std::string&)>> openSubGraphWeak = openSubGraph;
			*openSubGraph = [canvasTabs, subGraphCanvases, subGraphPorts, restoredSubGraphs, refreshNodeLibrary, updateSubGraphTabTitle, removeSubGraph, openSubGraphWeak, connectJackPort, disconnectJackPort, createModuleNode, configureCanvasInteractions, this](PatchbayCanvas* parentCanvas, const std::string& subGraphId) {
				if (parentCanvas == nullptr || !parentCanvas->isSubGraphNode(subGraphId))
					return;
				const auto contextId = parentCanvas->sceneContext().id + "/" + subGraphId;
				const bool createDefaultInterfaces = (!subGraphPorts->contains(contextId) || (*subGraphPorts)[contextId] == nullptr)
					&& !restoredSubGraphs->contains(contextId);
				auto existing = subGraphCanvases->find(contextId);
				if (existing != subGraphCanvases->end()) {
					if (!existing->second.isNull()) {
						const auto tabIndex = canvasTabs->indexOf(existing->second.data());
						if (tabIndex >= 0) {
							canvasTabs->setCurrentIndex(tabIndex);
						} else {
							const auto title = parentCanvas->subGraphName(subGraphId);
							canvasTabs->setCurrentIndex(canvasTabs->addTab(existing->second.data(), title.isEmpty() ? patchText("graph.defaultName","Subgraph") : title));
						}
						return;
					}
					subGraphCanvases->erase(existing);
				}

				PatchbaySceneContext context;
				context.id = contextId;
				context.title = parentCanvas->subGraphName(subGraphId);
				context.isSubGraph = true;
				auto* childCanvas = new PatchbayCanvas(context, canvasTabs);
				QPointer<PatchbayCanvas> childCanvasGuard(childCanvas);
				QPointer<PatchbayCanvas> parentCanvasGuard(parentCanvas);
				const bool isTopLevel = isTopLevelSubGraphContext(contextId);
				const auto rootContextId = rootSubGraphContext(contextId);
                const bool hadBackend = subGraphPorts->contains(contextId);
                auto &backend = (*subGraphPorts)[contextId];
                if (!backend)
                    backend = std::make_shared<SubGraphPortSet>();
                backend->offline = offline_;
                if (isTopLevel &&
                    !backend->openBoundary("WinJACKNexus-SubGraph-" + jackSafeToken(contextId)))
                {
                    qWarning() << "Subgraph JACK boundary could not be created";
                    subGraphPorts->erase(contextId);
                    delete childCanvas;
                    auto retained = parentCanvas->subGraphNodes();
                    std::erase_if(retained, [&subGraphId](const auto &graph) {
                        return graph.id == subGraphId;
                    });
                    parentCanvas->restoreSubGraphNodes(std::move(retained));
                    return;
                }

                childCanvas->setConnectionConnectHandler([subGraphPorts, rootContextId](
                                                             const std::string &source,
                                                             const std::string &destination) {
                    const auto portSet = subGraphPorts->find(rootContextId);
					if (portSet != subGraphPorts->end() && portSet->second != nullptr
						&& portSet->second->connect(source, destination))
						return true;
					return false;
                });
                childCanvas->setConnectionDisconnectHandler([subGraphPorts, rootContextId](const std::string& source, const std::string& destination) {
					const auto portSet = subGraphPorts->find(rootContextId);
					return portSet != subGraphPorts->end() && portSet->second != nullptr
						&& portSet->second->disconnect(source, destination);
				});
                childCanvas->setInterfaceNodeAddedHandler(
                    [this, subGraphPorts, contextId, rootContextId, isTopLevel, parentCanvasGuard,
                     subGraphId](PatchbayInterfaceInfo &interfaceNode) {
                        const auto definition = std::find_if(
                            subGraphInterfaceDefinitions().cbegin(),
                            subGraphInterfaceDefinitions().cend(),
                            [&interfaceNode](const auto &value) {
                                return interfaceNode.type == QString::fromLatin1(value.type);
                            });
                        if (definition == subGraphInterfaceDefinitions().cend())
                            return false;
                        auto &portSet = (*subGraphPorts)[contextId];
                        if (!portSet)
                            portSet = std::make_shared<SubGraphPortSet>();
                        portSet->offline = offline_;
                        auto &rootPortSet = (*subGraphPorts)[rootContextId];
                        if (!rootPortSet)
                            rootPortSet = std::make_shared<SubGraphPortSet>();
                        rootPortSet->offline = offline_;
                        if (isTopLevel && !offline_ && !portSet->client.isOpen())
                        {
                            const auto clientName =
                                "WinJACKNexus-SubGraph-" + jackSafeToken(contextId);
                            if (!portSet->client.open(
                                    {clientName.c_str(), false, false, nullptr}) ||
                                portSet->client.setProcessCallback(&SubGraphPortSet::process,
                                                                   portSet.get()) != 0)
                            {
                                portSet->client.close();
                                return false;
                            }
                            portSet->sampleRate = portSet->client.sampleRate();
                            portSet->requestedRate.store(uint32_t(portSet->sampleRate));
                            portSet->maxFrames = portSet->client.bufferSize();
                            portSet->requestedFrames.store(portSet->maxFrames);
                            portSet->client.setSampleRateCallback(&SubGraphPortSet::rateChanged,
                                                                  portSet.get());
                            portSet->client.setBufferSizeCallback(&SubGraphPortSet::sizeChanged,
                                                                  portSet.get());
                        }
                        const bool active = rootPortSet->client.isActive();
                        if (active && !rootPortSet->pause())
                            return false;
                        interfaceNode.portId = interfaceNode.id + ":port";
                        interfaceNode.externalPortId = interfaceNode.portId;
                        if (isTopLevel && !offline_)
                        {
                            portSet->ports.emplace_back();
                            auto &port = portSet->ports.back();
                            port.kind = interfaceNode.kind;
                            port.isInput = definition->flags == JackPortIsInput;
                            port.id = interfaceNode.portId;
                            const auto name = interfaceNode.name.toUtf8();
                            if (!port.port.registerPort(portSet->client, name.constData(),
                                                        definition->jackType, definition->flags))
                            {
                                portSet->ports.pop_back();
                                rootPortSet->resume(active);
                                return false;
                            }
                            const auto *external = port.port.name();
                            if (!external || !*external)
                            {
                                portSet->ports.pop_back();
                                rootPortSet->resume(active);
                                return false;
                            }
                            interfaceNode.externalPortId = external;
                        }
                        portSet->interfaces.push_back(interfaceNode);
                        if (!isTopLevel)
                            rootPortSet->interfaces.push_back(interfaceNode);
                        if (!rootPortSet->rebuild() || !rootPortSet->resume(active))
                        {
                            portSet->interfaces.pop_back();
                            if (!isTopLevel)
                                rootPortSet->interfaces.pop_back();
                            if (isTopLevel && !offline_)
                                portSet->ports.pop_back();
                            rootPortSet->rebuild();
                            rootPortSet->resume(active);
                            return false;
                        }
                        if (!parentCanvasGuard.isNull())
                            parentCanvasGuard->setSubGraphInterfaces(
                                subGraphId, portSet->contextInterfaces(contextId));
                        return true;
                    });
                childCanvas->setInterfaceNodeRenamedHandler([subGraphPorts, contextId,
                                                             rootContextId, isTopLevel,
                                                             parentCanvasGuard, childCanvasGuard,
                                                             subGraphId](
                                                                const std::string &interfaceId,
                                                                const QString &name) {
                    const auto portSet = subGraphPorts->find(contextId);
                    if (portSet == subGraphPorts->end() || portSet->second == nullptr)
                        return;
					const auto interfaceNode = std::find_if(portSet->second->interfaces.begin(), portSet->second->interfaces.end(), [&interfaceId](const auto& value) {
						return value.id == interfaceId;
					});
                    if (interfaceNode != portSet->second->interfaces.end())
                    {
                        const auto oldPortId = interfaceNode->portId;
                        if (isTopLevel)
                        {
                            const auto port = std::find_if(
                                portSet->second->ports.begin(), portSet->second->ports.end(),
                                [&oldPortId](const auto &value) { return value.id == oldPortId; });
                            const auto portName = name.toUtf8().toStdString();
                            if (port != portSet->second->ports.end() &&
                                WinJACKNexus::Audio::JackApi::instance().portRename(
                                    portSet->second->client.nativeHandle(),
                                    port->port.nativeHandle(), portName.c_str()) == 0)
                            {
                                const auto *renamedPortId = port->port.name();
                                if (renamedPortId)
                                    interfaceNode->externalPortId = renamedPortId;
                            }
                            else if (!portSet->second->offline)
                            {
                                qWarning()
                                    << "Native interface rename failed; stable mapping retained";
                                if (!childCanvasGuard.isNull())
                                    childCanvasGuard->setInterfaceNodes(
                                        portSet->second->contextInterfaces(contextId));
                                if (!parentCanvasGuard.isNull())
                                    parentCanvasGuard->setSubGraphInterfaces(
                                        subGraphId, portSet->second->contextInterfaces(contextId));
                                return;
                            }
                        }
                        interfaceNode->name = name;
                        if (!isTopLevel)
                        {
                            if (const auto rootPortSet = subGraphPorts->find(rootContextId);
                                rootPortSet != subGraphPorts->end() &&
                                rootPortSet->second != nullptr)
                            {
                                const auto rootInterface =
                                    std::find_if(rootPortSet->second->interfaces.begin(),
                                                 rootPortSet->second->interfaces.end(),
                                                 [&interfaceId](const auto &value) {
                                                     return value.id == interfaceId;
                                                 });
                                if (rootInterface != rootPortSet->second->interfaces.end())
									rootInterface->name = name;
                            }
                        }
                        if (!parentCanvasGuard.isNull())
                            parentCanvasGuard->setSubGraphInterfaces(
                                subGraphId, portSet->second->contextInterfaces(contextId));
                    }
                });
                childCanvas->setInterfaceNodeRemovedHandler([subGraphPorts, contextId,
                                                             rootContextId, isTopLevel,
                                                             parentCanvasGuard, subGraphId](
                                                                const std::string &interfaceId) {
                    const auto contextPortSet = subGraphPorts->find(contextId);
                    if (contextPortSet == subGraphPorts->end() || contextPortSet->second == nullptr)
						return false;
					auto& portSet = *contextPortSet->second;
					const auto interfaceNode = std::find_if(portSet.interfaces.begin(), portSet.interfaces.end(), [&interfaceId](const auto& value) {
						return value.id == interfaceId;
					});
					if (interfaceNode == portSet.interfaces.end())
						return false;
					const auto portId = interfaceNode->portId;
                    const auto root = subGraphPorts->find(rootContextId);
                    if (root == subGraphPorts->end() || !root->second)
                        return false;
                    auto &rootSet = *root->second;
                    const bool active = rootSet.client.isActive();
                    if (active && !rootSet.pause())
                        return false;
                    const auto previousInterfaces = rootSet.interfaces;
                    const auto previousRoutes = rootSet.routes;
                    std::erase_if(rootSet.interfaces,
                                  [&](const auto &value) { return value.id == interfaceId; });
                    std::erase_if(rootSet.routes, [&](const auto &value) {
                        return value.sourcePortId == portId || value.destinationPortId == portId;
                    });
                    if (!rootSet.rebuild())
                    {
                        rootSet.interfaces = previousInterfaces;
                        rootSet.routes = previousRoutes;
                        rootSet.resume(active);
                        return false;
                    }
                    if (isTopLevel)
                    {
                        const auto port =
                            std::find_if(portSet.ports.begin(), portSet.ports.end(),
                                         [&](const auto &value) { return value.id == portId; });
                        if (port != portSet.ports.end())
                        {
                            if (!port->port.unregisterPort())
                            {
                                rootSet.interfaces = previousInterfaces;
                                rootSet.routes = previousRoutes;
                                rootSet.rebuild();
                                rootSet.resume(active);
                                return false;
                            }
                            portSet.ports.erase(port);
                        }
                    }
                    else
                    {
                        portSet.interfaces.erase(interfaceNode);
                    }
                    if (!rootSet.resume(active))
                    {
                        rootSet.interfaces = previousInterfaces;
                        rootSet.routes = previousRoutes;
                        if (isTopLevel && !rootSet.offline)
                        {
                            const auto previous = std::find_if(
                                previousInterfaces.begin(), previousInterfaces.end(),
                                [&](const auto &value) { return value.id == interfaceId; });
                            if (previous != previousInterfaces.end())
                            {
                                const auto definition = std::find_if(
                                    subGraphInterfaceDefinitions().begin(),
                                    subGraphInterfaceDefinitions().end(), [&](const auto &value) {
                                        return previous->type == QLatin1String(value.type);
                                    });
                                portSet.ports.emplace_back();
                                auto &port = portSet.ports.back();
                                port.id = previous->portId;
                                port.kind = previous->kind;
                                port.isInput = definition->flags == JackPortIsInput;
                                if (!port.port.registerPort(
                                        portSet.client, previous->name.toUtf8().constData(),
                                        definition->jackType, definition->flags))
                                {
                                    portSet.ports.pop_back();
                                    qWarning()
                                        << "Failed to restore native interface after activation failure";
                                }
                            }
                        }
                        if (!isTopLevel)
                            portSet.interfaces.push_back(*std::find_if(
                                previousInterfaces.begin(), previousInterfaces.end(),
                                [&](const auto &value) { return value.id == interfaceId; }));
                        rootSet.rebuild();
                        rootSet.resume(active);
                        return false;
                    }
                    if (!parentCanvasGuard.isNull())
                        parentCanvasGuard->setSubGraphInterfaces(
                            subGraphId, portSet.contextInterfaces(contextId));
                    return true;
                });
                childCanvas->setSubGraphNodeDoubleClickHandler([openSubGraphWeak, childCanvasGuard](
                                                                   const std::string &childId) {
                    if (childCanvasGuard.isNull())
						return;
					QTimer::singleShot(0, childCanvasGuard.data(), [openSubGraphWeak, childCanvasGuard, childId] {
						if (const auto openSubGraph = openSubGraphWeak.lock(); openSubGraph != nullptr && !childCanvasGuard.isNull())
							(*openSubGraph)(childCanvasGuard.data(), childId);
					});
                });
                childCanvas->setSubGraphNodeRenamedHandler([updateSubGraphTabTitle, childCanvasGuard](const std::string& childId, const QString& name) {
					if (!childCanvasGuard.isNull())
						(*updateSubGraphTabTitle)(childCanvasGuard.data(), childId, name);
				});
				childCanvas->setSubGraphNodeRemovedHandler([removeSubGraph, childCanvasGuard](const std::string& childId) {
					return !childCanvasGuard.isNull() && (*removeSubGraph)(childCanvasGuard.data(), childId);
				});
				childCanvas->setSubGraphNodeDropHandler([childCanvas](const QString& name, const QPointF& position) {
					return !childCanvas->addSubGraph(name, position).empty();
				});
				childCanvas->setModuleNodeDropHandler([childCanvas, createModuleNode](const QString& modulePath, std::uint32_t index, const QPointF& position) {
					return *createModuleNode && (*createModuleNode)(childCanvas, modulePath, index, position, {});
				});
				childCanvas->setVstNodeDropHandler([childCanvas, createModuleNode](const QString& modulePath, std::uint32_t index, const QString& settingsJson, const QPointF& position) {
					return *createModuleNode && (*createModuleNode)(childCanvas, modulePath, index, position, settingsJson);
				});
				if (*configureCanvasInteractions)
					(*configureCanvasInteractions)(childCanvas);
				childCanvas->setSubGraphNodeAddedHandler([canvasTabs, openSubGraphWeak, childCanvasGuard](const std::string& childId, const QString&) {
					if (childCanvasGuard.isNull())
						return;
					const auto activeTab = canvasTabs->currentIndex();

						if (const auto openSubGraph = openSubGraphWeak.lock(); openSubGraph != nullptr && !childCanvasGuard.isNull()) {
							(*openSubGraph)(childCanvasGuard.data(), childId);
							if (activeTab >= 0)
								canvasTabs->setCurrentIndex(activeTab);
						}

				});
				childCanvas->setInterfaceNodeDropHandler([childCanvas](const QString& type, const QPointF& position) {
					return childCanvas->addInterfaceNode(type, position);
				});
				if (jackObserver_.isOpen())
					childCanvas->setSnapshot(WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_));
				const auto restoredSubGraph = restoredSubGraphs->find(contextId);
                if (restoredSubGraph != restoredSubGraphs->end())
                    childCanvas->restoreSubGraphNodes(restoredSubGraph->second.nodes);
                if (restoredSubGraph != restoredSubGraphs->end() && !hadBackend)
                {
                    childCanvas->restoreSubGraphNodes(restoredSubGraph->second.nodes);
                    if (!childCanvas->restoreInterfaceNodes(restoredSubGraph->second.interfaces))
                    {
                        childCanvas->deleteLater();
						return;
                    }
                }
                else if (const auto savedPorts = subGraphPorts->find(contextId);
                         savedPorts != subGraphPorts->end() && savedPorts->second != nullptr)
                {
                    std::vector<PatchbayInterfaceInfo> contextInterfaces;
                    const auto interfacePrefix = contextId + "/interface-";
                    std::copy_if(savedPorts->second->interfaces.cbegin(), savedPorts->second->interfaces.cend(),
					             std::back_inserter(contextInterfaces), [&interfacePrefix](const auto& interfaceNode) {
						return interfaceNode.id.starts_with(interfacePrefix);
					             });
					childCanvas->setInterfaceNodes(contextInterfaces);
					parentCanvas->setSubGraphInterfaces(subGraphId, std::move(contextInterfaces));
                }
                if (createDefaultInterfaces)
                {
                    childCanvas->addInterfaceNode(QStringLiteral("audio_in"), {});
                    childCanvas->addInterfaceNode(QStringLiteral("audio_in"), {});
					childCanvas->addInterfaceNode(QStringLiteral("audio_out"), {});
					childCanvas->addInterfaceNode(QStringLiteral("audio_out"), {});
					childCanvas->addInterfaceNode(QStringLiteral("midi_in"), {});
                }
                const auto tabTitle = context.title.isEmpty()
                                          ? patchText("graph.defaultName", "Subgraph")
                                          : context.title;
                const auto tabIndex = canvasTabs->addTab(childCanvas, tabTitle);
				subGraphCanvases->emplace(contextId, childCanvas);
				canvasTabs->setCurrentIndex(tabIndex);
			};
			auto restoreSubGraphs = [canvas, canvasTabs, subGraphCanvases, restoredSubGraphs, clearSubGraphs, openSubGraph](const std::vector<PatchbaySubGraphProject>& projects) {
				clearSubGraphs();
				restoredSubGraphs->clear();
				for (const auto& project : projects)
					restoredSubGraphs->emplace(project.context.id, project);
				const auto mainProject = restoredSubGraphs->find("main");
				if (mainProject == restoredSubGraphs->end())
					return projects.empty();
				canvas->restoreSubGraphNodes(mainProject->second.nodes);
				std::vector<const PatchbaySubGraphProject*> contexts;
				for (const auto& [contextId, project] : *restoredSubGraphs) {
					if (contextId != "main")
						contexts.push_back(&project);
				}
				std::sort(contexts.begin(), contexts.end(), [](const auto* left, const auto* right) {
					return left->context.id.size() < right->context.id.size();
				});
				for (const auto* project : contexts) {
					const auto separator = project->context.id.rfind('/');
					if (separator == std::string::npos)
						return false;
					const auto parentContextId = project->context.id.substr(0, separator);
					const auto subGraphId = project->context.id.substr(separator + 1);
					auto* parentCanvas = parentContextId == "main" ? canvas : nullptr;
					if (parentCanvas == nullptr) {
						const auto parent = subGraphCanvases->find(parentContextId);
						if (parent == subGraphCanvases->end() || parent->second.isNull())
							return false;
						parentCanvas = parent->second.data();
					}
					(*openSubGraph)(parentCanvas, subGraphId);
					if (!subGraphCanvases->contains(project->context.id))
						return false;
				}
				canvasTabs->setCurrentIndex(0);
				return true;
			};
			QPointer<PatchbayCanvas> canvasGuard(canvas);
			canvas->setSubGraphNodeDoubleClickHandler([openSubGraph, canvasGuard](const std::string& subGraphId) {
				if (canvasGuard.isNull())
					return;
				QTimer::singleShot(0, canvasGuard.data(), [openSubGraph, canvasGuard, subGraphId] {
					if (!canvasGuard.isNull())
						(*openSubGraph)(canvasGuard.data(), subGraphId);
				});
			});
			canvas->setSubGraphNodeRenamedHandler([updateSubGraphTabTitle, canvasGuard](const std::string& subGraphId, const QString& name) {
				if (!canvasGuard.isNull())
					(*updateSubGraphTabTitle)(canvasGuard.data(), subGraphId, name);
			});
			canvas->setSubGraphNodeRemovedHandler([removeSubGraph, canvasGuard](const std::string& subGraphId) {
				return !canvasGuard.isNull() && (*removeSubGraph)(canvasGuard.data(), subGraphId);
			});
			canvas->setSubGraphNodeDropHandler([canvas](const QString& name, const QPointF& position) {
				return !canvas->addSubGraph(name, position).empty();
			});
			canvas->setSubGraphNodeAddedHandler([canvasTabs, openSubGraphWeak, canvasGuard](const std::string& subGraphId, const QString&) {
				if (canvasGuard.isNull())
					return;
				const auto activeTab = canvasTabs->currentIndex();

					if (const auto openSubGraph = openSubGraphWeak.lock(); openSubGraph != nullptr && !canvasGuard.isNull()) {
						(*openSubGraph)(canvasGuard.data(), subGraphId);
						if (activeTab >= 0)
							canvasTabs->setCurrentIndex(activeTab);
					}

			});
			QObject::connect(canvasTabs, &QTabWidget::tabCloseRequested, window_, [canvasTabs, refreshNodeLibrary](int index) {
				if (index <= 0)
					return;
				auto* widget = canvasTabs->widget(index);
				canvasTabs->removeTab(index);
				if (widget != nullptr)
					widget->hide();
				QTimer::singleShot(0, canvasTabs, [refreshNodeLibrary] {
					(*refreshNodeLibrary)();
				});
			});
			auto lastObservedJackGraph = std::make_shared<Snapshot>();
			auto hasObservedJackGraph = std::make_shared<bool>(false);
			auto canRefreshJackGraph = [canvas, subGraphCanvases] {
				const auto childCanvasIsDragging = std::any_of(subGraphCanvases->cbegin(), subGraphCanvases->cend(), [](const auto& entry) {
					return !entry.second.isNull() && entry.second->isNodeDragActive();
				});
				return !canvas->isNodeDragActive() && !childCanvasIsDragging;
			};
			auto applyJackSnapshot = [this,
			                              canvas,
			                              subGraphCanvases,
			                              refreshNodeLibrary,
			                              refreshTrayNodeGuiEntries,
			                              lastObservedJackGraph,
			                              hasObservedJackGraph](Snapshot snapshot) -> bool {
				const auto selectedClientId = canvas->selectedNodeId();
				const auto previousSnapshot = canvas->snapshot();
				*lastObservedJackGraph = snapshot;
				*hasObservedJackGraph = true;
				const auto moduleRebindings = rebindLoadedModuleClients(snapshot);
				const auto reboundClients = inferClientRebindings(previousSnapshot, snapshot, moduleRebindings);
				for (const auto& node : loadedModuleNodes_) {
					const auto rebound = std::find_if(reboundClients.cbegin(), reboundClients.cend(), [&node](const auto& pair) {
						return pair.first == node->clientId();
					});
					if (rebound != reboundClients.cend())
						node->setClientId(rebound->second);
				}
				mergeReboundClientMetadata(previousSnapshot, snapshot, reboundClients);
				preserveLoadedModuleClients(previousSnapshot, snapshot, loadedModuleNodes_);
				preserveOfflineJackRecords(previousSnapshot, snapshot, reboundClients);
				mergeReboundConnections(previousSnapshot, snapshot, reboundClients);
				for (const auto& node : loadedModuleNodes_) {
					if (!isDigitalMixerModule(node->modulePath()) || !node->hasClient()) continue;
					WinJACKNexus::Common::DigitalMixerState mixerState;
					if (!mixerState.loadJson(QByteArray::fromStdString(node->settingsJson()))) continue;
					std::unordered_map<std::string, std::uint32_t> portOrder;
					auto orderedPorts = mixerState.ports();
					const auto priority = [&mixerState](const auto& port) {
						return port.stripId == mixerState.main().id ? 0 : port.stripId == 0 ? 1 : 2;
					};
					std::stable_sort(orderedPorts.begin(), orderedPorts.end(), [&](const auto& left, const auto& right) {
						return priority(left) < priority(right);
					});
					for (std::uint32_t index = 0; index < orderedPorts.size(); ++index)
						portOrder.emplace(orderedPorts[index].name.toStdString(), index);
					for (auto& port : snapshot.ports) {
						if (port.clientId != node->clientId()) continue;
						const auto position = portOrder.find(port.name);
						if (position != portOrder.end()) port.ordinal = position->second;
					}
				}
				for (const auto& client : snapshot.clients)
					knownJackClients_.insert(client.name);
                for (const auto &node : loadedModuleNodes_)
                {
                    if (!node->isVirtual())
                        continue;
                    std::erase_if(snapshot.clients, [&](const auto &client) {
                        return client.id == node->clientId();
                    });
                    std::erase_if(snapshot.ports, [&](const auto &port) {
                        return port.clientId == node->clientId();
                    });
                    node->appendVirtualSnapshot(snapshot);
                }
                std::unordered_set<std::string> childModuleClients;
                std::unordered_map<std::string, std::unordered_set<std::string>> modulesByContext;
                std::unordered_map<std::string, QString> clientThemeScopes;
				for (const auto& node : loadedModuleNodes_) {
					if (!node->hasClient())
						continue;
					const auto prefix = WinJACKNexus::UI::ThemeNamespaceRegistry::instance().scopeFor(
						WinJACKNexus::UI::ThemeModuleKind::node, node->modulePath(), node->index());
					if (prefix.startsWith(QStringLiteral("--wjn-theme-")) && prefix.endsWith(QLatin1Char('-')))
						clientThemeScopes.emplace(node->clientId(), prefix.mid(12, prefix.size() - 13));
					if (node->contextId() == "main")
						continue;
					childModuleClients.insert(node->clientId());
					modulesByContext[node->contextId()].insert(node->clientId());
				}
				canvas->setHiddenModuleClientIds(std::move(childModuleClients));
				canvas->setClientThemeScopes(clientThemeScopes);
				canvas->setSnapshot(snapshot);
				canvas->restoreJackConnections(jackObserver_);
				for (const auto& [contextId, child] : *subGraphCanvases) {
					if (!child.isNull()) {
						child->setVisibleModuleClientIds(modulesByContext[contextId]);
						child->setClientThemeScopes(clientThemeScopes);
						child->setSnapshot(snapshot);
					}
				}
				for (const auto& [oldClientId, newClientId] : reboundClients) {
					if (oldClientId == selectedClientId)
						canvas->selectNode(newClientId);
				}
				(*refreshTrayNodeGuiEntries)();
				(*refreshNodeLibrary)();
				return true;
			};
            auto refreshJackGraph = [this, canRefreshJackGraph, applyJackSnapshot] {
                if (offline_)
                    return applyJackSnapshot(Snapshot{});
                if (!jackObserver_.isOpen() || !canRefreshJackGraph())
                    return false;
                auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
                ensureSystemClient(snapshot);
				return applyJackSnapshot(std::move(snapshot));
            };
            auto pollJackGraph = [this, canRefreshJackGraph, applyJackSnapshot,
                                  lastObservedJackGraph, hasObservedJackGraph] {
                if (!jackObserver_.isOpen() || !canRefreshJackGraph())
                    return false;
				auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
				ensureSystemClient(snapshot);
				if (*hasObservedJackGraph && hasSameObservedJackGraph(*lastObservedJackGraph, snapshot))
					return true;
				return applyJackSnapshot(std::move(snapshot));
            };
            auto lastMixerSettings =
                std::make_shared<std::unordered_map<std::string, std::string>>();
            auto refreshPendingJackGraph = [this, subGraphPorts, refreshJackGraph, pollJackGraph,
                                            lastMixerSettings] {
                for (const auto &[id, root] : *subGraphPorts)
                {
                    if (root && isTopLevelSubGraphContext(id))
                        root->synchronizeClock();
                }
                std::unordered_map<std::string, std::string> currentMixerSettings;
                for (const auto &node : loadedModuleNodes_)
                {
                    if (isDigitalMixerModule(node->modulePath()) && node->hasClient())
                        currentMixerSettings.emplace(node->clientId(), node->settingsJson());
                    else if (isClassicMixerModule(node->modulePath()) &&
                             node->hasClient())
                        currentMixerSettings.emplace(node->clientId(), node->classicMixerRevision());
                }
                if (currentMixerSettings != *lastMixerSettings)
                {
                    if (refreshJackGraph())
                        *lastMixerSettings = std::move(currentMixerSettings);
                    return;
                }
                if (!graphOrderCallbackRegistered_)
                {
                    pollJackGraph();
                    return;
                }
                if (graphRefreshPending_.exchange(false, std::memory_order_acq_rel) &&
                    !refreshJackGraph())
                {
                    graphRefreshPending_.store(true, std::memory_order_release);
                }
            };
            auto restoreModuleRecords = [this, subGraphPorts, restoredSubGraphs, subGraphCanvases,
                                         refreshTrayNodeGuiEntries](
                                            const std::vector<PatchbayProjectModule> &modules) {
                unloadedModuleRecords_.clear();
                for (const auto &node : loadedModuleNodes_)
                {
                    if (!node->isVirtual())
                        continue;
                    const auto root = subGraphPorts->find(rootSubGraphContext(node->contextId()));
                    if (root != subGraphPorts->end() && root->second &&
                        !root->second->detach(node->clientId()))
                        return;
                }
                std::erase_if(loadedModuleNodes_, [](const auto &node) {
                    return !isDigitalMixerModule(node->modulePath());
                });
                for (const auto &module : modules)
                {
                    if (module.contextId == "main" && isDigitalMixerModule(module.modulePath))
                    {
                        const auto mixer =
                            std::find_if(loadedModuleNodes_.begin(), loadedModuleNodes_.end(),
                                         [](const auto &node) {
                                             return isDigitalMixerModule(node->modulePath()) &&
                                                    node->index() == 0;
                                         });
                        if (mixer != loadedModuleNodes_.end() &&
                            !(*mixer)->restoreSettingsJson(module.settingsJson))
                            PatchbayDialogs::message(window_,QMessageBox::Warning,"error.mixerRestoreTitle","Mixer restore failed","error.mixerRestore","Unable to restore the mixer settings saved in the project.");
						continue;
                    }
                    const auto mode =
                        module.contextId == "main" ? WJN_NODE_MODE_JACK : WJN_NODE_MODE_VIRT;
                    std::set<std::string> existingClients;
                    if (mode == WJN_NODE_MODE_JACK && jackObserver_.isOpen())
                    {
                        for (const auto &client :
                             WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_).clients)
                            existingClients.insert(client.id);
                    }
                    std::shared_ptr<SubGraphPortSet> root;
                    if (mode == WJN_NODE_MODE_VIRT)
                    {
                        const auto found =
                            subGraphPorts->find(rootSubGraphContext(module.contextId));
                        if (found != subGraphPorts->end())
                            root = found->second;
                    }
                    auto node = mode == WJN_NODE_MODE_VIRT && !root
                                    ? nullptr
                                    : LoadedModuleNode::create(module.modulePath, module.index,
                                                               module.settingsJson, mode,
                                                               root ? root->sampleRate : 48000.0,
                                                               root ? root->maxFrames : 8192);
                    if (node == nullptr)
                    {
                        qWarning().noquote() << "Preserving unloadable node:" << module.modulePath
                                             << QString::fromStdString(module.contextId);
                        unloadedModuleRecords_.push_back(module);
                        continue;
                    }
                    if (mode == WJN_NODE_MODE_JACK && jackObserver_.isOpen())
                    {
                        const auto snapshot =
                            WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
                        const auto client =
                            std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(),
                                         [&node, &existingClients](const auto &value) {
                                             return node->hasClient()
                                                        ? value.id == node->clientId()
                                                        : !existingClients.contains(value.id);
                                         });
                        if (client != snapshot.clients.cend())
                            node->setClientId(client->id);
                    }
                    if (!module.clientId.empty() && (node->isVirtual() || !node->hasClient()))
                        node->setClientId(module.clientId);
                    node->setContextId(module.contextId);
                    if (root)
                    {
                        const auto ports = node->virtualPorts();
                        if (ports.empty() || !root->attach(node->virtualProcessor(), ports))
                        {
                            unloadedModuleRecords_.push_back(module);
                            continue;
                        }
                    }
                    loadedModuleNodes_.push_back(std::move(node));
                }
                for (const auto &[id, project] : *restoredSubGraphs)
                {
                    const auto child = subGraphCanvases->find(id);
                    if (child == subGraphCanvases->end() || child->second.isNull())
                        continue;
                    auto connections = project.connections;
                    for (auto &connection : connections)
                    {
                        for (const auto &context : *restoredSubGraphs)
                        {
                            for (const auto &port : context.second.interfaces)
                            {
                                const auto stableId = port.id + ":port";
                                if (connection.sourcePortId == port.portId ||
                                    connection.sourcePortId == port.externalPortId)
                                    connection.sourcePortId = stableId;
                                if (connection.destinationPortId == port.portId ||
                                    connection.destinationPortId == port.externalPortId)
                                    connection.destinationPortId = stableId;
                            }
                        }
                    }
                    child->second->restoreLogicalConnections(std::move(connections));
                }
                restoredSubGraphs->clear();
                (*refreshTrayNodeGuiEntries)();
            };
            *createModuleNode = [this, subGraphPorts, refreshJackGraph, refreshTrayNodeGuiEntries,
                                 window = window_.data()](
                                    PatchbayCanvas *targetCanvas, const QString &modulePath,
                                    std::uint32_t index, const QPointF &position,
                                    const QString &settingsJson) {
                if (targetCanvas == nullptr)
                    return false;
                const auto contextId = targetCanvas->sceneContext().id;
                const auto mode = contextId == "main" ? WJN_NODE_MODE_JACK : WJN_NODE_MODE_VIRT;
                if (offline_ && mode == WJN_NODE_MODE_JACK)
                    return false;
                std::shared_ptr<SubGraphPortSet> root;
                if (mode == WJN_NODE_MODE_VIRT)
                {
                    const auto found = subGraphPorts->find(rootSubGraphContext(contextId));
                    if (found == subGraphPorts->end() || !found->second)
                        return false;
                    root = found->second;
                }
                if (mode == WJN_NODE_MODE_JACK && isDigitalMixerModule(modulePath) &&
                    hasLoadedDigitalMixer())
                    return showLoadedDigitalMixer() > 0;
                std::set<std::string> existingClients;
                if (mode == WJN_NODE_MODE_JACK && jackObserver_.isOpen())
                {
                    for (const auto &client :
                         WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_).clients)
                        existingClients.insert(client.id);
                }

                auto node = LoadedModuleNode::create(modulePath, index, settingsJson.toStdString(),
                                                     mode, root ? root->sampleRate : 48000.0,
                                                     root ? root->maxFrames : 8192);
                if (node == nullptr)
                {
                    PatchbayDialogs::message(
                        window, QMessageBox::Warning, "error.nodeStartTitle", "Node startup failed",
                        "error.nodeStart",
                        "Unable to initialize the selected module node. Check that JACK and the system device are available.");
                    return false;
                }
                if (mode == WJN_NODE_MODE_JACK && jackObserver_.isOpen())
                {
                    const auto snapshot =
                        WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
                    const auto client =
                        std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(),
                                     [&node, &existingClients](const auto &value) {
                                         return node->hasClient()
                                                    ? value.id == node->clientId()
                                                    : !existingClients.contains(value.id);
                                     });
                    if (client != snapshot.clients.cend())
                    {
                        node->setClientId(client->id);
                        auto settings = patchbaySettings();
						settings.setValue(QStringLiteral("layout/%1/x").arg(QString::fromStdString(client->id)), position.x());
						settings.setValue(QStringLiteral("layout/%1/y").arg(QString::fromStdString(client->id)), position.y());
                    }
                }
                auto *createdNode = node.get();
                node->setContextId(contextId);
                if (root)
                {
                    const auto ports = node->virtualPorts();
                    if (ports.empty() || !root->attach(node->virtualProcessor(), ports))
                        return false;
                }
                loadedModuleNodes_.push_back(std::move(node));
                (*refreshTrayNodeGuiEntries)();
                targetCanvas->markProjectDirty();
				refreshJackGraph();
                QTimer::singleShot(
                    100, window,
                    [this, mode, createdNode, existingClients = std::move(existingClients),
                     refreshJackGraph] {
                        if (!jackObserver_.isOpen() || mode == WJN_NODE_MODE_VIRT)
                            return;
                        const auto node =
                            std::find_if(loadedModuleNodes_.begin(), loadedModuleNodes_.end(),
                                         [createdNode](const auto &value) {
                                             return value.get() == createdNode;
                                         });
                        if (node == loadedModuleNodes_.end() || !(*node)->ownsClient({}))
                            return;
                        const auto snapshot =
                            WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
                        const auto client = std::find_if(
                            snapshot.clients.cbegin(), snapshot.clients.cend(),
                            [this, &existingClients](const auto &value) {
                                return !existingClients.contains(value.id) &&
                                       std::none_of(loadedModuleNodes_.cbegin(),
                                                    loadedModuleNodes_.cend(),
                                                    [&value](const auto &node) {
                                                        return node->ownsClient(value.id);
                                                    });
                            });
                        if (client != snapshot.clients.cend())
                            (*node)->setClientId(client->id);
                        refreshJackGraph();
                    });
                return true;
            };
            QObject::connect(
                nodeTree, &QTreeWidget::itemDoubleClicked, window_,
                [canvasTabs, createModuleNode](QTreeWidgetItem *item) {
                    const auto interfaceType = item->data(0, Qt::UserRole + 3).toString();
                    if (!interfaceType.isEmpty())
                    {
                        if (auto *activeCanvas =
                                dynamic_cast<PatchbayCanvas *>(canvasTabs->currentWidget()))
                            activeCanvas->addInterfaceNode(interfaceType, {});
                        return;
                    }
                    const auto subGraphName = item->data(0, Qt::UserRole + 2).toString();
                    if (!subGraphName.isEmpty())
                    {
                        if (auto *activeCanvas =
                                dynamic_cast<PatchbayCanvas *>(canvasTabs->currentWidget()))
                            activeCanvas->addSubGraph(subGraphName, {});
                        return;
                    }
                    const auto modulePath = item->data(0, Qt::UserRole).toString();
                    if (modulePath.isEmpty())
                        return;
                    if (auto *activeCanvas =
                            dynamic_cast<PatchbayCanvas *>(canvasTabs->currentWidget()))
                        (*createModuleNode)(activeCanvas, modulePath,
                                            item->data(0, Qt::UserRole + 1).toUInt(), {},
                                            item->data(0, Qt::UserRole + 4).toString());
                });
            canvas->setModuleNodeDropHandler([canvas, createModuleNode](const QString &modulePath,
                                                                        std::uint32_t index,
                                                                        const QPointF &position) {
                return (*createModuleNode)(canvas, modulePath, index, position, {});
            });
            canvas->setVstNodeDropHandler([canvas, createModuleNode](const QString& modulePath, std::uint32_t index, const QString& settingsJson, const QPointF& position) {
				return (*createModuleNode)(canvas, modulePath, index, position, settingsJson);
			});
			auto selectedModuleNode = std::make_shared<LoadedModuleNode*>(nullptr);
			auto* inspectorDock = new WinJACKNexus::UI::Controls::DockPanel(
				patchText("inspector.title","Properties"), Qt::RightDockWidgetArea, window_);
			bindPatchText(inspectorDock,"windowTitle","inspector.title","Properties");
			inspectorDock->setObjectName(QStringLiteral("patchbay.inspector"));
			auto* inspector = new QWidget(inspectorDock);
			auto* inspectorLayout = new QVBoxLayout(inspector);
			// Keep every inspector field clear of the overlaid resize grip.
			inspectorLayout->setContentsMargins(32, 12, 12, 12);
			auto* selectionTitle = new WinJACKNexus::UI::Controls::ReadOnlyField(inspector);
			bindPatchText(selectionTitle,"placeholderText","inspector.noSelection","No node selected");
			selectionTitle->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			auto* properties = new QWidget(inspector);
			auto* propertyForm = new QFormLayout(properties);
			auto* categoryValue = new WinJACKNexus::UI::Controls::ReadOnlyField(properties);
			auto* splitCheck = new WinJACKNexus::UI::Controls::ToggleField({}, properties);
			bindPatchText(splitCheck,"text","inspector.split","Split inputs and outputs");
			if (auto* manager=WinJACKNexus::UI::I18nManager::installed()) manager->bind(categoryValue,[categoryValue] {
                categoryValue->setText(categoryDisplayName(categoryValue->property("wjn.categoryIdentity").toString()));
            });
            categoryValue->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			splitCheck->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			auto* categoryLabel=new WinJACKNexus::UI::Controls::TextLabel(properties);
			bindPatchText(categoryLabel,"text","inspector.category","Category");
			propertyForm->addRow(categoryLabel, categoryValue);
			propertyForm->addRow(QString(), splitCheck);
			properties->setEnabled(false);
			inspectorLayout->addWidget(selectionTitle);
			inspectorLayout->addWidget(properties);
			auto* showGuiButton = patchButton("action.showGui","Show module GUI", inspector);
			showGuiButton->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			showGuiButton->setObjectName(QStringLiteral("showModuleGuiButton"));
			showGuiButton->setEnabled(false);
			inspectorLayout->addWidget(showGuiButton);
			inspectorLayout->addStretch(1);
			// Module forms may require a wide minimum size (for example device names).
            // Scroll their content instead of propagating that minimum to the dock.
            auto* inspectorScroll = new WinJACKNexus::UI::Controls::ScrollField(inspectorDock);
            inspectorScroll->setObjectName(QStringLiteral("patchbay.inspectorScroll"));
            inspectorScroll->setWidgetResizable(true);
            inspectorScroll->setWidget(inspector);
            inspectorDock->setWidget(inspectorScroll);
			window_->addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
			QTimer::singleShot(0, window_, [window = window_, nodeDock, inspectorDock] {
				window->resizeDocks({ nodeDock, inspectorDock }, { 270, 310 }, Qt::Horizontal);
			});

				auto selectedClient = std::make_shared<std::string>();
				auto selectedSettingsContainer = std::make_shared<QPointer<QWidget>>();
                *configureCanvasInteractions = [this, canvasTabs, selectionTitle, categoryValue,
                                                splitCheck, propertyForm, properties, showGuiButton,
                                                selectedClient, selectedModuleNode,
                                                selectedSettingsContainer, inspector,
                                                inspectorLayout, createModuleNode, subGraphPorts,
                                                refreshJackGraph, refreshTrayNodeGuiEntries](
                                                   PatchbayCanvas *targetCanvas) {
                    if (targetCanvas == nullptr)
                        return;
                    targetCanvas->setNodeAddMenuHandler([targetCanvas, createModuleNode](
                                                            QMenu &menu, const QPointF &position) {
                        QTreeWidget library;
                        populateNodeLibrary(&library, {}, targetCanvas->sceneContext().isSubGraph);
                        const auto append = [&](auto &&self, QMenu &parent,
                                                QTreeWidgetItem *item) -> void {
                            if (item->childCount())
                            {
                                auto *category = parent.addMenu(item->text(0));
                                for (int index = 0; index < item->childCount(); ++index)
                                    self(self, *category, item->child(index));
                                return;
                            }
                            const auto module = item->data(0, Qt::UserRole).toString();
                            const auto index = item->data(0, Qt::UserRole + 1).toUInt();
                            const auto graph = item->data(0, Qt::UserRole + 2).toString();
                            const auto interfaceType = item->data(0, Qt::UserRole + 3).toString();
                            const auto settings = item->data(0, Qt::UserRole + 4).toString();
                            if (module.isEmpty() && graph.isEmpty() && interfaceType.isEmpty())
                                return;
                            auto *action = parent.addAction(item->text(0));
                            action->setToolTip(item->toolTip(0));
                            action->setData(module);
                            QObject::connect(action, &QAction::triggered, targetCanvas,
                                             [targetCanvas, createModuleNode, position, module,
                                              index, graph, interfaceType, settings] {
                                                 if (!interfaceType.isEmpty())
                                                     targetCanvas->addInterfaceNode(interfaceType,
                                                                                    position);
                                                 else if (!graph.isEmpty())
                                                     targetCanvas->addSubGraph(graph, position);
                                                 else
                                                     (*createModuleNode)(targetCanvas, module,
                                                                         index, position, settings);
                                             });
                        };
                        for (int index = 0; index < library.topLevelItemCount(); ++index)
                            append(append, menu, library.topLevelItem(index));
                    });
                    targetCanvas->setBoxSelectionExclusionHandler([this](
                                                                      const std::string &clientId) {
                        return std::any_of(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                           [&clientId](const auto &node) {
                                               return node->ownsClient(clientId) &&
                                                      isDigitalMixerModule(node->modulePath());
                                           });
                    });
                    targetCanvas->setModuleNodeHandlers(
                        [this](const std::string &clientId) {
                            return std::any_of(loadedModuleNodes_.cbegin(),
                                               loadedModuleNodes_.cend(),
                                               [&clientId](const auto &node) {
                                                   return node->ownsClient(clientId);
                                               });
                        },
                        [this](const std::string &clientId) {
                            const auto node =
                                std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                             [&clientId](const auto &value) {
                                                 return value->ownsClient(clientId);
                                             });
                            if (node == loadedModuleNodes_.cend() ||
                                isDigitalMixerModule((*node)->modulePath()))
                                return false;
                            copiedModuleNode_ =
                                ModuleNodeInfo{{}, {}, (*node)->modulePath(), (*node)->index()};
                            return true;
                        },
                        [this, subGraphPorts, refreshJackGraph, refreshTrayNodeGuiEntries,
                         selectedModuleNode](const std::string &clientId) {
                            if (std::any_of(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                            [&clientId](const auto &node) {
                                                return node->ownsClient(clientId) &&
                                                       isDigitalMixerModule(node->modulePath());
                                            }))
                                return false;
                            for (const auto &node : loadedModuleNodes_)
                            {
                                if (!node->ownsClient(clientId) || !node->isVirtual())
                                    continue;
                                const auto root =
                                    subGraphPorts->find(rootSubGraphContext(node->contextId()));
                                if (root == subGraphPorts->end() || !root->second ||
                                    !root->second->detach(clientId))
                                    return false;
                            }
                            if (*selectedModuleNode != nullptr &&
                                (*selectedModuleNode)->ownsClient(clientId))
                            {
                                (*selectedModuleNode)->clearSettingsView();
                                *selectedModuleNode = nullptr;
                            }
                            const auto removed =
                                std::erase_if(loadedModuleNodes_, [&clientId](const auto &node) {
                                    return node->ownsClient(clientId);
                                });
                            if (removed != 0)
                                refreshJackGraph();
                            if (removed != 0)
                                (*refreshTrayNodeGuiEntries)();
                            return removed != 0;
                        },
                        [this](const std::string &clientId, QWidget *parent) {
                            const auto node =
                                std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                             [&clientId](const auto &value) {
                                                 return value->ownsClient(clientId);
                                             });
                            return node != loadedModuleNodes_.cend() && (*node)->hasCustomGui() &&
                                   !(*node)->isMeterProbe() && (*node)->showGui(parent);
                        },
                        [this](const std::string &clientId) {
                            const auto node =
                                std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                             [&clientId](const auto &value) {
                                                 return value->ownsClient(clientId);
                                             });
                            return node != loadedModuleNodes_.cend() && (*node)->hasCustomGui() &&
                                   !(*node)->isMeterProbe();
                        },
                        [targetCanvas, createModuleNode, this](const QPointF &position) {
                            return !copiedModuleNode_.modulePath.isEmpty() &&
                                   (*createModuleNode)(targetCanvas, copiedModuleNode_.modulePath,
                                                       copiedModuleNode_.index, position, {});
                        });
                    targetCanvas->setBatchModuleNodeHandlers(
                        [this](
                            const std::string &clientId) -> std::optional<PatchbayModuleCopyInfo> {
                            const auto node =
                                std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                             [&clientId](const auto &value) {
                                                 return value->ownsClient(clientId);
                                             });
                            if (node == loadedModuleNodes_.cend() ||
                                isDigitalMixerModule((*node)->modulePath()))
                                return std::nullopt;
                            return PatchbayModuleCopyInfo{
                                (*node)->modulePath(), (*node)->index(),
                                QString::fromStdString((*node)->settingsJson())};
                        },
                        [targetCanvas, createModuleNode](const PatchbayModuleCopyInfo &node,
                                                         const QPointF &position) {
                            return (*createModuleNode)(targetCanvas, node.modulePath, node.index,
                                                       position, node.settingsJson);
                        });
                    targetCanvas->setNodeSelectionHandler(
                        [this, canvasTabs, targetCanvas, selectionTitle, categoryValue, splitCheck,
                         propertyForm, properties, showGuiButton, selectedClient,
                         selectedModuleNode, selectedSettingsContainer, inspector,
                         inspectorLayout](const std::string &clientId) {
                            if (canvasTabs->currentWidget() != targetCanvas)
                                return;
                            const auto sameSelection = *selectedClient == clientId;
                            if (!sameSelection)
                            {
                                if (*selectedModuleNode != nullptr)
                                {
                                    (*selectedModuleNode)->clearSettingsView();
                                    *selectedModuleNode = nullptr;
                                }
                                else if (*selectedSettingsContainer != nullptr)
                                {
                                    WinJACKNexus::UI::NativeWindowUtils::detachNativeWindow(
                                        selectedSettingsContainer->data());
                                    delete selectedSettingsContainer->data();
                                }
                                *selectedSettingsContainer = nullptr;
                            }
                            *selectedClient = clientId;
                            const auto client = std::find_if(
                                targetCanvas->snapshot().clients.cbegin(),
                                targetCanvas->snapshot().clients.cend(),
                                [&clientId](const auto &value) { return value.id == clientId; });
                            const bool selected = client != targetCanvas->snapshot().clients.cend();
                            properties->setEnabled(selected);
                            showGuiButton->setVisible(true);
                            showGuiButton->setEnabled(false);
                            selectionTitle->setText(selected ? QString::fromStdString(client->name)
                                                             : QString());
                            const auto module =
                                selected ? std::find_if(loadedModuleNodes_.cbegin(),
                                                        loadedModuleNodes_.cend(),
                                                        [&clientId](const auto &value) {
                                                            return value->ownsClient(clientId);
                                                        })
                                         : loadedModuleNodes_.cend();
                            QString category;
                            if (selected)
                            {
                                category = targetCanvas->isSubGraphNode(clientId)
                                               ? QStringLiteral("Sub Graph")
                                           : module != loadedModuleNodes_.cend() &&
                                                   !(*module)->category().isEmpty()
                                               ? (*module)->category()
                                           : client->id == "system" ? QStringLiteral("system")
                                           : client->id == "WinJACKNexus-Patchbay"
                                               ? QStringLiteral("Patchbay")
                                               : QStringLiteral("jackclient");
                            }
                            categoryValue->setProperty("wjn.categoryIdentity", category);
                            categoryValue->setText(categoryDisplayName(category));
                            const auto canSplit =
                                selected && category != QStringLiteral("Classic Mixer") &&
                                clientCanSplit(targetCanvas->snapshot(), clientId);
                            propertyForm->setRowVisible(splitCheck, canSplit);
                            splitCheck->setChecked(canSplit && client->split);
                            if (!selected)
                                return;
                            if (module == loadedModuleNodes_.cend())
                                return;
                            const auto canShowModuleGui = !(*module)->isMeterProbe();
                            showGuiButton->setVisible(canShowModuleGui);
                            showGuiButton->setEnabled(canShowModuleGui &&
                                                      (*module)->hasCustomGui());
                            if (sameSelection && *selectedSettingsContainer != nullptr)
                                return;
                            const auto settingsContainer = (*module)->showSettings(inspector);
                            if (settingsContainer == nullptr)
                                return;
                            *selectedModuleNode = module->get();
                            *selectedSettingsContainer = settingsContainer;
                            inspectorLayout->insertWidget(inspectorLayout->count() - 1,
                                                          settingsContainer);
                        });
                };
                (*configureCanvasInteractions)(canvas);
                for (const auto &[contextId, child] : *subGraphCanvases)
                {
                    if (!child.isNull())
                        (*configureCanvasInteractions)(child.data());
                }
                QObject::connect(showGuiButton, &QPushButton::clicked, window_,
                                 [this, selectedClient, showGuiButton] {
                                     if (selectedClient->empty())
                                         return;
                                     const auto module = std::find_if(
                                         loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                                         [selectedClient](const auto &value) {
                                             return value->ownsClient(*selectedClient);
                                         });
                                     if (module == loadedModuleNodes_.cend())
                                     {
                                         showGuiButton->setEnabled(false);
                                         return;
                                     }
                                     if (!(*module)->hasCustomGui() || (*module)->isMeterProbe())
                                     {
                                         showGuiButton->setEnabled(false);
                                         return;
                                     }
                                     (*module)->showGui(window_.data());
                                 });
                QObject::connect(splitCheck, &QCheckBox::toggled, canvas,
                                 [canvas, selectedClient](bool checked) {
                                     if (!selectedClient->empty())
                                         canvas->setClientSplit(*selectedClient, checked);
                                 });

                QObject::connect(nodeFilter, &QLineEdit::textChanged, nodeTree,
                                 [nodeTree](const QString &query) {
                                     nodeTree->setProperty("filterQuery", query);
                                     applyNodeLibraryFilter(nodeTree, query);
                                 });
                auto *toolbar = window_->addToolBar(patchText("toolbar.title", "Patchbay"));
                bindPatchText(toolbar, "windowTitle", "toolbar.title", "Patchbay");
                toolbar->setMovable(false);
                auto *search = new WinJACKNexus::UI::Controls::SearchField(toolbar);
                bindPatchText(search, "placeholderText", "canvas.search", "Search nodes or ports");
                search->setMinimumWidth(190);
                toolbar->addWidget(search);
                toolbar->addSeparator();
                auto *fitButton = patchButton("action.fit", "Fit", toolbar);
                fitButton->setObjectName(QStringLiteral("patchbayFitButton"));
                fitButton->setFixedHeight(30);
                const auto applyFitTheme = [fitButton] {
                    using WinJACKNexus::UI::Controls::themeTokenColor;
                    fitButton->setProperty("surfaceColor",
                                           themeTokenColor(QStringLiteral("patchbay"),
                                                           QStringLiteral("toolbar-button-surface"),
                                                           QColor("#173946")));
                    fitButton->setProperty("borderColor",
                                           themeTokenColor(QStringLiteral("patchbay"),
                                                           QStringLiteral("toolbar-button-border"),
                                                           QColor("#356b7a")));
                    fitButton->setProperty("accentColor",
                                           themeTokenColor(QStringLiteral("patchbay"),
                                                           QStringLiteral("toolbar-button-accent"),
                                                           QColor("#43c7a8")));
                    fitButton->setProperty("textColor",
                                           themeTokenColor(QStringLiteral("patchbay"),
                                                           QStringLiteral("toolbar-button-text"),
                                                           QColor("#e6eef4")));
                    fitButton->update();
                };
                applyFitTheme();
                if (auto *core = WinJACKNexus::UI::ThemeManager::installedThemeCore())
                    QObject::connect(core, &WinJACKNexus::UI::ThemeCore::themeChanged, fitButton,
                                     applyFitTheme);
                toolbar->addWidget(fitButton);
                auto *zoomControl = new WinJACKNexus::UI::Controls::ZoomControl(toolbar);
                toolbar->addWidget(zoomControl);
                QObject::connect(search, &QLineEdit::textChanged, canvas,
                                 &PatchbayCanvas::setSearchQuery);
                QObject::connect(fitButton, &QPushButton::clicked, canvas,
                                 &PatchbayCanvas::zoomFit);
                QObject::connect(zoomControl,
                                 &WinJACKNexus::UI::Controls::ZoomControl::zoomInRequested, canvas,
                                 &PatchbayCanvas::zoomIn);
                QObject::connect(zoomControl,
                                 &WinJACKNexus::UI::Controls::ZoomControl::zoomOutRequested, canvas,
                                 &PatchbayCanvas::zoomOut);
                QObject::connect(zoomControl,
                                 &WinJACKNexus::UI::Controls::ZoomControl::zoomResetRequested,
                                 canvas, &PatchbayCanvas::zoomReset);
                QObject::connect(canvas, &PatchbayCanvas::zoomChanged, zoomControl,
                                 &WinJACKNexus::UI::Controls::ZoomControl::setZoomFactor);
                auto *scanVstAction = new QAction(window_);
                bindPatchText(scanVstAction, "text", "action.scanVst", "Scan VST");
                QObject::connect(
                    scanVstAction, &QAction::triggered, window_,
                    [refreshNodeLibrary, window = window_.data()] {
                        const auto scannerPath = QCoreApplication::applicationDirPath() +
                                                 QStringLiteral("/VST Scanner.exe");
                        if (!QFileInfo::exists(scannerPath))
                        {
                            PatchbayDialogs::message(window, QMessageBox::Warning,
                                                     "error.scannerMissingTitle",
                                                     "Scanner unavailable", "error.scannerMissing",
                                                     "VST Scanner.exe was not found.");
                            return;
                        }
                        auto *scanner = new QProcess(window);
                        QObject::connect(
                            scanner, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                            scanner, [scanner, refreshNodeLibrary](int, QProcess::ExitStatus) {
                                scanner->deleteLater();
                                (*refreshNodeLibrary)();
                            });
                        scanner->start(scannerPath, {QStringLiteral("--output"),
                                                     QCoreApplication::applicationDirPath() +
                                                         QStringLiteral("/nodes/VSTPlugins")});
                        if (!scanner->waitForStarted(1000))
                        {
                            scanner->deleteLater();
                            PatchbayDialogs::message(window, QMessageBox::Warning,
                                                     "error.scannerStartTitle",
                                                     "Scanner startup failed", "error.scannerStart",
                                                     "Unable to start VST Scanner.exe.");
                        }
                    });
                auto *fileMenu = patchMenu(window_->menuBar(), "menu.file", "File");
				auto currentProject = std::make_shared<QString>();
				auto collectSubGraphs = [canvas, subGraphCanvases] {
					std::vector<PatchbaySubGraphProject> projects;
                    projects.push_back({canvas->sceneContext(), canvas->subGraphNodes(),
                                        canvas->interfaceNodes(),
                                        canvas->savedLogicalConnections()});
                    for (const auto &[contextId, child] : *subGraphCanvases)
                    {
                        if (!child.isNull())
                            projects.push_back({child->sceneContext(), child->subGraphNodes(),
                                                child->interfaceNodes(),
                                                child->savedLogicalConnections()});
                    }
                    return projects;
				};
                saveEditorGraphs_ = [this, collectSubGraphs] {
                    editorGraphRecords_ = collectSubGraphs();
                };
                auto loadedModuleRecords = [this] {
                    std::vector<PatchbayProjectModule> modules = unloadedModuleRecords_;
                    modules.reserve(loadedModuleNodes_.size());
                    for (const auto &node : loadedModuleNodes_)
                        modules.push_back({ node->modulePath(), node->index(), node->clientId(), node->settingsJson(), node->contextId() });
					return modules;
                };
                auto syncModuleProjectState = [canvas, loadedModuleRecords, this] {
                    if (!moduleRecordsEqual(savedModuleRecords_, loadedModuleRecords()))
                        canvas->markProjectDirty();
                };
                auto syncSubGraphProjectState = [canvas, subGraphCanvases] {
                    for (const auto& [contextId, child] : *subGraphCanvases)
                    {
						if (!child.isNull() && child->isProjectDirty())
                        {
							canvas->markProjectDirty();
							return;
						}
					}
                };
                auto saveProject = [canvas, currentProject, loadedModuleRecords, collectSubGraphs, subGraphCanvases, this](bool saveAs, bool showError) {
					if (saveAs || currentProject->isEmpty()) {
						const auto path = PatchbayDialogs::projectFile(window_,true,*currentProject);
						if (path.isEmpty())
							return false;
						*currentProject = path.endsWith(QStringLiteral(".wjnproj"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".wjnproj");
					}
					const auto modules = loadedModuleRecords();
					if (canvas->saveProject(*currentProject, modules, collectSubGraphs())) {
						savedModuleRecords_ = modules;
						patchbaySettings().setValue(QStringLiteral("lastProjectPath"), *currentProject);
						canvas->markProjectSaved();
						for (const auto& [contextId, child] : *subGraphCanvases) {
							if (!child.isNull())
								child->markProjectSaved();
						}
						return true;
					}
					if (showError)
						PatchbayDialogs::message(window_,QMessageBox::Warning,"error.saveTitle","Save failed","error.save","Unable to save the patchbay project.");
					return false;
				};
				auto confirmSave = [canvas, saveProject, syncModuleProjectState, syncSubGraphProjectState, window = window_.data()] {
					syncModuleProjectState();
					syncSubGraphProjectState();
					if (!canvas->isProjectDirty())
						return true;
					const auto choice = PatchbayDialogs::message(window,QMessageBox::Warning,"project.unsavedTitle","Unsaved project","project.unsaved","The project has unsaved changes. Save them?",
						QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
					if (choice == QMessageBox::Save)
						return saveProject(false, true);
					return choice == QMessageBox::Discard;
				};
				static_cast<PatchbayWindow*>(window_.data())->setCloseConfirmation(confirmSave);
				auto* newProject = patchAction(fileMenu,"action.newProject","New project");
				auto* openProject = patchAction(fileMenu,"action.openProject","Open project...");
				auto* saveAction = patchAction(fileMenu,"action.saveProject","Save project");
				saveAction->setShortcut(QKeySequence::Save);
				auto* saveAsAction = patchAction(fileMenu,"action.saveProjectAs","Save project as...");
				QObject::connect(newProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, clearSubGraphs, refreshJackGraph, refreshNodeLibrary, refreshTrayNodeGuiEntries, selectedModuleNode, this] {
					if (!confirmSave())
						return;
					clearSubGraphs();
					canvas->newProject();
                    WinJACKNexus::SignalSwitch::Service::instance().reset();
					restoreMeterbridgeProjectState({});
					if (*selectedModuleNode != nullptr) {
						(*selectedModuleNode)->clearSettingsView();
						*selectedModuleNode = nullptr;
					}
					std::erase_if(loadedModuleNodes_, [](const auto& node) {
						return !isDigitalMixerModule(node->modulePath());
					});
					(*refreshTrayNodeGuiEntries)();
					if (jackObserver_.isOpen()) {
						auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
						ensureSystemClient(snapshot);
						canvas->setSnapshot(snapshot);
					}
					refreshJackGraph();
					(*refreshNodeLibrary)();
					savedModuleRecords_.clear();
                    unloadedModuleRecords_.clear();
                    currentProject->clear();
                    patchbaySettings().remove(QStringLiteral("lastProjectPath"));
				});
			auto projectMixerAction = std::make_shared<QPointer<QAction>>();
			const auto offerProjectMixer = [this, projectMixerAction](const std::vector<PatchbayProjectModule>& modules) {
				if (!*projectMixerAction || (*projectMixerAction)->isChecked()
					|| std::none_of(modules.cbegin(), modules.cend(), [](const auto& module) {
						return isDigitalMixerModule(module.modulePath);
					})) return;
				QMessageBox question(QMessageBox::Question, {}, {},
					QMessageBox::Yes | QMessageBox::No, window_);
				PatchbayDialogs::bind(&question,"windowTitle","project.enableMixerTitle","Enable mixer");
				PatchbayDialogs::bind(&question,"text","project.enableMixer","This project contains a mixer node. Enable the mixer?");
				question.setDefaultButton(QMessageBox::Yes);
				PatchbayDialogs::bind(question.button(QMessageBox::Yes),"text","action.enable","Enable");
				PatchbayDialogs::bind(question.button(QMessageBox::No),"text","action.notNow","Not now");
				if (question.exec() == QMessageBox::Yes)
					(*projectMixerAction)->setChecked(true);
			};
				QObject::connect(openProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, restoreModuleRecords, loadedModuleRecords, restoreSubGraphs, refreshJackGraph, offerProjectMixer, this] {
					const auto path = PatchbayDialogs::projectFile(window_,false,*currentProject);
					if (path.isEmpty() || !confirmSave())
						return;
					std::vector<PatchbayProjectModule> modules;
					std::vector<PatchbaySubGraphProject> subGraphs;
					if (canvas->loadProject(path, modules, subGraphs) && restoreSubGraphs(subGraphs)) {
						offerProjectMixer(modules);
						*currentProject = path;
						patchbaySettings().setValue(QStringLiteral("lastProjectPath"), path);
						WinJACKNexus::SignalSwitch::Service::instance().beginRestore();
                        restoreModuleRecords(modules);
                        if (!WinJACKNexus::SignalSwitch::Service::instance().restore(canvas->signalSwitchProjectState()))
                            qWarning() << "Signal Switch: invalid saved relation state; gates remain closed";
						restoreMeterbridgeProjectState(canvas->meterbridgeProjectState());
						savedModuleRecords_ = loadedModuleRecords();
						refreshJackGraph();
					} else {
						PatchbayDialogs::message(window_,QMessageBox::Warning,"error.openTitle","Open failed","error.open","Unable to read the patchbay project.");
					}
				});
				QObject::connect(saveAction, &QAction::triggered, window_, [saveProject] { saveProject(false, true); });
				QObject::connect(saveAsAction, &QAction::triggered, window_, [saveProject] { saveProject(true, true); });
				const auto savedInterval = patchbaySettings()
					.value(QStringLiteral("autoSaveIntervalSeconds"), 60).toInt();
				auto autoSaveInterval = std::make_shared<int>(std::clamp(savedInterval, 0, 3600));
				auto* autoSaveTimer = new QTimer(window_);
				auto applyAutoSaveInterval = [autoSaveTimer, autoSaveInterval](int seconds) {
					*autoSaveInterval = seconds;
					if (seconds == 0) {
						autoSaveTimer->stop();
						return;
					}
					autoSaveTimer->start(seconds * 1000);
				};
				applyAutoSaveInterval(*autoSaveInterval);
				QObject::connect(autoSaveTimer, &QTimer::timeout, window_, [canvas, currentProject, saveProject, syncModuleProjectState, syncSubGraphProjectState] {
					syncModuleProjectState();
						syncSubGraphProjectState();
					if (!currentProject->isEmpty() && canvas->isProjectDirty())
						saveProject(false, false);
				});
				auto* autoSaveAction = patchAction(fileMenu,"action.autosaveInterval","Autosave interval...");
				QObject::connect(autoSaveAction, &QAction::triggered, window_, [autoSaveInterval, applyAutoSaveInterval, window = window_.data()] {
					bool accepted = false;
					const auto seconds = PatchbayDialogs::integer(window,"project.autosaveTitle","Autosave","project.autosavePrompt","Save interval (seconds; 0 disables autosave)", *autoSaveInterval, 0, 3600, 5, &accepted);
					if (!accepted)
						return;
					patchbaySettings().setValue(QStringLiteral("autoSaveIntervalSeconds"), seconds);
					applyAutoSaveInterval(seconds);
				});
				fileMenu->addSeparator();
			auto* closeAction = patchAction(fileMenu,"action.close","Close window");
			closeAction->setShortcut(QKeySequence::Close);
			QObject::connect(closeAction, &QAction::triggered, window_, [window = window_.data()] {
				window->close();
			});
			auto* editMenu = patchMenu(window_->menuBar(),"menu.edit","Edit");
			auto* quickFileMenu = new QMenu(window_);
			for (auto* action : fileMenu->actions()) {
				if (action != closeAction) quickFileMenu->addAction(action);
			}
			WinJACKNexus::UI::TrayController::setQuickAccessFileMenu(quickFileMenu);
			patchAction(editMenu,"action.fitCanvas","Fit canvas", canvas, &PatchbayCanvas::zoomFit);
			patchAction(editMenu,"action.resetZoom","Reset zoom", canvas, &PatchbayCanvas::zoomReset);
			auto* viewMenu = patchMenu(window_->menuBar(),"menu.view","View");
			viewMenu->addAction(nodeDock->toggleViewAction());
			viewMenu->addAction(inspectorDock->toggleViewAction());
			auto* graphMenu = patchMenu(window_->menuBar(),"menu.graph","Graph");
			auto restoreLastProject = [this, canvas, currentProject, restoreSubGraphs, restoreModuleRecords, loadedModuleRecords, offerProjectMixer] {
				const auto lastProject = patchbaySettings().value(QStringLiteral("lastProjectPath")).toString();
				std::vector<PatchbayProjectModule> modules;
				std::vector<PatchbaySubGraphProject> subGraphs;
				if (!lastProject.isEmpty() && QFile::exists(lastProject) && canvas->loadProject(lastProject, modules, subGraphs)
					&& restoreSubGraphs(subGraphs)) {
					offerProjectMixer(modules);
					*currentProject = lastProject;
					WinJACKNexus::SignalSwitch::Service::instance().beginRestore();
                    restoreModuleRecords(modules);
                    if (!WinJACKNexus::SignalSwitch::Service::instance().restore(canvas->signalSwitchProjectState()))
                        qWarning() << "Signal Switch: invalid saved relation state; gates remain closed";
					restoreMeterbridgeProjectState(canvas->meterbridgeProjectState());
					savedModuleRecords_ = loadedModuleRecords();
					canvas->restoreJackConnections(jackObserver_);
				}
			};
			auto attachJackObserver = [this, refreshJackGraph, restoreLastProject] {
				if (offline_) return false;
				if (jackObserver_.isOpen())
					return true;
				if (!jackObserver_.open({ "WinJACKNexus-Patchbay", false, false, nullptr }))
					return false;
				graphRefreshPending_.store(false, std::memory_order_release);
				graphOrderCallbackRegistered_ = jackObserver_.setGraphOrderCallback(&PatchbayNode::markJackGraphChanged, this) == 0;
				jackObserver_.activate();
				if (graphRefreshTimer_ != nullptr)
					graphRefreshTimer_->setInterval(graphOrderCallbackRegistered_
						? 1000 / WinJACKNexus::UI::ApplicationSettings::instance().frameRate()
						: 1000);
				restoreLastProject();
				refreshJackGraph();
				return true;
			};
			graphRefreshTimer_ = new QTimer(window_);
			graphRefreshTimer_->setTimerType(Qt::PreciseTimer);
			graphRefreshTimer_->setInterval(1000);
			QObject::connect(&WinJACKNexus::UI::ApplicationSettings::instance(),
				&WinJACKNexus::UI::ApplicationSettings::frameRateChanged,
				window_, [this](int framesPerSecond) {
					if (graphRefreshTimer_ && graphOrderCallbackRegistered_)
						graphRefreshTimer_->setInterval(1000 / framesPerSecond);
				});
			QObject::connect(graphRefreshTimer_, &QTimer::timeout, window_, [this, attachJackObserver, refreshPendingJackGraph] {
				if (!jackObserver_.isOpen())
					attachJackObserver();
				refreshPendingJackGraph();
			});
			graphRefreshTimer_->start();

			patchAction(graphMenu,"action.refreshLibrary","Refresh node library", window_, [refreshNodeLibrary] {
				(*refreshNodeLibrary)();
			});
			auto* toolsMenu = patchMenu(window_->menuBar(),"menu.tools","Tools");
			auto* audioToolsMenu = patchMenu(toolsMenu,"menu.audioTools","Audio tools");
			auto* otherToolsMenu = patchMenu(toolsMenu,"menu.otherTools","Other tools");
			auto* meterbridgeAction = patchAction(audioToolsMenu,"action.meterbridge","Meter bridge");
			QObject::connect(meterbridgeAction, &QAction::triggered, window_, [window = window_.data()] {
				const auto module = GetModuleHandleW(L"mod_meterbrigde.dll");
				using ShowMeterbridge = int (*)(const char*);
				const auto show = module == nullptr ? nullptr
					: reinterpret_cast<ShowMeterbridge>(GetProcAddress(module, "wjn_meterbridge_show_for_instance"));
				if (show == nullptr || show(nullptr) == 0)
					PatchbayDialogs::message(window,QMessageBox::Warning,"action.meterbridge","Meter bridge","error.meterbridge","Unable to open the meter bridge at this time.");
			});
			auto* mixerEnabledAction = patchAction(audioToolsMenu,"action.mixer","Mixer");
			*projectMixerAction = mixerEnabledAction;
			mixerEnabledAction->setObjectName(QStringLiteral("patchbay.tools.mixerEnabled"));
			mixerEnabledAction->setCheckable(true);
			QObject::connect(mixerEnabledAction, &QAction::toggled, window_,
				[this, canvas, createModuleNode, mixerEnabledAction, refreshJackGraph, refreshTrayNodeGuiEntries](bool enabled) {
					if (enabled && !hasLoadedDigitalMixer()) {
						const auto modules = scanNodeModules();
						const auto mixer = std::find_if(modules.cbegin(), modules.cend(), [](const auto& node) {
							return isDigitalMixerModule(node.modulePath) && node.index == 0;
						});
						if (mixer == modules.cend() || !(*createModuleNode)(canvas, mixer->modulePath, mixer->index,
							canvas->mapToScene(canvas->viewport()->rect().center()), {})) {
							QSignalBlocker blocker(mixerEnabledAction);
							mixerEnabledAction->setChecked(false);
							return;
						}
					} else if (!enabled && removeLoadedDigitalMixer()) {
						(*refreshTrayNodeGuiEntries)();
						refreshJackGraph();
					}
					patchbaySettings().setValue(QStringLiteral("residentMixerEnabled"), enabled);
				});
			if (!offline_ && patchbaySettings().value(QStringLiteral("residentMixerEnabled"), false).toBool())
				mixerEnabledAction->setChecked(true);
			attachJackObserver();
			auto* themeManagerAction = patchAction(otherToolsMenu,"action.themeManager","Theme manager");
			QObject::connect(themeManagerAction, &QAction::triggered, window_, [window = window_.data()] {
				for (auto* widget : QApplication::topLevelWidgets()) {
					if (auto* dialog = dynamic_cast<WinJACKNexus::UI::ThemeManagementDialog*>(widget)) {
						dialog->show();
						dialog->raise();
						dialog->activateWindow();
						return;
					}
				}
				if (auto* core = WinJACKNexus::UI::ThemeManager::installedThemeCore()) {
					auto* dialog = new WinJACKNexus::UI::ThemeManagementDialog(*core, window);
					dialog->setAttribute(Qt::WA_DeleteOnClose);
					dialog->show();
				}
			});
			otherToolsMenu->addAction(scanVstAction);
			auto* helpMenu = patchMenu(window_->menuBar(),"menu.help","Help");
			patchAction(helpMenu,"action.logs","Open log folder", window_, [window = window_.data()] {
				WinJACKNexus::Common::Logger::instance().flush();
				const auto directory = QString::fromStdWString(
					WinJACKNexus::Common::PathUtils::logDirectory().wstring());
				WJN_LOG_INFO("Opening diagnostic log directory=%s", directory.toUtf8().constData());
				WinJACKNexus::Common::Logger::instance().flush();
				if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
					WJN_LOG_ERROR("Failed to open diagnostic log directory=%s", directory.toUtf8().constData());
					PatchbayDialogs::message(window,QMessageBox::Warning,"action.logs","Open log folder","error.logs","Unable to open the log folder.\n{directory}",QMessageBox::Ok,QMessageBox::NoButton,{{"directory",directory}});
				}
			});
			patchAction(helpMenu,"action.about","About", window_, [window = window_.data()] {
				PatchbayDialogs::message(window,QMessageBox::Information,"about.title","About WinJACKNexus","about.text","WinJACKNexus\nJACK audio routing and mixing workstation\nAGPL-3.0-or-later");
			});
            if (!editorGraphRecords_.empty())
            {
                for (const auto &record : editorGraphRecords_)
                    restoredSubGraphs->emplace(record.context.id, record);
                const auto main = restoredSubGraphs->find("main");
                if (main != restoredSubGraphs->end())
                    canvas->restoreSubGraphNodes(main->second.nodes);
                auto records = editorGraphRecords_;
                std::sort(records.begin(), records.end(), [](const auto &left, const auto &right) {
                    return left.context.id.size() < right.context.id.size();
                });
                for (const auto &record : records)
                {
                    if (record.context.id == "main")
                        continue;
                    const auto separator = record.context.id.rfind('/');
                    const auto parentId = record.context.id.substr(0, separator);
                    auto *parent =
                        parentId == "main" ? canvas : (*subGraphCanvases)[parentId].data();
                    if (parent)
                        (*openSubGraph)(parent, record.context.id.substr(separator + 1));
                    const auto child = subGraphCanvases->find(record.context.id);
                    if (child != subGraphCanvases->end() && child->second)
                        child->second->restoreLogicalConnections(record.connections);
                }
                restoredSubGraphs->clear();
                canvasTabs->setCurrentIndex(0);
                refreshJackGraph();
            }
        }

        window_->showNormal();
        window_->raise();
		window_->activateWindow();
		return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
	}

	void destroyWindow(HWND) noexcept override
	{
        WinJACKNexus::SignalSwitch::Service::instance().synchronize = {};
        WinJACKNexus::SignalSwitch::Service::instance().edited = {};
		if (window_ != nullptr) {
            if (saveEditorGraphs_)
                saveEditorGraphs_();
            saveEditorGraphs_ = {};
            // Releasing child language leases publishes a language update. Disconnect
            // this editor before any widget begins destroying its internal model.
            if (auto *manager = WinJACKNexus::UI::I18nManager::installed())
            {
                QObject::disconnect(manager, nullptr, window_, nullptr);
                for (auto *child : window_->findChildren<QObject *>())
                {
                    QObject::disconnect(manager, nullptr, child, nullptr);
                }
            }
            delete window_;
            window_ = nullptr;
        }
	}

private:
	bool offline_=false;
	static int markJackGraphChanged(void* context) noexcept
	{
		auto* node = static_cast<PatchbayNode*>(context);
		if (node != nullptr)
			node->graphRefreshPending_.store(true, std::memory_order_release);
		return 0;
	}

	std::vector<std::pair<std::string, std::string>> rebindLoadedModuleClients(
		const WinJACKNexus::Patchbay::Core::Snapshot& snapshot)
	{
		std::vector<std::pair<std::string, std::string>> reboundClients;
		std::set<std::string> liveClients;
		for (const auto& client : snapshot.clients) {
			if (client.id != "system" && client.id != "WinJACKNexus-Patchbay")
				liveClients.insert(client.id);
		}

		std::set<std::string> newClients;
		std::set_difference(liveClients.cbegin(), liveClients.cend(),
		                    observedJackClients_.cbegin(), observedJackClients_.cend(),
		                    std::inserter(newClients, newClients.end()));
		for (const auto& node : loadedModuleNodes_) {
            if (node->isVirtual() || (node->hasClient() && liveClients.contains(node->clientId())))
                continue;
            const auto candidate =
                std::find_if(newClients.cbegin(), newClients.cend(), [this](const auto &clientId) {
                    return std::none_of(
                        loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(),
                        [&clientId](const auto &node) { return node->ownsClient(clientId); });
                });
            if (candidate != newClients.cend()) {
				if (node->hasClient())
					reboundClients.emplace_back(node->clientId(), *candidate);
				node->setClientId(*candidate);
			}
		}
		observedJackClients_ = std::move(liveClients);
		return reboundClients;
	}

	bool initialized_ = false;
	QPointer<QMainWindow> window_;
	WinJACKNexus::Audio::JackClient jackObserver_;
	QPointer<QTimer> graphRefreshTimer_;
	std::atomic_bool graphRefreshPending_ { false };
	bool graphOrderCallbackRegistered_ = false;
	std::set<std::string> knownJackClients_ { "External Audio Client", "DAW Bridge" };
	std::set<std::string> observedJackClients_;
    std::shared_ptr<std::map<std::string, std::shared_ptr<SubGraphPortSet>>> subGraphPorts_ =
        std::make_shared<std::map<std::string, std::shared_ptr<SubGraphPortSet>>>();
    std::vector<std::unique_ptr<LoadedModuleNode>> loadedModuleNodes_;
    std::vector<PatchbayProjectModule> savedModuleRecords_;
    std::vector<PatchbayProjectModule> unloadedModuleRecords_;
    std::vector<PatchbaySubGraphProject> editorGraphRecords_;
    std::function<void()> saveEditorGraphs_;
    ModuleNodeInfo copiedModuleNode_;
};

}

extern "C" WINJACK_NODE_API int wjn_patchbay_show_digital_mixer_view(int view)
{
    return view<0||view>2?-1:livePatchbayNode==nullptr?0:livePatchbayNode->showLoadedDigitalMixer(view);
}

extern "C" WINJACK_NODE_API int wjn_patchbay_show_digital_mixer(void)
{
	return livePatchbayNode == nullptr ? 0 : livePatchbayNode->showLoadedDigitalMixer();
}

const char* wjn_get_node_api_version(void)
{
    return "4";
}

std::uint32_t wjn_get_node_theme_api_version(void)
{
	return WJN_NODE_THEME_API_VERSION;
}

const char *wjn_get_node_theme_namespace_json(void)
{
    static constexpr char descriptor[] = R"json({
		"descriptorVersion":1,"themeApiVersion":1,
		"moduleId":"module-winjacknexus-patchbay",
		"namespace":"--wjn-theme-module-winjacknexus-patchbay-",
		"displayName":"Patchbay",
		"tokens":[{
			"name":"--wjn-theme-module-winjacknexus-patchbay-node-card-background",
			"type":"color",
			"default":"var(--wjn-theme-patchbay-node-card-background)",
			"description":"Host-drawn card background for the Patchbay node type"
		}]
	})json";
    return descriptor;
}

const char *wjn_get_node_name(void)
{
    return "Patchbay";
}

const char *wjn_get_node_category(void)
{
    return "Patchbay";
}

WinJACKNexus::Node::INode *wjn_create_node(void)
{
    return new PatchbayNode;
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

#define WJN_NODE_MODULE_FEATURES WJN_NODE_HAS_GUI
#include <WinJACKNexus/Node/JackOnlyModuleV4.inc>
