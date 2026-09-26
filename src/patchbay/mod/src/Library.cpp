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
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackMidiBuffer.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/NativeWindowUtils.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>

#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QActionGroup>
#include <QCheckBox>
#include <QCoreApplication>
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
#include <QScrollArea>
#include <QTabWidget>
#include <QTreeWidget>
#include <QToolBar>
#include <QTimer>
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

namespace {

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
	category->setExpanded(true);
	return category;
}

QTreeWidgetItem* addCategory(QTreeWidgetItem* parent, const QString& name, bool expanded = true)
{
	auto* category = new QTreeWidgetItem(parent, { name });
	category->setExpanded(expanded);
	return category;
}

void captureExpandedCategories(const QTreeWidgetItem* parent, const QString& parentPath, QVariantMap& states)
{
	for (int index = 0; index < parent->childCount(); ++index) {
		const auto* item = parent->child(index);
		if (item->childCount() == 0)
			continue;
		const auto path = parentPath + QChar(0x1f) + item->text(0);
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
		const auto path = item->text(0);
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
		const auto path = parentPath + QChar(0x1f) + item->text(0);
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
		const auto path = item->text(0);
		if (const auto state = states.constFind(path); state != states.cend())
			item->setExpanded(state->toBool());
		restoreExpandedCategories(item, path, states);
	}
}

void addNodeItem(QTreeWidgetItem* category, const QString& name, bool online = true)
{
	auto* item = new QTreeWidgetItem(category, { online ? name : name + QStringLiteral("（离线）") });
	if (!online)
		item->setForeground(0, QColor(QStringLiteral("#8193a5")));
}

void addSubGraphItem(QTreeWidgetItem* category)
{
	auto* item = new QTreeWidgetItem(category, { QStringLiteral("Sub Graph") });
	item->setData(0, Qt::UserRole + 2, QStringLiteral("Sub Graph"));
}

void addInterfaceItem(QTreeWidgetItem* category, const QString& type, const QString& name)
{
	auto* item = new QTreeWidgetItem(category, { name });
	item->setData(0, Qt::UserRole + 3, type);
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
};

struct SubGraphPortSet final {
	WinJACKNexus::Audio::JackClient client;
	std::vector<SubGraphInterfacePort> ports;
	std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> routes;
	std::vector<PatchbayInterfaceInfo> interfaces;

	WinJACKNexus::Patchbay::Core::PortKind endpointKind(const std::string& id) const noexcept
	{
		const auto port = std::find_if(ports.cbegin(), ports.cend(), [&id](const auto& item) { return item.id == id; });
		if (port != ports.cend())
			return port->kind;
		const auto interfaceNode = std::find_if(interfaces.cbegin(), interfaces.cend(), [&id](const auto& item) { return item.portId == id; });
		return interfaceNode != interfaces.cend() ? interfaceNode->kind : WinJACKNexus::Patchbay::Core::PortKind::unknown;
	}

	bool reaches(const std::string& sourcePortId, const std::string& destinationPortId, std::size_t remainingHops) const noexcept
	{
		if (sourcePortId == destinationPortId)
			return true;
		if (remainingHops == 0)
			return false;
		for (const auto& route : routes) {
			if (route.sourcePortId == sourcePortId && reaches(route.destinationPortId, destinationPortId, remainingHops - 1))
				return true;
		}
		return false;
	}

	static int process(jack_nframes_t frames, void* userData) noexcept
	{
		auto* portSet = static_cast<SubGraphPortSet*>(userData);
		if (portSet == nullptr)
			return 0;

		for (auto& item : portSet->ports) {
			if (item.kind == WinJACKNexus::Patchbay::Core::PortKind::audio && !item.isInput) {
				if (auto* output = item.port.audioBuffer(frames); output != nullptr)
					std::memset(output, 0, static_cast<std::size_t>(frames) * sizeof(*output));
			} else if (item.kind == WinJACKNexus::Patchbay::Core::PortKind::midi && !item.isInput) {
				WinJACKNexus::Audio::JackMidiBuffer(item.port.midiBuffer(frames)).clear();
			}
		}

		for (const auto& source : portSet->ports) {
			if (!source.isInput)
				continue;
			for (auto& destination : portSet->ports) {
				if (destination.isInput || source.kind != destination.kind
					|| !portSet->reaches(source.id, destination.id, portSet->routes.size()))
					continue;
				if (source.kind == WinJACKNexus::Patchbay::Core::PortKind::audio) {
					const auto* input = source.port.audioBuffer(frames);
					auto* output = destination.port.audioBuffer(frames);
					if (input != nullptr && output != nullptr)
						std::memcpy(output, input, static_cast<std::size_t>(frames) * sizeof(*output));
				} else if (source.kind == WinJACKNexus::Patchbay::Core::PortKind::midi) {
					WinJACKNexus::Audio::JackMidiBuffer input(source.port.midiBuffer(frames));
					WinJACKNexus::Audio::JackMidiBuffer output(destination.port.midiBuffer(frames));
					for (std::uint32_t index = 0; index < input.eventCount(); ++index) {
						WinJACKNexus::Audio::JackMidiEventView event;
						if (input.event(index, event) && event.size != 0)
							output.write(event.time, event.data, event.size);
					}
				}
			}
		}
		return 0;
	}

	bool connect(const std::string& sourcePortId, const std::string& destinationPortId) noexcept
	{
		const auto sourceKind = endpointKind(sourcePortId);
		const auto destinationKind = endpointKind(destinationPortId);
		if (sourceKind == WinJACKNexus::Patchbay::Core::PortKind::unknown || sourceKind != destinationKind)
			return false;
		if (std::any_of(routes.cbegin(), routes.cend(), [&sourcePortId, &destinationPortId](const auto& route) {
				return route.sourcePortId == sourcePortId && route.destinationPortId == destinationPortId;
			}))
			return true;
		if (client.isActive() && !client.deactivate())
			return false;
		try {
			routes.push_back({ sourcePortId, destinationPortId });
		} catch (...) {
			client.activate();
			return false;
		}
		return client.activate();
	}

