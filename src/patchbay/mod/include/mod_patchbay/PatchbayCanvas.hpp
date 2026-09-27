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
#pragma once

#include <mod_patchbay/Export.hpp>
#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

namespace WinJACKNexus::Audio {
class JackClient;
}

#include <QGraphicsView>
#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QGraphicsPathItem;

struct PatchbayProjectModule final {
    QString modulePath;
    std::uint32_t index = 0;
    std::string clientId;
    std::string settingsJson;
    std::string contextId = "main";
};

struct PatchbayModuleCopyInfo final {
    QString modulePath;
    std::uint32_t index = 0;
    QString settingsJson;
};

struct PatchbaySceneContext final {
    std::string id = "main";
    QString title;
    bool isSubGraph = false;
};

struct PatchbayInterfaceInfo final {
    std::string id;
    std::string portId;
    std::string externalPortId;
    QString type;
    QString name;
    WinJACKNexus::Patchbay::Core::PortKind kind = WinJACKNexus::Patchbay::Core::PortKind::unknown;
    WinJACKNexus::Patchbay::Core::Direction direction = WinJACKNexus::Patchbay::Core::Direction::unknown;
    QPointF position;
};

struct PatchbaySubGraphInfo final {
    std::string id;
    QString name;
    QPointF position;
    std::vector<PatchbayInterfaceInfo> interfaces;
};

struct PatchbaySubGraphProject final {
    PatchbaySceneContext context;
    std::vector<PatchbaySubGraphInfo> nodes;
    std::vector<PatchbayInterfaceInfo> interfaces;
    std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> connections;
};

class MOD_PATCHBAY_API PatchbayCanvas final : public QGraphicsView {
    Q_OBJECT
public:
    explicit PatchbayCanvas(QWidget* parent = nullptr);
    PatchbayCanvas(PatchbaySceneContext context, QWidget* parent = nullptr);

