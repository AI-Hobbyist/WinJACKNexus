#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QActionGroup>
#include <QCheckBox>
#include <QDockWidget>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QTreeWidget>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <set>

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

void populateNodeLibrary(QTreeWidget* tree,
                         const WinJACKNexus::Patchbay::Core::Snapshot& snapshot,
                         const std::set<std::string>& knownJackClients)
{
	tree->clear();
	auto* jackClients = addCategory(tree, QStringLiteral("jackclient"));
	std::set<std::string> onlineClients;
	const std::set<std::string> builtInClients { "system", "WinJACKNexus-Patchbay" };
	for (const auto& client : snapshot.clients)
	{
		if (client.id.starts_with("placeholder-") || builtInClients.contains(client.name))
			continue;
		addNodeItem(jackClients, QString::fromStdString(client.name), client.alive);
		onlineClients.insert(client.name);
	}
	for (const auto& client : knownJackClients) {
		if (!onlineClients.contains(client))
			addNodeItem(jackClients, QString::fromStdString(client), false);
	}
}

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
			auto* nodeTree = new QTreeWidget(nodePanel);
			nodeTree->setHeaderHidden(true);
			nodeTree->setIndentation(14);
			nodeTree->setAnimated(true);
			nodeLayout->addWidget(nodeFilter);
			nodeLayout->addWidget(nodeTree, 1);
			nodeDock->setWidget(nodePanel);
			window_->addDockWidget(Qt::LeftDockWidgetArea, nodeDock);
			populateNodeLibrary(nodeTree, canvas->snapshot(), knownJackClients_);

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
			auto* aliasEditor = new QLineEdit(properties);
			aliasEditor->setPlaceholderText(QStringLiteral("显示别名"));
			auto* splitCheck = new QCheckBox(QStringLiteral("拆分输入和输出"), properties);
			propertyForm->addRow(QStringLiteral("类别"), categoryValue);
			propertyForm->addRow(QStringLiteral("别名"), aliasEditor);
			propertyForm->addRow(QString(), splitCheck);
			properties->setEnabled(false);
			inspectorLayout->addWidget(selectionTitle);
			inspectorLayout->addWidget(properties);
			inspectorLayout->addStretch(1);
			inspectorDock->setWidget(inspector);
			window_->addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

			std::shared_ptr<std::string> selectedClient = std::make_shared<std::string>();
			canvas->setNodeSelectionHandler([canvas, selectionTitle, categoryValue, aliasEditor, splitCheck, properties, selectedClient](const std::string& clientId) {
				*selectedClient = clientId;
				const auto client = std::find_if(canvas->snapshot().clients.cbegin(), canvas->snapshot().clients.cend(), [&clientId](const auto& value) {
					return value.id == clientId;
				});
				const bool selected = client != canvas->snapshot().clients.cend();
				properties->setEnabled(selected);
				selectionTitle->setText(selected ? QString::fromStdString(client->name) : QString());
				categoryValue->setText(selected ? QString::fromStdString(client->iconName) : QString());
				aliasEditor->setText(selected ? QString::fromStdString(client->alias) : QString());
				splitCheck->setChecked(selected && client->split);
			});
			QObject::connect(aliasEditor, &QLineEdit::editingFinished, canvas, [canvas, selectedClient, aliasEditor] {
				if (!selectedClient->empty())
					canvas->setClientAlias(*selectedClient, aliasEditor->text());
			});
			QObject::connect(splitCheck, &QCheckBox::toggled, canvas, [canvas, selectedClient](bool checked) {
				if (!selectedClient->empty())
					canvas->setClientSplit(*selectedClient, checked);
			});

			QObject::connect(nodeFilter, &QLineEdit::textChanged, nodeTree, [nodeTree](const QString& query) {
				const auto needle = query.trimmed();
				for (int index = 0; index < nodeTree->topLevelItemCount(); ++index) {
					auto* category = nodeTree->topLevelItem(index);
					bool categoryMatch = needle.isEmpty() || category->text(0).contains(needle, Qt::CaseInsensitive);
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
				auto saveProject = [canvas, currentProject, this](bool saveAs, bool showError) {
					if (saveAs || currentProject->isEmpty()) {
						const auto path = QFileDialog::getSaveFileName(window_, QStringLiteral("保存接线盘工程"), *currentProject,
						                                                  QStringLiteral("WinJACKNexus 工程 (*.wjnproj)"));
						if (path.isEmpty())
							return false;
						*currentProject = path.endsWith(QStringLiteral(".wjnproj"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".wjnproj");
					}
					if (canvas->saveProject(*currentProject)) {
						QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).setValue(QStringLiteral("lastProjectPath"), *currentProject);
						canvas->markProjectSaved();
						return true;
					}
					if (showError)
						QMessageBox::warning(window_, QStringLiteral("保存失败"), QStringLiteral("无法保存接线盘工程。"));
					return false;
				};
				auto confirmSave = [canvas, saveProject, window = window_.data()] {
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
				QObject::connect(newProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave] {
					if (!confirmSave())
						return;
					canvas->newProject();
					currentProject->clear();
					QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).remove(QStringLiteral("lastProjectPath"));
				});
				QObject::connect(openProject, &QAction::triggered, window_, [canvas, currentProject, confirmSave, this] {
					const auto path = QFileDialog::getOpenFileName(window_, QStringLiteral("打开接线盘工程"), *currentProject,
					                                              QStringLiteral("WinJACKNexus 工程 (*.wjnproj)"));
					if (path.isEmpty() || !confirmSave())
						return;
					if (canvas->loadProject(path)) {
						*currentProject = path;
						QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).setValue(QStringLiteral("lastProjectPath"), path);
						canvas->restoreJackConnections(jackObserver_);
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
				QObject::connect(autoSaveTimer, &QTimer::timeout, window_, [canvas, currentProject, saveProject] {
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
			auto refreshJackGraph = [this, canvas, nodeTree] {
				if (canvas->isNodeDragActive())
					return;
				if (!jackObserver_.isOpen())
					return;
				auto snapshot = WinJACKNexus::Patchbay::Core::Snapshot::capture(jackObserver_);
				ensureSystemClient(snapshot);
				for (const auto& connection : canvas->snapshot().connections) {
					const bool exists = std::any_of(snapshot.connections.cbegin(), snapshot.connections.cend(), [&connection](const auto& current) {
						return current.sourcePortId == connection.sourcePortId && current.destinationPortId == connection.destinationPortId;
					});
					if (!exists)
						snapshot.connections.push_back(connection);
				}
				canvas->restoreJackConnections(jackObserver_);
				for (const auto& client : snapshot.clients)
					knownJackClients_.insert(client.name);
				canvas->setSnapshot(snapshot);
				populateNodeLibrary(nodeTree, canvas->snapshot(), knownJackClients_);
			};
			if (jackObserver_.open({ "WinJACKNexus-Patchbay", false, false, nullptr })) {
				jackObserver_.activate();
				const auto lastProject = QSettings(QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay")).value(QStringLiteral("lastProjectPath")).toString();
				if (!lastProject.isEmpty() && QFile::exists(lastProject) && canvas->loadProject(lastProject)) {
					*currentProject = lastProject;
					canvas->restoreJackConnections(jackObserver_);
				}
				refreshJackGraph();
				graphRefreshTimer_ = new QTimer(window_);
				graphRefreshTimer_->setInterval(1000);
				QObject::connect(graphRefreshTimer_, &QTimer::timeout, window_, refreshJackGraph);
				graphRefreshTimer_->start();
			}

			graphMenu->addAction(QStringLiteral("刷新节点库"), window_, [nodeTree, canvas, this] {
				populateNodeLibrary(nodeTree, canvas->snapshot(), knownJackClients_);
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
	bool initialized_ = false;
	QPointer<QMainWindow> window_;
	WinJACKNexus::Audio::JackClient jackObserver_;
	QPointer<QTimer> graphRefreshTimer_;
	std::set<std::string> knownJackClients_ { "External Audio Client", "DAW Bridge" };
};

}

const char* wjn_get_node_api_version(void)
{
	return "1";
}

const char* wjn_get_node_name(void)
{
	return "Patchbay";
}

const char* wjn_get_node_category(void)
{
	return "Patchbay";
}

WinJACKNexus::Node::INode* wjn_create_node(void)
{
	return new PatchbayNode;
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
	delete node;
}