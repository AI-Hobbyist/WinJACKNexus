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

#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

namespace WinJACKNexus::Audio {
class JackClient;
}

#include <QGraphicsView>
#include <QPointF>
#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class QGraphicsPathItem;

struct PatchbayProjectModule final {
    QString modulePath;
    std::uint32_t index = 0;
    std::string clientId;
    std::string settingsJson;
};

struct PatchbaySceneContext final {
    std::string id = "main";
    QString title;
    bool isSubGraph = false;
};

struct PatchbaySubGraphInfo final {
    std::string id;
    QString name;
    QPointF position;
};

struct PatchbayInterfaceInfo final {
    std::string id;
    std::string portId;
    QString type;
    QString name;
    WinJACKNexus::Patchbay::Core::PortKind kind = WinJACKNexus::Patchbay::Core::PortKind::unknown;
    WinJACKNexus::Patchbay::Core::Direction direction = WinJACKNexus::Patchbay::Core::Direction::unknown;
    QPointF position;
};

class PatchbayCanvas final : public QGraphicsView {
public:
    explicit PatchbayCanvas(QWidget* parent = nullptr);
    PatchbayCanvas(PatchbaySceneContext context, QWidget* parent = nullptr);

    void setSnapshot(const WinJACKNexus::Patchbay::Core::Snapshot& snapshot);
    const WinJACKNexus::Patchbay::Core::Snapshot& snapshot() const noexcept;
    const PatchbaySceneContext& sceneContext() const noexcept;
    std::string addSubGraph(const QString& name, const QPointF& position);
    bool isSubGraphNode(const std::string& clientId) const noexcept;
    QString subGraphName(const std::string& clientId) const;
    bool renameSubGraph(const std::string& clientId, const QString& name);
    void setInterfaceNodes(std::vector<PatchbayInterfaceInfo> nodes);
    bool addInterfaceNode(const QString& type, const QPointF& position);
    bool saveProject(const QString& filePath) const;
    bool saveProject(const QString& filePath, const std::vector<PatchbayProjectModule>& modules) const;
    bool loadProject(const QString& filePath);
    bool loadProject(const QString& filePath, std::vector<PatchbayProjectModule>& modules);
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
    void setConnectionDisconnectHandler(std::function<void(const std::string&, const std::string&)> handler);
    void setSubGraphNodeDoubleClickHandler(std::function<void(const std::string&)> handler);
    void setSubGraphNodeRenamedHandler(std::function<void(const std::string&, const QString&)> handler);
    void setSubGraphNodeAddedHandler(std::function<void(const std::string&, const QString&)> handler);
    void setInterfaceNodeRenamedHandler(std::function<void(const std::string&, const QString&)> handler);
    void setInterfaceNodeAddedHandler(std::function<bool(PatchbayInterfaceInfo&)> handler);
    void setInterfaceNodeDropHandler(std::function<bool(const QString&, const QPointF&)> handler);
    void setSubGraphNodeDropHandler(std::function<bool(const QString&, const QPointF&)> handler);
    void setModuleNodeHandlers(std::function<bool(const std::string&)> isModuleNode,
                               std::function<bool(const std::string&)> copyModuleNode,
                               std::function<bool(const std::string&)> removeModuleNode,
                               std::function<bool(const std::string&, QWidget*)> showModuleNodeGui,
                               std::function<bool(const QPointF&)> pasteModuleNode);
    void setModuleNodeDropHandler(std::function<bool(const QString&, std::uint32_t, const QPointF&)> handler);
    bool isNodeDragActive() const noexcept;
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();

    static void applyTheme(bool light);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
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

    WinJACKNexus::Patchbay::Core::Snapshot snapshot_;
    PatchbaySceneContext sceneContext_;
    std::vector<PatchbaySubGraphInfo> subGraphNodes_;
    std::vector<PatchbayInterfaceInfo> interfaceNodes_;
    QString searchQuery_;
    std::string pendingPortId_;
    std::string draggedPortId_;
    std::string draggedConnectionSourcePortId_;
    std::string draggedConnectionDestinationPortId_;
    QGraphicsPathItem* portDragPreview_ = nullptr;
    QGraphicsPathItem* draggedConnectionLine_ = nullptr;
    std::function<void(const std::string&)> nodeSelectionHandler_;
    std::function<bool(const std::string&, const std::string&)> connectionConnectHandler_;
    std::function<void(const std::string&, const std::string&)> connectionDisconnectHandler_;
    std::function<void(const std::string&)> subGraphNodeDoubleClickHandler_;
    std::function<void(const std::string&, const QString&)> subGraphNodeRenamedHandler_;
    std::function<void(const std::string&, const QString&)> subGraphNodeAddedHandler_;
    std::function<void(const std::string&, const QString&)> interfaceNodeRenamedHandler_;
    std::function<bool(PatchbayInterfaceInfo&)> interfaceNodeAddedHandler_;
    std::function<bool(const QString&, const QPointF&)> interfaceNodeDropHandler_;
    std::function<bool(const QString&, const QPointF&)> subGraphNodeDropHandler_;
    std::function<bool(const std::string&)> isModuleNode_;
    std::function<bool(const std::string&)> copyModuleNode_;
    std::function<bool(const std::string&)> removeModuleNode_;
    std::function<bool(const std::string&, QWidget*)> showModuleNodeGui_;
    std::function<bool(const QPointF&)> pasteModuleNode_;
    std::function<bool(const QString&, std::uint32_t, const QPointF&)> moduleNodeDropHandler_;
    QPoint panOrigin_;
    Qt::MouseButton panningButton_ = Qt::NoButton;
    bool panning_ = false;
    bool nodeDragActive_ = false;
    bool connectionDragActive_ = false;
    bool connectionDragMoved_ = false;
    bool snapshotRefreshPending_ = false;
    bool projectDirty_ = false;
};