    void setSnapshot(const WinJACKNexus::Patchbay::Core::Snapshot& snapshot);
    void setVisibleModuleClientIds(std::unordered_set<std::string> clientIds);
    void setHiddenModuleClientIds(std::unordered_set<std::string> clientIds);
    void setClientThemeScopes(std::unordered_map<std::string, QString> scopes);
    void setClientColorOverride(const std::string& clientId, const QColor& color);
    const WinJACKNexus::Patchbay::Core::Snapshot& snapshot() const noexcept;
    const PatchbaySceneContext& sceneContext() const noexcept;
    std::string addSubGraph(const QString& name, const QPointF& position);
    bool isSubGraphNode(const std::string& clientId) const noexcept;
    QString subGraphName(const std::string& clientId) const;
    bool renameSubGraph(const std::string& clientId, const QString& name);
    void setSubGraphInterfaces(const std::string& clientId, std::vector<PatchbayInterfaceInfo> interfaces);
    void setInterfaceNodes(std::vector<PatchbayInterfaceInfo> nodes);
    const std::vector<PatchbaySubGraphInfo>& subGraphNodes() const noexcept;
    const std::vector<PatchbayInterfaceInfo>& interfaceNodes() const noexcept;
    const std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo>& logicalConnections() const noexcept;
    void restoreSubGraphNodes(std::vector<PatchbaySubGraphInfo> nodes);
    bool restoreInterfaceNodes(std::vector<PatchbayInterfaceInfo> nodes);
    bool restoreLogicalConnections(std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> connections);
    bool addInterfaceNode(const QString& type, const QPointF& position);
    bool saveProject(const QString& filePath) const;
    bool saveProject(const QString& filePath, const std::vector<PatchbayProjectModule>& modules) const;
    bool saveProject(const QString& filePath, const std::vector<PatchbayProjectModule>& modules,
                     const std::vector<PatchbaySubGraphProject>& subGraphs) const;
    bool loadProject(const QString& filePath);
    bool loadProject(const QString& filePath, std::vector<PatchbayProjectModule>& modules);
    bool loadProject(const QString& filePath, std::vector<PatchbayProjectModule>& modules,
                     std::vector<PatchbaySubGraphProject>& subGraphs);
    QJsonObject meterbridgeProjectState() const;
    bool restoreJackConnections(WinJACKNexus::Audio::JackClient& client);
    bool isProjectDirty() const noexcept;
    void markProjectDirty() noexcept;
    void markProjectSaved() noexcept;
    void newProject();
    void setClientAlias(const std::string& clientId, const QString& alias);
    void setClientSplit(const std::string& clientId, bool split);
    std::string selectedNodeId() const;
    void selectNode(const std::string& clientId);
    void setSearchQuery(const QString& query);
    void setNodeSelectionHandler(std::function<void(const std::string&)> handler);
    void setConnectionConnectHandler(std::function<bool(const std::string&, const std::string&)> handler);
    void setConnectionDisconnectHandler(std::function<bool(const std::string&, const std::string&)> handler);
    void setSubGraphNodeDoubleClickHandler(std::function<void(const std::string&)> handler);
    void setSubGraphNodeRenamedHandler(std::function<void(const std::string&, const QString&)> handler);
    void setSubGraphNodeAddedHandler(std::function<void(const std::string&, const QString&)> handler);
    void setSubGraphNodeRemovedHandler(std::function<bool(const std::string&)> handler);
    void setInterfaceNodeRenamedHandler(std::function<void(const std::string&, const QString&)> handler);
    void setInterfaceNodeAddedHandler(std::function<bool(PatchbayInterfaceInfo&)> handler);
    void setInterfaceNodeRemovedHandler(std::function<bool(const std::string&)> handler);
    void setInterfaceNodeDropHandler(std::function<bool(const QString&, const QPointF&)> handler);
    void setSubGraphNodeDropHandler(std::function<bool(const QString&, const QPointF&)> handler);
    void setModuleNodeHandlers(std::function<bool(const std::string&)> isModuleNode,
                               std::function<bool(const std::string&)> copyModuleNode,
                               std::function<bool(const std::string&)> removeModuleNode,
                               std::function<bool(const std::string&, QWidget*)> showModuleNodeGui,
                               std::function<bool(const std::string&)> canShowModuleNodeGui,
                               std::function<bool(const QPointF&)> pasteModuleNode);
    void setModuleNodeDropHandler(std::function<bool(const QString&, std::uint32_t, const QPointF&)> handler);
    void setBoxSelectionExclusionHandler(std::function<bool(const std::string&)> handler);
    void setBatchModuleNodeHandlers(
        std::function<std::optional<PatchbayModuleCopyInfo>(const std::string&)> inspect,
        std::function<bool(const PatchbayModuleCopyInfo&, const QPointF&)> create);
    bool copySelectedNodes();
    bool pasteCopiedNodes(const QPointF& position);
    bool removeSelectedNodes();
    void setVstNodeDropHandler(std::function<bool(const QString&, std::uint32_t, const QString&, const QPointF&)> handler);
    bool isNodeDragActive() const noexcept;
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    static void applyTheme(bool light);

Q_SIGNALS:
    void zoomChanged(qreal factor);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRect overviewRect() const;
    QRectF overviewWorldBounds() const;
    QRectF overviewViewportRect() const;
    QPointF overviewScenePoint(const QPoint& point) const;
    void activatePort(const std::string& portId);
    void applySearchFilter();
    void beginPortDrag(const std::string& portId, const QPointF& position);
    void updatePortDrag(const QPointF& position);
    void finishPortDrag(const QPointF& position);
    void beginConnectionDrag(const std::string& sourcePortId,
                             const std::string& destinationPortId,
                             const QPointF& position);
    void updateConnectionDrag(const QPointF& position);
    void finishConnectionDrag(const QPointF& position);
    void updateConnectionPaths();
    void scheduleSnapshotRefresh();
    void updateClientPositions();
    void notifySelection();
    void cancelDragState();
    void refreshTheme();
    bool connectPortPair(const std::string& sourcePortId, const std::string& destinationPortId);
    bool disconnectPortPair(const std::string& sourcePortId, const std::string& destinationPortId);
    void rememberLogicalConnection(const WinJACKNexus::Patchbay::Core::ConnectionInfo& connection);
    void forgetLogicalConnection(const std::string& sourcePortId, const std::string& destinationPortId);
    void forgetSavedJackConnection(const std::string& sourcePortId, const std::string& destinationPortId);
    void retryPendingLogicalConnections();

