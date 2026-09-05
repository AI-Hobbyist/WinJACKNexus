#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>
#include <WinJACKNexus/UI/NativeWindowUtils.hpp>

#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QActionGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDrag>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QDirIterator>
#include <QSettings>
#include <QScrollArea>
#include <QTreeWidget>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

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

class ModuleNodeTree final : public QTreeWidget {
public:
	explicit ModuleNodeTree(QWidget* parent = nullptr)
		: QTreeWidget(parent)
	{
	}

protected:
	void startDrag(Qt::DropActions) override
	{
		auto* item = currentItem();
		if (item == nullptr)
			return;
		const auto modulePath = item->data(0, Qt::UserRole).toString();
		if (modulePath.isEmpty())
			return;

		auto* mimeData = new QMimeData;
		const auto payload = modulePath.toUtf8() + '\n' + QByteArray::number(item->data(0, Qt::UserRole + 1).toUInt());
		mimeData->setData("application/x-winjacknexus-module-node", payload);
		auto* drag = new QDrag(this);
		drag->setMimeData(mimeData);
		drag->exec(Qt::CopyAction);
	}
};

QTreeWidgetItem* addCategory(QTreeWidget* tree, const QString& name)
{
	auto* category = new QTreeWidgetItem(tree, { name });
	category->setExpanded(true);
	return category;
}

void addNodeItem(QTreeWidgetItem* category, const QString& name, bool online = true)
{
	auto* item = new QTreeWidgetItem(category, { online ? name : name + QStringLiteral("（离线）") });
	if (!online)
		item->setForeground(0, QColor(QStringLiteral("#8193a5")));
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
	for (int index = 0; index < tree->topLevelItemCount(); ++index) {
		auto* category = tree->topLevelItem(index);
		const bool categoryMatch = needle.isEmpty() || category->text(0).contains(needle, Qt::CaseInsensitive);
		bool childMatch = false;
		for (int childIndex = 0; childIndex < category->childCount(); ++childIndex) {
			auto* item = category->child(childIndex);
			const bool match = categoryMatch || item->text(0).contains(needle, Qt::CaseInsensitive);
			item->setHidden(!match);
			childMatch = childMatch || match;
		}
		category->setHidden(!childMatch);
		if (!needle.isEmpty() && childMatch)
			category->setExpanded(true);
	}
}

struct ModuleNodeInfo final {
	QString category;
	QString name;
	QString modulePath;
	std::uint32_t index = 0;
};