	bool disconnect(const std::string& sourcePortId, const std::string& destinationPortId) noexcept
	{
		const bool wasActive = client.isActive();
		if (wasActive && !client.deactivate())
			return false;
		const auto removed = std::erase_if(routes, [&sourcePortId, &destinationPortId](const auto& route) {
			return route.sourcePortId == sourcePortId && route.destinationPortId == destinationPortId;
		});
		return removed != 0 && (!wasActive || client.activate());
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

std::vector<ModuleNodeInfo> scanNodeModules()
{
	using ApiVersion = const char* (*)();
	using NodeCount = std::uint32_t (*)();
	using NodeName = const char* (*)(std::uint32_t index);
	using NodeCategory = const char* (*)(std::uint32_t index);

	std::vector<ModuleNodeInfo> nodes;
	const QDir modulesDirectory(QCoreApplication::applicationDirPath() + QStringLiteral("/nodes"));
	if (!modulesDirectory.exists())
		return nodes;

	QDirIterator iterator(modulesDirectory.absolutePath(), { QStringLiteral("*.dll") },
	                     QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		const auto modulePath = iterator.next();
		const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(modulePath.utf16()));
		if (library == nullptr)
			continue;

		const auto apiVersion = reinterpret_cast<ApiVersion>(GetProcAddress(library, "wjn_get_node_api_version"));
		const auto nodeCount = reinterpret_cast<NodeCount>(GetProcAddress(library, "wjn_get_node_count"));
		const auto nodeName = reinterpret_cast<NodeName>(GetProcAddress(library, "wjn_get_node_name"));
		const auto nodeCategory = reinterpret_cast<NodeCategory>(GetProcAddress(library, "wjn_get_node_category"));
		if (apiVersion != nullptr && nodeCount != nullptr && nodeName != nullptr
			&& std::strcmp(apiVersion(), "2") == 0) {
			const auto moduleDirectory = QFileInfo(modulePath).dir();
			auto category = modulesDirectory.relativeFilePath(moduleDirectory.absolutePath());
			if (category.isEmpty() || category == QStringLiteral("."))
				category = QStringLiteral("根目录");
			else
				category.replace(QDir::separator(), QStringLiteral(" / "));
			for (std::uint32_t index = 0; index < nodeCount(); ++index) {
				const auto* name = nodeName(index);
				if (name == nullptr || name[0] == '\0')
					continue;
				const auto* exportedCategory = nodeCategory == nullptr ? nullptr : nodeCategory(index);
				const auto nodeCategoryName = categoryFromModuleOrFolder(exportedCategory, category);
				nodes.push_back({ nodeCategoryName, QString::fromUtf8(name), modulePath, index });
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

void addVstPluginItems(QTreeWidget* tree, const std::vector<ModuleNodeInfo>& modules)
{
	const auto vstHost = std::find_if(modules.cbegin(), modules.cend(), [](const auto& module) {
		return module.name == QStringLiteral("VST Plugin");
	});
	if (vstHost == modules.cend())
		return;

	const QDir pluginsDirectory(QCoreApplication::applicationDirPath() + QStringLiteral("/nodes/VSTPlugins"));
	auto* vstCategory = addCategory(tree, QStringLiteral("VST Plugins"));
	vstCategory->setExpanded(false);
	std::map<QString, QTreeWidgetItem*> categories { { QStringLiteral("VST Plugins"), vstCategory } };
	for (const auto& plugin : scanVstPlugins()) {
		auto relativeDirectory = pluginsDirectory.relativeFilePath(QFileInfo(plugin.infoFile).absolutePath());
		if (relativeDirectory.isEmpty() || relativeDirectory == QStringLiteral(".")) {
			relativeDirectory = plugin.type == QStringLiteral("vst2") ? QStringLiteral("VST2") : QStringLiteral("VST3");
			relativeDirectory += plugin.instrument ? QStringLiteral("/Instruments") : QStringLiteral("/Effects");
		}
		QString categoryPath = QStringLiteral("VST Plugins");
		QTreeWidgetItem* category = vstCategory;
		for (const auto& segment : QDir::fromNativeSeparators(relativeDirectory).split('/', Qt::SkipEmptyParts)) {
			categoryPath += QChar(0x1f) + segment;
			auto [childCategory, inserted] = categories.try_emplace(categoryPath, nullptr);
			if (inserted)
				childCategory->second = addCategory(category, segment, false);
			category = childCategory->second;
		}
		auto* item = new QTreeWidgetItem(category, { plugin.name });
		item->setData(0, Qt::UserRole, vstHost->modulePath);
		item->setData(0, Qt::UserRole + 1, vstHost->index);
		item->setData(0, Qt::UserRole + 4, vstPluginSettings(plugin));
		item->setToolTip(0, QStringLiteral("%1\n%2\n%3 %4-bit\n%5")
			.arg(plugin.vendor, plugin.version, plugin.type.toUpper(), QString::number(plugin.bits), plugin.path));
		if (!plugin.verified)
			item->setForeground(0, QColor(QStringLiteral("#8193a5")));
	}
}

void populateNodeLibrary(QTreeWidget* tree,
                         const WinJACKNexus::Patchbay::Core::Snapshot& snapshot,
						 const std::set<std::string>& knownJackClients,
						 const std::set<std::string>& moduleClientIds,
			 const QString& filterQuery = {},
			 bool subGraphContext = false)
{
	const auto expandedCategories = captureExpandedCategories(tree);
	tree->clear();
	const auto modules = scanNodeModules();
	auto* subGraphs = addCategory(tree, QStringLiteral("Sub Graph"));
	addSubGraphItem(subGraphs);
	if (subGraphContext) {
		auto* interfaces = addCategory(tree, QStringLiteral("Sub Graph Interface"));
		addInterfaceItem(interfaces, QStringLiteral("audio_in"), QStringLiteral("Sub Graph Audio In"));
		addInterfaceItem(interfaces, QStringLiteral("audio_out"), QStringLiteral("Sub Graph Audio Out"));
		addInterfaceItem(interfaces, QStringLiteral("midi_in"), QStringLiteral("Sub Graph MIDI In"));
		addInterfaceItem(interfaces, QStringLiteral("midi_out"), QStringLiteral("Sub Graph MIDI Out"));
		addVstPluginItems(tree, modules);
		applyNodeLibraryFilter(tree, filterQuery);
		if (filterQuery.trimmed().isEmpty())
			restoreExpandedCategories(tree, expandedCategories);
		return;
	}

	auto* jackClients = addCategory(tree, QStringLiteral("jackclient"));
	std::set<std::string> onlineClients;
	std::set<std::string> moduleClientNames;
	const std::set<std::string> builtInClients { "system", "WinJACKNexus-Patchbay" };
	for (const auto& client : snapshot.clients)
	{
		if (moduleClientIds.contains(client.id)) {
			moduleClientNames.insert(client.name);
			continue;
		}
		if (client.id.starts_with("placeholder-") || builtInClients.contains(client.name))
			continue;
		addNodeItem(jackClients, QString::fromStdString(client.name), client.alive);
		onlineClients.insert(client.name);
	}
	for (const auto& client : knownJackClients) {
		if (!onlineClients.contains(client) && !moduleClientNames.contains(client))
			addNodeItem(jackClients, QString::fromStdString(client), false);
	}

	std::map<QString, QTreeWidgetItem*> moduleCategories;
	for (const auto& node : modules) {
		auto [category, inserted] = moduleCategories.try_emplace(node.category, nullptr);
		if (inserted)
			category->second = addCategory(tree, node.category);
		auto* item = new QTreeWidgetItem(category->second, { node.name });
		item->setData(0, Qt::UserRole, node.modulePath);
		item->setData(0, Qt::UserRole + 1, node.index);
	}
	addVstPluginItems(tree, modules);
	applyNodeLibraryFilter(tree, filterQuery);
	if (filterQuery.trimmed().isEmpty())
		restoreExpandedCategories(tree, expandedCategories);
}

class LoadedModuleNode final {
public:
	static std::unique_ptr<LoadedModuleNode> create(const QString& modulePath,
	                                               std::uint32_t index,
	                                               const std::string& settingsJson = {}) noexcept
	{
		using ApiVersion = const char* (*)();
		using NodeCount = std::uint32_t (*)();
		using CreateNode = WinJACKNexus::Node::INode* (*)(std::uint32_t index);
		using DestroyNode = void (*)(WinJACKNexus::Node::INode* node);
		using SetNodeSettingsJson = int (*)(WinJACKNexus::Node::INode* node, const char* json);
		using PrepareNodeForCanvas = int (*)(WinJACKNexus::Node::INode* node);
		using ShowNodeGui = int (*)(WinJACKNexus::Node::INode* node);
		using GetNodeClientId = const char* (*)(WinJACKNexus::Node::INode* node);
			using CreateSettingsWidget = QWidget* (*)(WinJACKNexus::Node::INode* node, QWidget* parent);
			using DestroySettingsWidget = void (*)(WinJACKNexus::Node::INode* node, QWidget* widget);
			using CreateSettingsView = void* (*)(WinJACKNexus::Node::INode* node, void* parentHwnd);
			using DestroySettingsView = void (*)(WinJACKNexus::Node::INode* node, void* viewHwnd);

		const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(modulePath.utf16()));
		if (library == nullptr)
			return nullptr;
		const auto apiVersion = reinterpret_cast<ApiVersion>(GetProcAddress(library, "wjn_get_node_api_version"));
		const auto nodeCount = reinterpret_cast<NodeCount>(GetProcAddress(library, "wjn_get_node_count"));
		const auto createNode = reinterpret_cast<CreateNode>(GetProcAddress(library, "wjn_create_node"));
		const auto destroyNode = reinterpret_cast<DestroyNode>(GetProcAddress(library, "wjn_destroy_node"));
		const auto setNodeSettingsJson = reinterpret_cast<SetNodeSettingsJson>(GetProcAddress(library, "wjn_set_node_settings_json"));
		const auto prepareNodeForCanvas = reinterpret_cast<PrepareNodeForCanvas>(GetProcAddress(library, "wjn_prepare_node_for_canvas"));
		const auto showNodeGui = reinterpret_cast<ShowNodeGui>(GetProcAddress(library, "wjn_show_node_gui"));
		const auto getNodeClientId = reinterpret_cast<GetNodeClientId>(GetProcAddress(library, "wjn_get_node_client_id"));
			const auto createSettingsWidget = reinterpret_cast<CreateSettingsWidget>(GetProcAddress(library, "wjn_create_settings_widget"));
			const auto destroySettingsWidget = reinterpret_cast<DestroySettingsWidget>(GetProcAddress(library, "wjn_destroy_settings_widget"));
			const auto createSettingsView = reinterpret_cast<CreateSettingsView>(GetProcAddress(library, "wjn_create_settings_view"));
			const auto destroySettingsView = reinterpret_cast<DestroySettingsView>(GetProcAddress(library, "wjn_destroy_settings_view"));
		if (apiVersion == nullptr || nodeCount == nullptr || createNode == nullptr || destroyNode == nullptr
			|| std::strcmp(apiVersion(), "2") != 0 || index >= nodeCount()) {
			FreeLibrary(library);
			return nullptr;
		}

		auto* node = createNode(index);
		if (node == nullptr) {
			FreeLibrary(library);
			return nullptr;
		}
		if (setNodeSettingsJson != nullptr && !settingsJson.empty()
			&& setNodeSettingsJson(node, settingsJson.c_str()) == 0) {
			destroyNode(node);
			FreeLibrary(library);
			return nullptr;
		}
		if (prepareNodeForCanvas != nullptr && prepareNodeForCanvas(node) == 0) {
			destroyNode(node);
			FreeLibrary(library);
			return nullptr;
		}
		if (!node->init()) {
			if (node != nullptr)
				destroyNode(node);
			FreeLibrary(library);
			return nullptr;
		}
		const auto expectedClientId = getNodeClientId == nullptr ? nullptr : getNodeClientId(node);
		const auto info = node->info();
		QString category;
		for (const auto& module : scanNodeModules()) {
			if (module.modulePath == modulePath && module.index == index) {
				category = module.category;
				break;
			}
		}
			return std::unique_ptr<LoadedModuleNode>(new LoadedModuleNode(library,
			                                                            node,
			                                                            destroyNode,
				                                                            createSettingsWidget,
				                                                            destroySettingsWidget,
			                                                            createSettingsView,
			                                                            destroySettingsView,
			                                                            showNodeGui,
				                                                            expectedClientId == nullptr ? std::string {} : std::string(expectedClientId),
			                                                            modulePath,
			                                                            index,
			                                                            category,
			                                                            QString::fromUtf8(info.name)));
	}

	~LoadedModuleNode()
	{
			clearSettingsView();
		node_->destroyWindow(nullptr);
		node_->shutdown();
		destroyNode_(node_);
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
			return showNodeGui_ != nullptr;
		}

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

	void setClientId(std::string clientId)
	{
		clientId_ = std::move(clientId);
	}

		const std::string& clientId() const noexcept
		{
			if (getNodeClientId_ != nullptr) {
				try {
					const auto* current = getNodeClientId_(node_);
					if (current != nullptr && clientId_ != current)
						clientId_ = current;
				} catch (...) {
				}
			}
			return clientId_;
		}

		void setContextId(std::string contextId)
		{
			contextId_ = std::move(contextId);
		}

		const std::string& contextId() const noexcept
		{
			return contextId_;
		}

		bool hasClient() const noexcept
		{
			return !clientId().empty();
		}

	bool ownsClient(const std::string& clientId) const noexcept
	{
		return this->clientId() == clientId;
	}

	bool showGui(QWidget* parent) noexcept
	{
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
			if (destroySettingsWidget_ != nullptr)
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

		LoadedModuleNode(HMODULE library,
		                 WinJACKNexus::Node::INode* node,
		                 DestroyNode destroyNode,
			                 CreateSettingsWidget createSettingsWidget,
			                 DestroySettingsWidget destroySettingsWidget,
		                 CreateSettingsView createSettingsView,
		                 DestroySettingsView destroySettingsView,
		                 ShowNodeGui showNodeGui,
						 std::string clientId,
		                 const QString& modulePath,
		                 std::uint32_t index,
		                 const QString& category,
		                 const QString& name) noexcept
		: library_(library)
		, node_(node)
		, destroyNode_(destroyNode)
		, createSettingsWidget_(createSettingsWidget)
		, destroySettingsWidget_(destroySettingsWidget)
			, createSettingsView_(createSettingsView)
			, destroySettingsView_(destroySettingsView)
			, showNodeGui_(showNodeGui)
			, clientId_(std::move(clientId))
			, getNodeClientId_(reinterpret_cast<GetNodeClientId>(GetProcAddress(library, "wjn_get_node_client_id")))
			, getNodeSettingsJson_(reinterpret_cast<GetNodeSettingsJson>(GetProcAddress(library, "wjn_get_node_settings_json")))
		, modulePath_(modulePath)
		, index_(index)
			, category_(category)
			, name_(name)
	{
	}

	HMODULE library_ = nullptr;
	WinJACKNexus::Node::INode* node_ = nullptr;
	DestroyNode destroyNode_ = nullptr;
		CreateSettingsWidget createSettingsWidget_ = nullptr;
		DestroySettingsWidget destroySettingsWidget_ = nullptr;
		CreateSettingsView createSettingsView_ = nullptr;
		DestroySettingsView destroySettingsView_ = nullptr;
		ShowNodeGui showNodeGui_ = nullptr;
		GetNodeSettingsJson getNodeSettingsJson_ = nullptr;
	QString modulePath_;
	std::uint32_t index_ = 0;
	QString category_;
	QString name_;
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

	int showLoadedDigitalMixer() noexcept
	{
		const auto found = std::find_if(loadedModuleNodes_.begin(), loadedModuleNodes_.end(), [](const auto& node) {
			return isDigitalMixerModule(node->modulePath()) && node->index() == 0;
		});
		if (found == loadedModuleNodes_.end()) return 0;
		return (*found)->showGui(nullptr) ? 1 : -1;
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
		return true;
	}

	void shutdown() noexcept override
	{
		if (livePatchbayNode == this) livePatchbayNode = nullptr;
		destroyWindow(nullptr);
		WinJACKNexus::UI::TrayController::clearNodeGuiEntries();
		loadedModuleNodes_.clear();
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
			window_->setWindowTitle(QStringLiteral("WinJACKNexus - 接线盘"));
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
			canvas->setConnectionConnectHandler(connectJackPort);
			canvas->setConnectionDisconnectHandler([this](const std::string& source, const std::string& destination) {
				return jackObserver_.isOpen()
					&& WinJACKNexus::Audio::JackApi::instance().disconnect(jackObserver_.nativeHandle(), source.c_str(), destination.c_str()) == 0;
			});
			auto* canvasTabs = new WinJACKNexus::UI::Controls::TabField(window_);
			canvasTabs->setTabsClosable(true);
			canvasTabs->addTab(canvas, QStringLiteral("主图"));
			window_->setCentralWidget(canvasTabs);
			window_->setDockNestingEnabled(true);

			auto* nodeDock = new WinJACKNexus::UI::Controls::DockPanel(
				QStringLiteral("节点"), Qt::LeftDockWidgetArea, window_);
			nodeDock->setObjectName(QStringLiteral("patchbay.nodeLibrary"));
			auto* nodePanel = new QWidget(nodeDock);
			auto* nodeLayout = new QVBoxLayout(nodePanel);
			// The dock's 24 px resize grip overlays its canvas-facing edge.
			nodeLayout->setContentsMargins(8, 8, 32, 8);
			auto* nodeFilter = new WinJACKNexus::UI::Controls::SearchField(nodePanel);
			nodeFilter->setPlaceholderText(QStringLiteral("筛选节点"));
			nodeFilter->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			auto* nodeTree = new WinJACKNexus::UI::Controls::DragTreeWidget(nodePanel);
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
			auto subGraphPorts = std::make_shared<std::map<std::string, std::shared_ptr<SubGraphPortSet>>>();
			auto restoredSubGraphs = std::make_shared<std::map<std::string, PatchbaySubGraphProject>>();
			auto refreshNodeLibrary = std::make_shared<std::function<void()>>();
			*refreshNodeLibrary = [canvasTabs, nodeTree, loadedModuleClientIds, this] {
				auto* activeCanvas = dynamic_cast<PatchbayCanvas*>(canvasTabs->currentWidget());
				if (activeCanvas == nullptr)
					return;
				std::set<std::string> moduleClientIds;
				for (const auto& node : loadedModuleNodes_)
					if (node->hasClient())
						moduleClientIds.insert(node->clientId());
				populateNodeLibrary(nodeTree, activeCanvas->snapshot(), knownJackClients_, moduleClientIds,
				                   nodeTree->property("filterQuery").toString(), activeCanvas->sceneContext().isSubGraph);
			};
			auto refreshTrayNodeGuiEntries = std::make_shared<std::function<void()>>();
			*refreshTrayNodeGuiEntries = [this] {
				std::vector<WinJACKNexus::UI::TrayNodeGuiEntry> entries;
				bool hasMeterProbe = false;
				for (const auto& node : loadedModuleNodes_) {
					if (!node->hasCustomGui())
						continue;
					if (node->category() == QStringLiteral("Meter")) {
						hasMeterProbe = true;
						continue;
					}
					auto* nodePointer = node.get();
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
						QStringLiteral("电平表桥"),
						QStringLiteral("电平表桥"),
						[] {
							const auto module = GetModuleHandleW(L"mod_meterbrigde.dll");
							using ShowMeterbridge = int (*)(const char*);
							const auto show = module == nullptr ? nullptr
								: reinterpret_cast<ShowMeterbridge>(GetProcAddress(module, "wjn_meterbridge_show_for_instance"));
							return show != nullptr && show(nullptr) != 0;
						}
					});
				}
				WinJACKNexus::UI::TrayController::setNodeGuiEntries(std::move(entries));
			};
			(*refreshNodeLibrary)();
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
					canvasTabs->setTabText(tabIndex, name.isEmpty() ? QStringLiteral("Sub Graph") : name);
			};
			auto removeSubGraph = std::make_shared<std::function<bool(PatchbayCanvas*, const std::string&)>>();
			*removeSubGraph = [canvasTabs, subGraphCanvases, subGraphPorts](PatchbayCanvas* parentCanvas, const std::string& subGraphId) {
				if (parentCanvas == nullptr)
					return false;
				const auto contextId = parentCanvas->sceneContext().id + "/" + subGraphId;
				const auto rootContextId = rootSubGraphContext(contextId);
				const auto belongsToSubTree = [&contextId](const std::string& value) {
					return value == contextId || value.starts_with(contextId + "/");
				};
				std::unordered_set<std::string> removedPortIds;
				for (const auto& [id, portSet] : *subGraphPorts) {
					if (!belongsToSubTree(id) || portSet == nullptr)
						continue;
					for (const auto& interfaceNode : portSet->interfaces)
						removedPortIds.insert(interfaceNode.portId);
				}
				const auto root = subGraphPorts->find(rootContextId);
				if (root != subGraphPorts->end() && root->second != nullptr) {
					auto& portSet = *root->second;
					const bool wasActive = portSet.client.isActive();
					if (wasActive && !portSet.client.deactivate())
						return false;
					std::erase_if(portSet.routes, [&removedPortIds](const auto& route) {
						return removedPortIds.contains(route.sourcePortId) || removedPortIds.contains(route.destinationPortId);
					});
					if (contextId == rootContextId) {
						for (auto& port : portSet.ports)
							port.port.unregisterPort();
						portSet.ports.clear();
						portSet.interfaces.clear();
						portSet.client.close();
					} else {
						std::erase_if(portSet.interfaces, [&removedPortIds](const auto& interfaceNode) {
							return removedPortIds.contains(interfaceNode.portId);
						});
						if (wasActive)
							portSet.client.activate();
					}
				}
				for (auto iterator = subGraphCanvases->begin(); iterator != subGraphCanvases->end();) {
					if (!belongsToSubTree(iterator->first)) {
						++iterator;
						continue;
					}
					if (!iterator->second.isNull()) {
						const auto tabIndex = canvasTabs->indexOf(iterator->second.data());
						if (tabIndex >= 0)
							canvasTabs->removeTab(tabIndex);
						iterator->second->deleteLater();
					}
					iterator = subGraphCanvases->erase(iterator);
				}
				for (auto iterator = subGraphPorts->begin(); iterator != subGraphPorts->end();) {
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
			using CreateModuleNode = std::function<bool(PatchbayCanvas*, const QString&, std::uint32_t, const QPointF&, const QString&)>;
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
							canvasTabs->setCurrentIndex(canvasTabs->addTab(existing->second.data(), title.isEmpty() ? QStringLiteral("Sub Graph") : title));
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
				childCanvas->setConnectionConnectHandler([subGraphPorts, rootContextId](const std::string& source, const std::string& destination) {
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
				childCanvas->setInterfaceNodeAddedHandler([subGraphPorts, contextId, rootContextId, isTopLevel, parentCanvasGuard, subGraphId](PatchbayInterfaceInfo& interfaceNode) {
					const auto definition = std::find_if(subGraphInterfaceDefinitions().cbegin(), subGraphInterfaceDefinitions().cend(), [&interfaceNode](const auto& value) {
						return interfaceNode.type == QString::fromLatin1(value.type);
					});
					if (definition == subGraphInterfaceDefinitions().cend())
						return false;
					auto& portSet = (*subGraphPorts)[contextId];
					if (!isTopLevel) {
						if (portSet == nullptr)
							portSet = std::make_shared<SubGraphPortSet>();
						const auto portName = std::string(definition->type) + "_" + std::to_string(portSet->interfaces.size() + 1);
						interfaceNode.portId = contextId + ":" + portName;
						interfaceNode.externalPortId = interfaceNode.portId;
						portSet->interfaces.push_back(interfaceNode);
						auto& rootPortSet = (*subGraphPorts)[rootContextId];
						if (rootPortSet == nullptr)
							rootPortSet = std::make_shared<SubGraphPortSet>();
						rootPortSet->interfaces.push_back(interfaceNode);
						if (!parentCanvasGuard.isNull())
							parentCanvasGuard->setSubGraphInterfaces(subGraphId, portSet->interfaces);
						return true;
					}
					if (portSet == nullptr)
						portSet = std::make_shared<SubGraphPortSet>();
					if (!portSet->client.isOpen()) {
						const auto clientName = "WinJACKNexus-SubGraph-" + jackSafeToken(contextId);
						if (!portSet->client.open({ clientName.c_str(), false, false, nullptr })
							|| portSet->client.setProcessCallback(&SubGraphPortSet::process, portSet.get()) != 0) {
							portSet = nullptr;
							return false;
						}
					}
					const bool wasActive = portSet->client.isActive();
					if (wasActive && !portSet->client.deactivate())
						return false;

					const auto portName = interfaceNode.name.toUtf8().toStdString();
					portSet->ports.emplace_back();
					auto& port = portSet->ports.back();
					port.kind = interfaceNode.kind;
					port.isInput = definition->flags == JackPortIsInput;
					if (!port.port.registerPort(portSet->client, portName.c_str(), definition->jackType, definition->flags)) {
						portSet->ports.pop_back();
						if (wasActive)
							portSet->client.activate();
						return false;
					}
					const auto* registeredPortName = port.port.name();
					if (registeredPortName == nullptr || *registeredPortName == '\0') {
						portSet->ports.pop_back();
						if (wasActive)
							portSet->client.activate();
						return false;
					}
					port.id = registeredPortName;
					interfaceNode.portId = port.id;
					interfaceNode.externalPortId = port.id;
					if (!portSet->client.activate())
						return false;
					portSet->interfaces.push_back(interfaceNode);
					if (!parentCanvasGuard.isNull())
						parentCanvasGuard->setSubGraphInterfaces(subGraphId, portSet->interfaces);
					return true;
				});
				childCanvas->setInterfaceNodeRenamedHandler([subGraphPorts, contextId, rootContextId, isTopLevel, parentCanvasGuard, subGraphId](const std::string& interfaceId, const QString& name) {
					const auto portSet = subGraphPorts->find(contextId);
					if (portSet == subGraphPorts->end() || portSet->second == nullptr)
						return;
					const auto interfaceNode = std::find_if(portSet->second->interfaces.begin(), portSet->second->interfaces.end(), [&interfaceId](const auto& value) {
						return value.id == interfaceId;
					});
					if (interfaceNode != portSet->second->interfaces.end()) {
						const auto oldPortId = interfaceNode->portId;
						if (isTopLevel) {
							const auto port = std::find_if(portSet->second->ports.begin(), portSet->second->ports.end(), [&oldPortId](const auto& value) {
								return value.id == oldPortId;
							});
							const auto portName = name.toUtf8().toStdString();
							if (port != portSet->second->ports.end()
								&& WinJACKNexus::Audio::JackApi::instance().portRename(portSet->second->client.nativeHandle(), port->port.nativeHandle(), portName.c_str()) == 0) {
								const auto* renamedPortId = port->port.name();
								if (renamedPortId != nullptr) {
									port->id = renamedPortId;
									interfaceNode->portId = port->id;
									interfaceNode->externalPortId = port->id;
									for (auto& route : portSet->second->routes) {
										if (route.sourcePortId == oldPortId)
											route.sourcePortId = port->id;
										if (route.destinationPortId == oldPortId)
											route.destinationPortId = port->id;
									}
								}
							}
						}
						interfaceNode->name = name;
						if (!isTopLevel) {
							if (const auto rootPortSet = subGraphPorts->find(rootContextId); rootPortSet != subGraphPorts->end() && rootPortSet->second != nullptr) {
								const auto rootInterface = std::find_if(rootPortSet->second->interfaces.begin(), rootPortSet->second->interfaces.end(), [&interfaceId](const auto& value) {
									return value.id == interfaceId;
								});
								if (rootInterface != rootPortSet->second->interfaces.end())
									rootInterface->name = name;
							}
						}
						if (!parentCanvasGuard.isNull())
							parentCanvasGuard->setSubGraphInterfaces(subGraphId, portSet->second->interfaces);
					}
				});
				childCanvas->setInterfaceNodeRemovedHandler([subGraphPorts, contextId, rootContextId, isTopLevel, parentCanvasGuard, subGraphId](const std::string& interfaceId) {
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
					if (isTopLevel) {
						const bool wasActive = portSet.client.isActive();
						if (wasActive && !portSet.client.deactivate())
							return false;
						const auto port = std::find_if(portSet.ports.begin(), portSet.ports.end(), [&portId](const auto& value) {
							return value.id == portId;
						});
						if (port != portSet.ports.end()) {
							port->port.unregisterPort();
							portSet.ports.erase(port);
						}
						std::erase_if(portSet.routes, [&portId](const auto& route) {
							return route.sourcePortId == portId || route.destinationPortId == portId;
						});
						if (wasActive)
							portSet.client.activate();
					}
					portSet.interfaces.erase(interfaceNode);
					if (!isTopLevel) {
						const auto rootPortSet = subGraphPorts->find(rootContextId);
						if (rootPortSet != subGraphPorts->end() && rootPortSet->second != nullptr) {
							std::erase_if(rootPortSet->second->interfaces, [&interfaceId](const auto& value) {
								return value.id == interfaceId;
							});
							std::erase_if(rootPortSet->second->routes, [&portId](const auto& route) {
								return route.sourcePortId == portId || route.destinationPortId == portId;
							});
						}
					}
					if (!parentCanvasGuard.isNull())
						parentCanvasGuard->setSubGraphInterfaces(subGraphId, portSet.interfaces);
					return true;
				});
				childCanvas->setSubGraphNodeDoubleClickHandler([openSubGraphWeak, childCanvasGuard](const std::string& childId) {
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
					QTimer::singleShot(0, childCanvasGuard.data(), [canvasTabs, openSubGraphWeak, childCanvasGuard, childId, activeTab] {
						if (const auto openSubGraph = openSubGraphWeak.lock(); openSubGraph != nullptr && !childCanvasGuard.isNull()) {
							(*openSubGraph)(childCanvasGuard.data(), childId);
							if (activeTab >= 0)
								canvasTabs->setCurrentIndex(activeTab);
						}
					});
				});
				childCanvas->setInterfaceNodeDropHandler([childCanvas](const QString& type, const QPointF& position) {
					return childCanvas->addInterfaceNode(type, position);
				});
				if (jackObserver_.isOpen())
					childCanvas->setSnapshot(WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_));
				const auto restoredSubGraph = restoredSubGraphs->find(contextId);
				if (restoredSubGraph != restoredSubGraphs->end()) {
					childCanvas->restoreSubGraphNodes(restoredSubGraph->second.nodes);
					if (!childCanvas->restoreInterfaceNodes(restoredSubGraph->second.interfaces)) {
						childCanvas->deleteLater();
						return;
					}
				} else if (const auto savedPorts = subGraphPorts->find(contextId);
				           savedPorts != subGraphPorts->end() && savedPorts->second != nullptr) {
					std::vector<PatchbayInterfaceInfo> contextInterfaces;
					const auto interfacePrefix = contextId + "/interface-";
					std::copy_if(savedPorts->second->interfaces.cbegin(), savedPorts->second->interfaces.cend(),
					             std::back_inserter(contextInterfaces), [&interfacePrefix](const auto& interfaceNode) {
						return interfaceNode.id.starts_with(interfacePrefix);
					             });
					childCanvas->setInterfaceNodes(contextInterfaces);
					parentCanvas->setSubGraphInterfaces(subGraphId, std::move(contextInterfaces));
				}
				if (createDefaultInterfaces) {
					childCanvas->addInterfaceNode(QStringLiteral("audio_in"), {});
					childCanvas->addInterfaceNode(QStringLiteral("audio_in"), {});
					childCanvas->addInterfaceNode(QStringLiteral("audio_out"), {});
					childCanvas->addInterfaceNode(QStringLiteral("audio_out"), {});
					childCanvas->addInterfaceNode(QStringLiteral("midi_in"), {});
				}
				const auto tabTitle = context.title.isEmpty() ? QStringLiteral("Sub Graph") : context.title;
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
				for (const auto* project : contexts) {
					const auto child = subGraphCanvases->find(project->context.id);
					if (child == subGraphCanvases->end() || child->second.isNull()
						|| !child->second->restoreLogicalConnections(project->connections)) {
						return false;
					}
				}
				restoredSubGraphs->clear();
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
				QTimer::singleShot(0, canvasGuard.data(), [canvasTabs, openSubGraphWeak, canvasGuard, subGraphId, activeTab] {
					if (const auto openSubGraph = openSubGraphWeak.lock(); openSubGraph != nullptr && !canvasGuard.isNull()) {
						(*openSubGraph)(canvasGuard.data(), subGraphId);
						if (activeTab >= 0)
							canvasTabs->setCurrentIndex(activeTab);
					}
				});
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
				for (const auto& client : snapshot.clients)
					knownJackClients_.insert(client.name);
				std::unordered_set<std::string> childModuleClients;
				std::unordered_map<std::string, std::unordered_set<std::string>> modulesByContext;
				for (const auto& node : loadedModuleNodes_) {
					if (!node->hasClient() || node->contextId() == "main")
						continue;
					childModuleClients.insert(node->clientId());
					modulesByContext[node->contextId()].insert(node->clientId());
				}
				canvas->setHiddenModuleClientIds(std::move(childModuleClients));
				canvas->setSnapshot(snapshot);
				canvas->restoreJackConnections(jackObserver_);
				for (const auto& [contextId, child] : *subGraphCanvases) {
					if (!child.isNull()) {
						child->setVisibleModuleClientIds(modulesByContext[contextId]);
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
				if (!jackObserver_.isOpen() || !canRefreshJackGraph())
					return false;
				auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
				ensureSystemClient(snapshot);
				return applyJackSnapshot(std::move(snapshot));
			};
			auto pollJackGraph = [this, canRefreshJackGraph, applyJackSnapshot, lastObservedJackGraph, hasObservedJackGraph] {
				if (!jackObserver_.isOpen() || !canRefreshJackGraph())
					return false;
				auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
				ensureSystemClient(snapshot);
				if (*hasObservedJackGraph && hasSameObservedJackGraph(*lastObservedJackGraph, snapshot))
					return true;
				return applyJackSnapshot(std::move(snapshot));
			};
			auto refreshPendingJackGraph = [this, refreshJackGraph, pollJackGraph] {
				if (!graphOrderCallbackRegistered_) {
					pollJackGraph();
					return;
				}
				if (graphRefreshPending_.exchange(false, std::memory_order_acq_rel)
					&& !refreshJackGraph()) {
					graphRefreshPending_.store(true, std::memory_order_release);
				}
			};
			auto restoreModuleRecords = [this, refreshTrayNodeGuiEntries](const std::vector<PatchbayProjectModule>& modules) {
				loadedModuleNodes_.clear();
				for (const auto& module : modules) {
					if (isDigitalMixerModule(module.modulePath)
						&& hasLoadedDigitalMixer()) continue;
						std::set<std::string> existingClients;
						if (jackObserver_.isOpen()) {
							for (const auto& client : WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_).clients)
								existingClients.insert(client.id);
						}
				auto node = LoadedModuleNode::create(module.modulePath, module.index, module.settingsJson);
						if (node == nullptr)
							continue;
						if (jackObserver_.isOpen()) {
							const auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
							const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [&node, &existingClients](const auto& value) {
								return node->hasClient() ? value.id == node->clientId() : !existingClients.contains(value.id);
							});
							if (client != snapshot.clients.cend())
								node->setClientId(client->id);
						}
						if (!module.clientId.empty() && !node->hasClient())
							node->setClientId(module.clientId);
						node->setContextId(module.contextId);
						loadedModuleNodes_.push_back(std::move(node));
					}
					(*refreshTrayNodeGuiEntries)();
				};
			*createModuleNode = [this, refreshJackGraph, refreshTrayNodeGuiEntries, window = window_.data()](PatchbayCanvas* targetCanvas, const QString& modulePath, std::uint32_t index, const QPointF& position, const QString& settingsJson) {
			if (targetCanvas == nullptr)
				return false;
			if (isDigitalMixerModule(modulePath) && hasLoadedDigitalMixer())
				return showLoadedDigitalMixer() > 0;
				std::set<std::string> existingClients;
				if (jackObserver_.isOpen()) {
					for (const auto& client : WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_).clients)
						existingClients.insert(client.id);
				}

				auto node = LoadedModuleNode::create(modulePath, index, settingsJson.toStdString());
				if (node == nullptr) {
					QMessageBox::warning(window, QStringLiteral("节点启动失败"),
					                     QStringLiteral("无法初始化所选模块节点。请确认 JACK 服务和对应系统设备可用。"));
					return false;
				}
				if (jackObserver_.isOpen()) {
					const auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
					const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [&node, &existingClients](const auto& value) {
						return node->hasClient() ? value.id == node->clientId() : !existingClients.contains(value.id);
					});
					if (client != snapshot.clients.cend()) {
						node->setClientId(client->id);
						auto settings = patchbaySettings();
						settings.setValue(QStringLiteral("layout/%1/x").arg(QString::fromStdString(client->id)), position.x());
						settings.setValue(QStringLiteral("layout/%1/y").arg(QString::fromStdString(client->id)), position.y());
					}
				}
				auto* createdNode = node.get();
				node->setContextId(targetCanvas->sceneContext().id);
				loadedModuleNodes_.push_back(std::move(node));
				(*refreshTrayNodeGuiEntries)();
				targetCanvas->markProjectDirty();
				refreshJackGraph();
				QTimer::singleShot(100, window, [this, createdNode, existingClients = std::move(existingClients), refreshJackGraph] {
					if (!jackObserver_.isOpen())
						return;
					const auto node = std::find_if(loadedModuleNodes_.begin(), loadedModuleNodes_.end(), [createdNode](const auto& value) {
						return value.get() == createdNode;
					});
					if (node == loadedModuleNodes_.end() || !(*node)->ownsClient({}))
						return;
					const auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
					const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [this, &existingClients](const auto& value) {
						return !existingClients.contains(value.id)
							&& std::none_of(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&value](const auto& node) {
								return node->ownsClient(value.id);
							});
					});
					if (client != snapshot.clients.cend())
						(*node)->setClientId(client->id);
					refreshJackGraph();
				});
				return true;
			};
			QObject::connect(nodeTree, &QTreeWidget::itemDoubleClicked, window_, [canvasTabs, createModuleNode](QTreeWidgetItem* item) {
				const auto interfaceType = item->data(0, Qt::UserRole + 3).toString();
				if (!interfaceType.isEmpty()) {
					if (auto* activeCanvas = dynamic_cast<PatchbayCanvas*>(canvasTabs->currentWidget()))
						activeCanvas->addInterfaceNode(interfaceType, {});
					return;
				}
				const auto subGraphName = item->data(0, Qt::UserRole + 2).toString();
				if (!subGraphName.isEmpty()) {
					if (auto* activeCanvas = dynamic_cast<PatchbayCanvas*>(canvasTabs->currentWidget()))
						activeCanvas->addSubGraph(subGraphName, {});
					return;
				}
				const auto modulePath = item->data(0, Qt::UserRole).toString();
				if (modulePath.isEmpty())
					return;
				if (auto* activeCanvas = dynamic_cast<PatchbayCanvas*>(canvasTabs->currentWidget()))
					(*createModuleNode)(activeCanvas, modulePath, item->data(0, Qt::UserRole + 1).toUInt(), {}, item->data(0, Qt::UserRole + 4).toString());
			});
			canvas->setModuleNodeDropHandler([canvas, createModuleNode](const QString& modulePath, std::uint32_t index, const QPointF& position) {
				return (*createModuleNode)(canvas, modulePath, index, position, {});
			});
			canvas->setVstNodeDropHandler([canvas, createModuleNode](const QString& modulePath, std::uint32_t index, const QString& settingsJson, const QPointF& position) {
				return (*createModuleNode)(canvas, modulePath, index, position, settingsJson);
			});
			auto selectedModuleNode = std::make_shared<LoadedModuleNode*>(nullptr);
			auto* inspectorDock = new WinJACKNexus::UI::Controls::DockPanel(
				QStringLiteral("参数"), Qt::RightDockWidgetArea, window_);
			inspectorDock->setObjectName(QStringLiteral("patchbay.inspector"));
			auto* inspector = new QWidget(inspectorDock);
			auto* inspectorLayout = new QVBoxLayout(inspector);
			// Keep every inspector field clear of the overlaid resize grip.
			inspectorLayout->setContentsMargins(32, 12, 12, 12);
			auto* selectionTitle = new WinJACKNexus::UI::Controls::ReadOnlyField(inspector);
			selectionTitle->setPlaceholderText(QStringLiteral("未选择节点"));
			selectionTitle->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			auto* properties = new QWidget(inspector);
			auto* propertyForm = new QFormLayout(properties);
			auto* categoryValue = new WinJACKNexus::UI::Controls::ReadOnlyField(properties);
			auto* splitCheck = new WinJACKNexus::UI::Controls::ToggleField(QStringLiteral("拆分输入和输出"), properties);
			categoryValue->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			splitCheck->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			propertyForm->addRow(QStringLiteral("类别"), categoryValue);
			propertyForm->addRow(QString(), splitCheck);
			properties->setEnabled(false);
			inspectorLayout->addWidget(selectionTitle);
			inspectorLayout->addWidget(properties);
			auto* showGuiButton = new WinJACKNexus::UI::Controls::ActionButton(QStringLiteral("显示自带 GUI"), inspector);
			showGuiButton->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
			showGuiButton->setObjectName(QStringLiteral("showModuleGuiButton"));
			showGuiButton->setEnabled(false);
			inspectorLayout->addWidget(showGuiButton);
			inspectorLayout->addStretch(1);
			inspectorDock->setWidget(inspector);
			window_->addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
			QTimer::singleShot(0, window_, [window = window_, nodeDock, inspectorDock] {
				window->resizeDocks({ nodeDock, inspectorDock }, { 270, 310 }, Qt::Horizontal);
			});

				auto selectedClient = std::make_shared<std::string>();
				auto selectedSettingsContainer = std::make_shared<QPointer<QWidget>>();
			*configureCanvasInteractions = [this,
			                                  canvasTabs,
			                                  selectionTitle,
			                                  categoryValue,
			                                  splitCheck,
			                                  propertyForm,
			                                  properties,
				                                  showGuiButton,
			                                  selectedClient,
			                                  selectedModuleNode,
			                                  selectedSettingsContainer,
			                                  inspector,
			                                  inspectorLayout,
			                                  createModuleNode,
			                                  refreshJackGraph,
			                                  refreshTrayNodeGuiEntries](PatchbayCanvas* targetCanvas) {
				if (targetCanvas == nullptr)
					return;
				targetCanvas->setModuleNodeHandlers(
					[this](const std::string& clientId) {
						return std::any_of(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& node) {
							return node->ownsClient(clientId);
						});
					},
					[this](const std::string& clientId) {
						const auto node = std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& value) {
							return value->ownsClient(clientId);
						});
						if (node == loadedModuleNodes_.cend())
							return false;
						copiedModuleNode_ = ModuleNodeInfo { {}, {}, (*node)->modulePath(), (*node)->index() };
						return true;
					},
					[this, refreshJackGraph, refreshTrayNodeGuiEntries, selectedModuleNode](const std::string& clientId) {
						if (*selectedModuleNode != nullptr && (*selectedModuleNode)->ownsClient(clientId)) {
							(*selectedModuleNode)->clearSettingsView();
							*selectedModuleNode = nullptr;
						}
						const auto removed = std::erase_if(loadedModuleNodes_, [&clientId](const auto& node) {
							return node->ownsClient(clientId);
						});
						if (removed != 0)
							refreshJackGraph();
						if (removed != 0)
							(*refreshTrayNodeGuiEntries)();
						return removed != 0;
					},
					[this](const std::string& clientId, QWidget* parent) {
						const auto node = std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& value) {
							return value->ownsClient(clientId);
						});
						return node != loadedModuleNodes_.cend()
							&& (*node)->category() != QStringLiteral("Meter")
							&& (*node)->showGui(parent);
					},
					[this](const std::string& clientId) {
						const auto node = std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& value) {
							return value->ownsClient(clientId);
						});
						return node != loadedModuleNodes_.cend()
							&& (*node)->category() != QStringLiteral("Meter");
					},
					[targetCanvas, createModuleNode, this](const QPointF& position) {
						return !copiedModuleNode_.modulePath.isEmpty()
							&& (*createModuleNode)(targetCanvas, copiedModuleNode_.modulePath, copiedModuleNode_.index, position, {});
					});
				targetCanvas->setNodeSelectionHandler([this,
			                                  canvasTabs,
			                                  targetCanvas,
			                                  selectionTitle,
			                                  categoryValue,
			                                  splitCheck,
			                                  propertyForm,
			                                  properties,
			                                  showGuiButton,
			                                  selectedClient,
			                                  selectedModuleNode,
			                                  selectedSettingsContainer,
			                                  inspector,
			                                  inspectorLayout](const std::string& clientId) {
				if (canvasTabs->currentWidget() != targetCanvas)
					return;
				const auto sameSelection = *selectedClient == clientId;
				if (!sameSelection) {
					if (*selectedModuleNode != nullptr) {
						(*selectedModuleNode)->clearSettingsView();
						*selectedModuleNode = nullptr;
					} else if (*selectedSettingsContainer != nullptr) {
						WinJACKNexus::UI::NativeWindowUtils::detachNativeWindow(selectedSettingsContainer->data());
						delete selectedSettingsContainer->data();
					}
					*selectedSettingsContainer = nullptr;
				}
				*selectedClient = clientId;
				const auto client = std::find_if(targetCanvas->snapshot().clients.cbegin(), targetCanvas->snapshot().clients.cend(), [&clientId](const auto& value) {
					return value.id == clientId;
				});
				const bool selected = client != targetCanvas->snapshot().clients.cend();
				properties->setEnabled(selected);
				showGuiButton->setVisible(true);
				showGuiButton->setEnabled(false);
				selectionTitle->setText(selected ? QString::fromStdString(client->name) : QString());
				const auto module = selected ? std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& value) {
					return value->ownsClient(clientId);
				}) : loadedModuleNodes_.cend();
				QString category;
				if (selected) {
					category = targetCanvas->isSubGraphNode(clientId) ? QStringLiteral("Sub Graph")
						: module != loadedModuleNodes_.cend() && !(*module)->category().isEmpty()
							? (*module)->category()
							: client->id == "system" ? QStringLiteral("system")
							: client->id == "WinJACKNexus-Patchbay" ? QStringLiteral("Patchbay")
							: QStringLiteral("jackclient");
				}
				categoryValue->setText(category);
				const auto canSplit = selected && clientCanSplit(targetCanvas->snapshot(), clientId);
				propertyForm->setRowVisible(splitCheck, canSplit);
				splitCheck->setChecked(canSplit && client->split);
				if (!selected)
					return;
				if (module == loadedModuleNodes_.cend())
					return;
				const auto canShowModuleGui = (*module)->category() != QStringLiteral("Meter");
				showGuiButton->setVisible(canShowModuleGui);
				showGuiButton->setEnabled(canShowModuleGui);
				if (sameSelection && *selectedSettingsContainer != nullptr)
					return;
				const auto settingsContainer = (*module)->showSettings(inspector);
				if (settingsContainer == nullptr)
					return;
				*selectedModuleNode = module->get();
				*selectedSettingsContainer = settingsContainer;
				inspectorLayout->insertWidget(inspectorLayout->count() - 1, settingsContainer);
			});
			};
			(*configureCanvasInteractions)(canvas);
			for (const auto& [contextId, child] : *subGraphCanvases) {
				if (!child.isNull())
					(*configureCanvasInteractions)(child.data());
			}
			QObject::connect(showGuiButton, &QPushButton::clicked, window_, [this, selectedClient, showGuiButton] {
				if (selectedClient->empty())
					return;
				const auto module = std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [selectedClient](const auto& value) {
					return value->ownsClient(*selectedClient);
				});
				if (module == loadedModuleNodes_.cend()) {
					showGuiButton->setEnabled(false);
					return;
				}
				if ((*module)->category() == QStringLiteral("Meter")) {
					showGuiButton->setEnabled(false);
					return;
				}
				(*module)->showGui(window_.data());
			});
			QObject::connect(splitCheck, &QCheckBox::toggled, canvas, [canvas, selectedClient](bool checked) {
				if (!selectedClient->empty())
					canvas->setClientSplit(*selectedClient, checked);
			});

			QObject::connect(nodeFilter, &QLineEdit::textChanged, nodeTree, [nodeTree](const QString& query) {
				nodeTree->setProperty("filterQuery", query);
				applyNodeLibraryFilter(nodeTree, query);
			});
			auto* toolbar = window_->addToolBar(QStringLiteral("接线盘"));
			toolbar->setMovable(false);
			auto* search = new WinJACKNexus::UI::Controls::SearchField(toolbar);
			search->setPlaceholderText(QStringLiteral("搜索节点或端口"));
			search->setMinimumWidth(190);
			toolbar->addWidget(search);
			toolbar->addSeparator();
			auto* fitAction = toolbar->addAction(QStringLiteral("适配"));
			auto* zoomOutAction = toolbar->addAction(QStringLiteral("缩小"));
			auto* zoomResetAction = toolbar->addAction(QStringLiteral("100%"));
			auto* zoomInAction = toolbar->addAction(QStringLiteral("放大"));
			QObject::connect(search, &QLineEdit::textChanged, canvas, &PatchbayCanvas::setSearchQuery);
			QObject::connect(fitAction, &QAction::triggered, canvas, &PatchbayCanvas::zoomFit);
			QObject::connect(zoomOutAction, &QAction::triggered, canvas, &PatchbayCanvas::zoomOut);
			QObject::connect(zoomResetAction, &QAction::triggered, canvas, &PatchbayCanvas::zoomReset);
			QObject::connect(zoomInAction, &QAction::triggered, canvas, &PatchbayCanvas::zoomIn);
			toolbar->addSeparator();
			auto* scanVstAction = toolbar->addAction(QStringLiteral("扫描 VST"));
			QObject::connect(scanVstAction, &QAction::triggered, window_, [refreshNodeLibrary, window = window_.data()] {
				const auto scannerPath = QCoreApplication::applicationDirPath() + QStringLiteral("/VST Scanner.exe");
				if (!QFileInfo::exists(scannerPath)) {
					QMessageBox::warning(window, QStringLiteral("扫描器不可用"), QStringLiteral("未找到 VST Scanner.exe。"));
					return;
				}
				auto* scanner = new QProcess(window);
				QObject::connect(scanner, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), scanner, [scanner, refreshNodeLibrary](int, QProcess::ExitStatus) {
					scanner->deleteLater();
					(*refreshNodeLibrary)();
				});
				scanner->start(scannerPath, { QStringLiteral("--output"), QCoreApplication::applicationDirPath() + QStringLiteral("/nodes/VSTPlugins") });
				if (!scanner->waitForStarted(1000)) {
					scanner->deleteLater();
					QMessageBox::warning(window, QStringLiteral("扫描器启动失败"), QStringLiteral("无法启动 VST Scanner.exe。"));
				}
			});
			auto* themes = new QActionGroup(window_);
			themes->setExclusive(true);
			auto* darkAction = toolbar->addAction(QStringLiteral("深色"));
			darkAction->setCheckable(true);
			darkAction->setChecked(true);
			themes->addAction(darkAction);
			auto* lightAction = toolbar->addAction(QStringLiteral("浅色"));
			lightAction->setCheckable(true);
			themes->addAction(lightAction);
			QObject::connect(darkAction, &QAction::triggered, window_, [] {
				PatchbayCanvas::applyTheme(false);
			});
			QObject::connect(lightAction, &QAction::triggered, window_, [] {
				PatchbayCanvas::applyTheme(true);
			});

			auto* fileMenu = window_->menuBar()->addMenu(QStringLiteral("文件"));
				auto currentProject = std::make_shared<QString>();
				auto collectSubGraphs = [canvas, subGraphCanvases] {
					std::vector<PatchbaySubGraphProject> projects;
					projects.push_back({ canvas->sceneContext(), canvas->subGraphNodes(), canvas->interfaceNodes(), canvas->logicalConnections() });
					for (const auto& [contextId, child] : *subGraphCanvases) {
						if (!child.isNull())
							projects.push_back({ child->sceneContext(), child->subGraphNodes(), child->interfaceNodes(), child->logicalConnections() });
					}
					return projects;
				};
				auto loadedModuleRecords = [this] {
					std::vector<PatchbayProjectModule> modules;
					modules.reserve(loadedModuleNodes_.size());
					for (const auto& node : loadedModuleNodes_)
						modules.push_back({ node->modulePath(), node->index(), node->clientId(), node->settingsJson(), node->contextId() });
					return modules;
				};
				auto syncModuleProjectState = [canvas, loadedModuleRecords, this] {
					if (!moduleRecordsEqual(savedModuleRecords_, loadedModuleRecords()))
						canvas->markProjectDirty();
				};
				auto syncSubGraphProjectState = [canvas, subGraphCanvases] {
					for (const auto& [contextId, child] : *subGraphCanvases) {
						if (!child.isNull() && child->isProjectDirty()) {
							canvas->markProjectDirty();
							return;
						}
					}
				};
				auto saveProject = [canvas, currentProject, loadedModuleRecords, collectSubGraphs, subGraphCanvases, this](bool saveAs, bool showError) {
					if (saveAs || currentProject->isEmpty()) {
						const auto path = QFileDialog::getSaveFileName(window_, QStringLiteral("保存接线盘工程"), *currentProject,
						                                                  QStringLiteral("WinJACKNexus 工程 (*.wjnproj)"));
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
						QMessageBox::warning(window_, QStringLiteral("保存失败"), QStringLiteral("无法保存接线盘工程。"));
					return false;
				};
				auto confirmSave = [canvas, saveProject, syncModuleProjectState, syncSubGraphProjectState, window = window_.data()] {
					syncModuleProjectState();
					syncSubGraphProjectState();
					if (!canvas->isProjectDirty())
						return true;
					const auto choice = QMessageBox::warning(window, QStringLiteral("未保存的工程"),
						QStringLiteral("当前工程有未保存的更改。是否保存？"),
						QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
					if (choice == QMessageBox::Save)
						return saveProject(false, true);
					return choice == QMessageBox::Discard;
				};
				static_cast<PatchbayWindow*>(window_.data())->setCloseConfirmation(confirmSave);
				auto* newProject = fileMenu->addAction(QStringLiteral("新建工程"));
				auto* openProject = fileMenu->addAction(QStringLiteral("打开工程..."));
				auto* saveAction = fileMenu->addAction(QStringLiteral("保存工程"));
				saveAction->setShortcut(QKeySequence::Save);
				auto* saveAsAction = fileMenu->addAction(QStringLiteral("工程另存为..."));
				QObject::connect(newProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, clearSubGraphs, refreshJackGraph, refreshNodeLibrary, refreshTrayNodeGuiEntries, selectedModuleNode, this] {
					if (!confirmSave())
						return;
					clearSubGraphs();
					canvas->newProject();
					restoreMeterbridgeProjectState({});
					if (*selectedModuleNode != nullptr) {
						(*selectedModuleNode)->clearSettingsView();
						*selectedModuleNode = nullptr;
					}
					loadedModuleNodes_.clear();
					(*refreshTrayNodeGuiEntries)();
					if (jackObserver_.isOpen()) {
						auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
						ensureSystemClient(snapshot);
						canvas->setSnapshot(snapshot);
					}
					refreshJackGraph();
					(*refreshNodeLibrary)();
					savedModuleRecords_.clear();
					currentProject->clear();
					patchbaySettings().remove(QStringLiteral("lastProjectPath"));
				});
				QObject::connect(openProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, restoreModuleRecords, loadedModuleRecords, restoreSubGraphs, refreshJackGraph, this] {
					const auto path = QFileDialog::getOpenFileName(window_, QStringLiteral("打开接线盘工程"), *currentProject,
					                                              QStringLiteral("WinJACKNexus 工程 (*.wjnproj)"));
					if (path.isEmpty() || !confirmSave())
						return;
					std::vector<PatchbayProjectModule> modules;
					std::vector<PatchbaySubGraphProject> subGraphs;
					if (canvas->loadProject(path, modules, subGraphs) && restoreSubGraphs(subGraphs)) {
						*currentProject = path;
						patchbaySettings().setValue(QStringLiteral("lastProjectPath"), path);
						restoreModuleRecords(modules);
						restoreMeterbridgeProjectState(canvas->meterbridgeProjectState());
						savedModuleRecords_ = loadedModuleRecords();
						refreshJackGraph();
					} else {
						QMessageBox::warning(window_, QStringLiteral("打开失败"), QStringLiteral("无法读取接线盘工程。"));
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
				auto* autoSaveAction = fileMenu->addAction(QStringLiteral("自动保存间隔..."));
				QObject::connect(autoSaveAction, &QAction::triggered, window_, [autoSaveInterval, applyAutoSaveInterval, window = window_.data()] {
					bool accepted = false;
					const auto seconds = QInputDialog::getInt(window, QStringLiteral("自动保存"),
						QStringLiteral("保存间隔（秒，0 为关闭）"), *autoSaveInterval, 0, 3600, 5, &accepted);
					if (!accepted)
						return;
					patchbaySettings().setValue(QStringLiteral("autoSaveIntervalSeconds"), seconds);
					applyAutoSaveInterval(seconds);
				});
				fileMenu->addSeparator();
			auto* closeAction = fileMenu->addAction(QStringLiteral("关闭窗口"));
			closeAction->setShortcut(QKeySequence::Close);
			QObject::connect(closeAction, &QAction::triggered, window_, [window = window_.data()] {
				window->close();
			});
			auto* editMenu = window_->menuBar()->addMenu(QStringLiteral("编辑"));
			editMenu->addAction(QStringLiteral("适配画布"), canvas, &PatchbayCanvas::zoomFit);
			editMenu->addAction(QStringLiteral("重置缩放"), canvas, &PatchbayCanvas::zoomReset);
			auto* viewMenu = window_->menuBar()->addMenu(QStringLiteral("视图"));
			viewMenu->addAction(nodeDock->toggleViewAction());
			viewMenu->addAction(inspectorDock->toggleViewAction());
			viewMenu->addSeparator();
			viewMenu->addAction(darkAction);
			viewMenu->addAction(lightAction);
			auto* graphMenu = window_->menuBar()->addMenu(QStringLiteral("图形"));
			auto restoreLastProject = [this, canvas, currentProject, restoreSubGraphs, restoreModuleRecords, loadedModuleRecords] {
				const auto lastProject = patchbaySettings().value(QStringLiteral("lastProjectPath")).toString();
				std::vector<PatchbayProjectModule> modules;
				std::vector<PatchbaySubGraphProject> subGraphs;
				if (!lastProject.isEmpty() && QFile::exists(lastProject) && canvas->loadProject(lastProject, modules, subGraphs)
					&& restoreSubGraphs(subGraphs)) {
					*currentProject = lastProject;
					restoreModuleRecords(modules);
					restoreMeterbridgeProjectState(canvas->meterbridgeProjectState());
					savedModuleRecords_ = loadedModuleRecords();
					canvas->restoreJackConnections(jackObserver_);
				}
			};
			auto attachJackObserver = [this, refreshJackGraph, restoreLastProject] {
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
			attachJackObserver();

			graphMenu->addAction(QStringLiteral("刷新节点库"), window_, [refreshNodeLibrary] {
				(*refreshNodeLibrary)();
			});
			graphMenu->addAction(QStringLiteral("卸载 Digital Mixer 节点"), window_,
				[this, canvas, refreshJackGraph, refreshTrayNodeGuiEntries] {
					if (!removeLoadedDigitalMixer()) return;
					canvas->markProjectDirty();
					(*refreshTrayNodeGuiEntries)();
					refreshJackGraph();
				});
		}

		window_->showNormal();
		window_->raise();
		window_->activateWindow();
		return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
	}

	void destroyWindow(HWND) noexcept override
	{
		if (window_ != nullptr) {
			delete window_;
			window_ = nullptr;
		}
	}

private:
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
			if (node->hasClient() && liveClients.contains(node->clientId()))
				continue;
			const auto candidate = std::find_if(newClients.cbegin(), newClients.cend(), [this](const auto& clientId) {
				return std::none_of(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& node) {
					return node->ownsClient(clientId);
				});
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
	std::vector<std::unique_ptr<LoadedModuleNode>> loadedModuleNodes_;
	std::vector<PatchbayProjectModule> savedModuleRecords_;
	ModuleNodeInfo copiedModuleNode_;
};

}

extern "C" WINJACK_NODE_API int wjn_patchbay_show_digital_mixer(void)
{
	return livePatchbayNode == nullptr ? 0 : livePatchbayNode->showLoadedDigitalMixer();
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
	return index == 0 ? "Patchbay" : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
	return index == 0 ? "Patchbay" : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
	return index == 0 ? new PatchbayNode : nullptr;
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