    WinJACKNexus::Patchbay::Core::Snapshot snapshot_;
    WinJACKNexus::Patchbay::Core::Snapshot renderedSnapshot_;
    PatchbaySceneContext sceneContext_;
    std::vector<PatchbaySubGraphInfo> subGraphNodes_;
    std::vector<PatchbayInterfaceInfo> interfaceNodes_;
    std::unordered_set<std::string> visibleModuleClientIds_;
    std::unordered_set<std::string> hiddenModuleClientIds_;
    std::unordered_map<std::string, QString> clientThemeScopes_;
    std::unordered_map<std::string, QColor> clientColorOverrides_;
    QString searchQuery_;
    std::string pendingPortId_;
    std::string selectedNodeId_;
    std::string draggedPortId_;
    std::string draggedConnectionSourcePortId_;
    std::string draggedConnectionDestinationPortId_;
    std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> logicalConnections_;
    std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> pendingLogicalConnections_;
    std::vector<WinJACKNexus::Patchbay::Core::ConnectionInfo> savedJackConnections_;
    QGraphicsPathItem* portDragPreview_ = nullptr;
    QGraphicsPathItem* draggedConnectionLine_ = nullptr;
    std::function<void(const std::string&)> nodeSelectionHandler_;
    std::function<bool(const std::string&, const std::string&)> connectionConnectHandler_;
    std::function<bool(const std::string&, const std::string&)> connectionDisconnectHandler_;
    std::function<void(const std::string&)> subGraphNodeDoubleClickHandler_;
    std::function<void(const std::string&, const QString&)> subGraphNodeRenamedHandler_;
    std::function<void(const std::string&, const QString&)> subGraphNodeAddedHandler_;
    std::function<bool(const std::string&)> subGraphNodeRemovedHandler_;
    std::function<void(const std::string&, const QString&)> interfaceNodeRenamedHandler_;
    std::function<bool(PatchbayInterfaceInfo&)> interfaceNodeAddedHandler_;
    std::function<bool(const std::string&)> interfaceNodeRemovedHandler_;
    std::function<bool(const QString&, const QPointF&)> interfaceNodeDropHandler_;
    std::function<bool(const QString&, const QPointF&)> subGraphNodeDropHandler_;
    std::function<bool(const std::string&)> isModuleNode_;
    std::function<bool(const std::string&)> excludeBoxSelection_;
    std::function<std::optional<PatchbayModuleCopyInfo>(const std::string&)> inspectModuleNode_;
    std::function<bool(const PatchbayModuleCopyInfo&, const QPointF&)> createCopiedModuleNode_;
    struct CopiedNode final {
        enum class Kind { module, subGraph, interfaceNode } kind = Kind::module;
        PatchbayModuleCopyInfo module;
        QString detail;
        QPointF offset;
    };
    std::vector<CopiedNode> copiedNodes_;
    std::function<bool(const std::string&)> copyModuleNode_;
    std::function<bool(const std::string&)> removeModuleNode_;
    std::function<bool(const std::string&, QWidget*)> showModuleNodeGui_;
    std::function<bool(const std::string&)> canShowModuleNodeGui_;
    std::function<bool(const QPointF&)> pasteModuleNode_;
    std::function<bool(const QString&, std::uint32_t, const QPointF&)> moduleNodeDropHandler_;
    std::function<bool(const QString&, std::uint32_t, const QString&, const QPointF&)> vstNodeDropHandler_;
    QString copiedSubGraphName_;
    QString copiedInterfaceType_;
    QPoint panOrigin_;
    QPoint rightBoxOrigin_;
    QPoint rightBoxCurrent_;
    int rightPressSerial_ = 0;
    bool rightHoldPending_ = false;
    bool rightBoxSelecting_ = false;
    bool overviewPanning_ = false;
    QRectF overviewDragWorld_;
    QPointF overviewDragOffset_;
    bool suppressNextContextMenu_ = false;
    Qt::MouseButton panningButton_ = Qt::NoButton;
    bool panning_ = false;
    bool nodeDragActive_ = false;
    bool connectionDragActive_ = false;
    bool connectionDragMoved_ = false;
    bool snapshotRefreshPending_ = false;
    bool projectDirty_ = false;
    QJsonObject meterbridgeProjectState_;
    std::uint8_t logicalConnectionRestoreRetries_ = 0;
    std::uint8_t jackConnectionRestoreRetries_ = 0;
};