bool moduleRecordsEqual(const std::vector<PatchbayProjectModule>& left,
	                       const std::vector<PatchbayProjectModule>& right)
{
	if (left.size() != right.size())
		return false;
	return std::equal(left.cbegin(), left.cend(), right.cbegin(), [](const auto& leftModule, const auto& rightModule) {
		return leftModule.modulePath == rightModule.modulePath
			&& leftModule.index == rightModule.index
			&& leftModule.clientId == rightModule.clientId
			&& leftModule.settingsJson == rightModule.settingsJson;
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

void populateNodeLibrary(QTreeWidget* tree,
                         const WinJACKNexus::Patchbay::Core::Snapshot& snapshot,
						 const std::set<std::string>& knownJackClients,
						 const std::set<std::string>& moduleClientIds,
						 const QString& filterQuery = {})
{
	tree->clear();
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
	for (const auto& node : scanNodeModules()) {
		auto [category, inserted] = moduleCategories.try_emplace(node.category, nullptr);
		if (inserted)
			category->second = addCategory(tree, node.category);
		auto* item = new QTreeWidgetItem(category->second, { node.name });
		item->setData(0, Qt::UserRole, node.modulePath);
		item->setData(0, Qt::UserRole + 1, node.index);
	}
	applyNodeLibraryFilter(tree, filterQuery);
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
		if (!node->init()) {
			if (node != nullptr)
				destroyNode(node);
			FreeLibrary(library);
			return nullptr;
		}
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
			                                                            modulePath,
			                                                            index,
			                                                            category));
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
			return clientId_;
		}

		bool hasClient() const noexcept
		{
			return !clientId_.empty();
		}

	bool ownsClient(const std::string& clientId) const noexcept
	{
		return clientId_ == clientId;
	}

	bool showGui(QWidget* parent) noexcept
	{
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
		using CreateSettingsWidget = QWidget* (*)(WinJACKNexus::Node::INode* node, QWidget* parent);
		using DestroySettingsWidget = void (*)(WinJACKNexus::Node::INode* node, QWidget* widget);
		using CreateSettingsView = void* (*)(WinJACKNexus::Node::INode* node, void* parentHwnd);
		using DestroySettingsView = void (*)(WinJACKNexus::Node::INode* node, void* viewHwnd);

		LoadedModuleNode(HMODULE library,
		                 WinJACKNexus::Node::INode* node,
		                 DestroyNode destroyNode,
			                 CreateSettingsWidget createSettingsWidget,
			                 DestroySettingsWidget destroySettingsWidget,
		                 CreateSettingsView createSettingsView,
		                 DestroySettingsView destroySettingsView,
		                 const QString& modulePath,
		                 std::uint32_t index,
		                 const QString& category) noexcept
		: library_(library)
		, node_(node)
		, destroyNode_(destroyNode)
		, createSettingsWidget_(createSettingsWidget)
		, destroySettingsWidget_(destroySettingsWidget)
			, createSettingsView_(createSettingsView)
			, destroySettingsView_(destroySettingsView)
			, getNodeSettingsJson_(reinterpret_cast<GetNodeSettingsJson>(GetProcAddress(library, "wjn_get_node_settings_json")))
		, modulePath_(modulePath)
		, index_(index)
			, category_(category)
	{
	}

	HMODULE library_ = nullptr;
	WinJACKNexus::Node::INode* node_ = nullptr;
	DestroyNode destroyNode_ = nullptr;
		CreateSettingsWidget createSettingsWidget_ = nullptr;
		DestroySettingsWidget destroySettingsWidget_ = nullptr;
		CreateSettingsView createSettingsView_ = nullptr;
		DestroySettingsView destroySettingsView_ = nullptr;
		GetNodeSettingsJson getNodeSettingsJson_ = nullptr;
	QString modulePath_;
	std::uint32_t index_ = 0;
	QString category_;
	std::string clientId_;
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

class PatchbayNode final : public WinJACKNexus::Node::INode {
public:
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
		return true;
	}

	void shutdown() noexcept override
	{
		destroyWindow(nullptr);
		loadedModuleNodes_.clear();
		observedJackClients_.clear();
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

			auto* canvas = new PatchbayCanvas(window_);
			canvas->setConnectionDisconnectHandler([this](const std::string& source, const std::string& destination) {
				if (jackObserver_.isOpen())
					WinJACKNexus::Audio::JackApi::instance().disconnect(jackObserver_.nativeHandle(), source.c_str(), destination.c_str());
			});
			window_->setCentralWidget(canvas);
			window_->setDockNestingEnabled(true);

			auto* nodeDock = new QDockWidget(QStringLiteral("节点"), window_);
			nodeDock->setObjectName(QStringLiteral("patchbay.nodeLibrary"));
			nodeDock->setMinimumWidth(240);
			auto* nodePanel = new QWidget(nodeDock);
			auto* nodeLayout = new QVBoxLayout(nodePanel);
			nodeLayout->setContentsMargins(8, 8, 8, 8);
			auto* nodeFilter = new QLineEdit(nodePanel);
			nodeFilter->setPlaceholderText(QStringLiteral("筛选节点"));
			nodeFilter->setClearButtonEnabled(true);
			auto* nodeTree = new ModuleNodeTree(nodePanel);
			nodeTree->setHeaderHidden(true);
			nodeTree->setIndentation(14);
			nodeTree->setAnimated(true);
			nodeTree->setDragEnabled(true);
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
			populateNodeLibrary(nodeTree, canvas->snapshot(), knownJackClients_, loadedModuleClientIds(), nodeFilter->text());
			auto refreshJackGraph = [this, canvas, nodeTree, loadedModuleClientIds] {
				if (canvas->isNodeDragActive() || !jackObserver_.isOpen())
					return;
				const auto selectedClientId = canvas->selectedNodeId();
				const auto previousSnapshot = canvas->snapshot();
				auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
				ensureSystemClient(snapshot);
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
				canvas->setSnapshot(snapshot);
				canvas->restoreJackConnections(jackObserver_);
				for (const auto& [oldClientId, newClientId] : reboundClients) {
					if (oldClientId == selectedClientId)
						canvas->selectNode(newClientId);
				}
				populateNodeLibrary(nodeTree, canvas->snapshot(), knownJackClients_, loadedModuleClientIds(), nodeTree->property("filterQuery").toString());
			};
			auto restoreModuleRecords = [this](const std::vector<PatchbayProjectModule>& modules) {
					loadedModuleNodes_.clear();
					for (const auto& module : modules) {
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
							const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [&existingClients](const auto& value) {
								return !existingClients.contains(value.id);
							});
							if (client != snapshot.clients.cend())
								node->setClientId(client->id);
						}
						if (!module.clientId.empty() && !node->hasClient())
							node->setClientId(module.clientId);
						loadedModuleNodes_.push_back(std::move(node));
					}
				};
			auto createModuleNode = [this, canvas, refreshJackGraph, window = window_.data()](const QString& modulePath, std::uint32_t index, const QPointF& position) {
				std::set<std::string> existingClients;
				if (jackObserver_.isOpen()) {
					for (const auto& client : WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_).clients)
						existingClients.insert(client.id);
				}

				auto node = LoadedModuleNode::create(modulePath, index);
				if (node == nullptr) {
					QMessageBox::warning(window, QStringLiteral("节点启动失败"),
					                     QStringLiteral("无法初始化所选模块节点。请确认 JACK 服务和对应系统设备可用。"));
					return false;
				}
				if (jackObserver_.isOpen()) {
					const auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
					const auto client = std::find_if(snapshot.clients.cbegin(), snapshot.clients.cend(), [&existingClients](const auto& value) {
						return !existingClients.contains(value.id);
					});
					if (client != snapshot.clients.cend()) {
						node->setClientId(client->id);
						QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay"));
						settings.setValue(QStringLiteral("layout/%1/x").arg(QString::fromStdString(client->id)), position.x());
						settings.setValue(QStringLiteral("layout/%1/y").arg(QString::fromStdString(client->id)), position.y());
					}
				}
				auto* createdNode = node.get();
				loadedModuleNodes_.push_back(std::move(node));
				canvas->markProjectDirty();
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
			QObject::connect(nodeTree, &QTreeWidget::itemDoubleClicked, window_, [createModuleNode](QTreeWidgetItem* item) {
				const auto modulePath = item->data(0, Qt::UserRole).toString();
				if (modulePath.isEmpty())
					return;
				createModuleNode(modulePath, item->data(0, Qt::UserRole + 1).toUInt(), {});
			});
			canvas->setModuleNodeDropHandler([createModuleNode](const QString& modulePath, std::uint32_t index, const QPointF& position) {
				return createModuleNode(modulePath, index, position);
			});
			auto selectedModuleNode = std::make_shared<LoadedModuleNode*>(nullptr);
			canvas->setModuleNodeHandlers(
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
				[this, refreshJackGraph, selectedModuleNode](const std::string& clientId) {
					if (*selectedModuleNode != nullptr && (*selectedModuleNode)->ownsClient(clientId)) {
						(*selectedModuleNode)->clearSettingsView();
						*selectedModuleNode = nullptr;
					}
					const auto removed = std::erase_if(loadedModuleNodes_, [&clientId](const auto& node) {
						return node->ownsClient(clientId);
					});
					if (removed != 0)
						refreshJackGraph();
					return removed != 0;
				},
				[this](const std::string& clientId, QWidget* parent) {
					const auto node = std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& value) {
						return value->ownsClient(clientId);
					});
					return node != loadedModuleNodes_.cend() && (*node)->showGui(parent);
				},
				[createModuleNode, this](const QPointF& position) {
					return !copiedModuleNode_.modulePath.isEmpty()
						&& createModuleNode(copiedModuleNode_.modulePath, copiedModuleNode_.index, position);
				});

			auto* inspectorDock = new QDockWidget(QStringLiteral("参数"), window_);
			inspectorDock->setObjectName(QStringLiteral("patchbay.inspector"));
			inspectorDock->setMinimumWidth(270);
			auto* inspector = new QWidget(inspectorDock);
			auto* inspectorLayout = new QVBoxLayout(inspector);
			inspectorLayout->setContentsMargins(12, 12, 12, 12);
			auto* selectionTitle = new QLineEdit(inspector);
			selectionTitle->setReadOnly(true);
			selectionTitle->setPlaceholderText(QStringLiteral("未选择节点"));
			auto* properties = new QWidget(inspector);
			auto* propertyForm = new QFormLayout(properties);
			auto* categoryValue = new QLineEdit(properties);
			categoryValue->setReadOnly(true);
			auto* splitCheck = new QCheckBox(QStringLiteral("拆分输入和输出"), properties);
			propertyForm->addRow(QStringLiteral("类别"), categoryValue);
			propertyForm->addRow(QString(), splitCheck);
			properties->setEnabled(false);
			inspectorLayout->addWidget(selectionTitle);
			inspectorLayout->addWidget(properties);
			auto* showGuiButton = new QPushButton(QStringLiteral("显示自带 GUI"), inspector);
			showGuiButton->setObjectName(QStringLiteral("showModuleGuiButton"));
			showGuiButton->setEnabled(false);
			inspectorLayout->addWidget(showGuiButton);
			inspectorLayout->addStretch(1);
			inspectorDock->setWidget(inspector);
			window_->addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

				auto selectedClient = std::make_shared<std::string>();
				auto selectedSettingsContainer = std::make_shared<QPointer<QWidget>>();
			canvas->setNodeSelectionHandler([this,
			                                  canvas,
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
				const auto client = std::find_if(canvas->snapshot().clients.cbegin(), canvas->snapshot().clients.cend(), [&clientId](const auto& value) {
					return value.id == clientId;
				});
				const bool selected = client != canvas->snapshot().clients.cend();
				properties->setEnabled(selected);
				showGuiButton->setEnabled(false);
				selectionTitle->setText(selected ? QString::fromStdString(client->name) : QString());
				const auto module = selected ? std::find_if(loadedModuleNodes_.cbegin(), loadedModuleNodes_.cend(), [&clientId](const auto& value) {
					return value->ownsClient(clientId);
				}) : loadedModuleNodes_.cend();
				QString category;
				if (selected) {
					category = module != loadedModuleNodes_.cend() && !(*module)->category().isEmpty()
							? (*module)->category()
							: client->id == "system" ? QStringLiteral("system")
							: client->id == "WinJACKNexus-Patchbay" ? QStringLiteral("Patchbay")
							: QStringLiteral("jackclient");
				}
				categoryValue->setText(category);
				const auto canSplit = selected && clientCanSplit(canvas->snapshot(), clientId);
				propertyForm->setRowVisible(splitCheck, canSplit);
				splitCheck->setChecked(canSplit && client->split);
				if (!selected)
					return;
				if (module == loadedModuleNodes_.cend())
					return;
				showGuiButton->setEnabled(true);
				if (sameSelection && *selectedSettingsContainer != nullptr)
					return;
				const auto settingsContainer = (*module)->showSettings(inspector);
				if (settingsContainer == nullptr)
					return;
				*selectedModuleNode = module->get();
				*selectedSettingsContainer = settingsContainer;
				inspectorLayout->insertWidget(inspectorLayout->count() - 1, settingsContainer);
			});
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
			auto* search = new QLineEdit(toolbar);
			search->setPlaceholderText(QStringLiteral("搜索节点或端口"));
			search->setClearButtonEnabled(true);
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
				auto loadedModuleRecords = [this] {
					std::vector<PatchbayProjectModule> modules;
					modules.reserve(loadedModuleNodes_.size());
					for (const auto& node : loadedModuleNodes_)
						modules.push_back({ node->modulePath(), node->index(), node->clientId(), node->settingsJson() });
					return modules;
				};
				auto syncModuleProjectState = [canvas, loadedModuleRecords, this] {
					if (!moduleRecordsEqual(savedModuleRecords_, loadedModuleRecords()))
						canvas->markProjectDirty();
				};
				auto saveProject = [canvas, currentProject, loadedModuleRecords, this](bool saveAs, bool showError) {
					if (saveAs || currentProject->isEmpty()) {
						const auto path = QFileDialog::getSaveFileName(window_, QStringLiteral("保存接线盘工程"), *currentProject,
						                                                  QStringLiteral("WinJACKNexus 工程 (*.wjnproj)"));
						if (path.isEmpty())
							return false;
						*currentProject = path.endsWith(QStringLiteral(".wjnproj"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".wjnproj");
					}
					const auto modules = loadedModuleRecords();
					if (canvas->saveProject(*currentProject, modules)) {
						savedModuleRecords_ = modules;
						QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).setValue(QStringLiteral("lastProjectPath"), *currentProject);
						canvas->markProjectSaved();
						return true;
					}
					if (showError)
						QMessageBox::warning(window_, QStringLiteral("保存失败"), QStringLiteral("无法保存接线盘工程。"));
					return false;
				};
				auto confirmSave = [canvas, saveProject, syncModuleProjectState, window = window_.data()] {
					syncModuleProjectState();
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
				QObject::connect(newProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, this] {
					if (!confirmSave())
						return;
					canvas->newProject();
					savedModuleRecords_.clear();
					currentProject->clear();
					QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).remove(QStringLiteral("lastProjectPath"));
				});
				QObject::connect(openProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, restoreModuleRecords, loadedModuleRecords, refreshJackGraph, this] {
					const auto path = QFileDialog::getOpenFileName(window_, QStringLiteral("打开接线盘工程"), *currentProject,
					                                              QStringLiteral("WinJACKNexus 工程 (*.wjnproj)"));
					if (path.isEmpty() || !confirmSave())
						return;
					std::vector<PatchbayProjectModule> modules;
					if (canvas->loadProject(path, modules)) {
						*currentProject = path;
						QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).setValue(QStringLiteral("lastProjectPath"), path);
						restoreModuleRecords(modules);
						savedModuleRecords_ = loadedModuleRecords();
						refreshJackGraph();
					} else {
						QMessageBox::warning(window_, QStringLiteral("打开失败"), QStringLiteral("无法读取接线盘工程。"));
					}
				});
				QObject::connect(saveAction, &QAction::triggered, window_, [saveProject] { saveProject(false, true); });
				QObject::connect(saveAsAction, &QAction::triggered, window_, [saveProject] { saveProject(true, true); });
				const auto savedInterval = QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay"))
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
				QObject::connect(autoSaveTimer, &QTimer::timeout, window_, [canvas, currentProject, saveProject, syncModuleProjectState] {
					syncModuleProjectState();
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
					QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).setValue(QStringLiteral("autoSaveIntervalSeconds"), seconds);
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
			if (jackObserver_.open({ "WinJACKNexus-Patchbay", false, false, nullptr })) {
				jackObserver_.activate();
				const auto lastProject = QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).value(QStringLiteral("lastProjectPath")).toString();
				std::vector<PatchbayProjectModule> modules;
				if (!lastProject.isEmpty() && QFile::exists(lastProject) && canvas->loadProject(lastProject, modules)) {
					*currentProject = lastProject;
					restoreModuleRecords(modules);
					savedModuleRecords_ = loadedModuleRecords();
					canvas->restoreJackConnections(jackObserver_);
				}
				refreshJackGraph();
				graphRefreshTimer_ = new QTimer(window_);
				graphRefreshTimer_->setInterval(1000);
				QObject::connect(graphRefreshTimer_, &QTimer::timeout, window_, refreshJackGraph);
				graphRefreshTimer_->start();
			}

			graphMenu->addAction(QStringLiteral("刷新节点库"), window_, [nodeTree, canvas, this] {
				std::set<std::string> moduleClientIds;
				for (const auto& node : loadedModuleNodes_)
					if (node->hasClient())
						moduleClientIds.insert(node->clientId());
				populateNodeLibrary(nodeTree, canvas->snapshot(), knownJackClients_, moduleClientIds,
								   nodeTree->property("filterQuery").toString());
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
	std::set<std::string> knownJackClients_ { "External Audio Client", "DAW Bridge" };
	std::set<std::string> observedJackClients_;
	std::vector<std::unique_ptr<LoadedModuleNode>> loadedModuleNodes_;
	std::vector<PatchbayProjectModule> savedModuleRecords_;
	ModuleNodeInfo copiedModuleNode_;
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