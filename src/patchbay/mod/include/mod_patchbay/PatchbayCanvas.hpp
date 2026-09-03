#pragma once

#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

namespace WinJACKNexus::Audio {
class JackClient;
}

#include <QGraphicsView>
#include <QPointF>

#include <functional>
#include <string>

class QGraphicsPathItem;

class PatchbayCanvas final : public QGraphicsView {
public:
    explicit PatchbayCanvas(QWidget* parent = nullptr);

    void setSnapshot(const WinJACKNexus::Patchbay::Core::Snapshot& snapshot);
    const WinJACKNexus::Patchbay::Core::Snapshot& snapshot() const noexcept;
    bool saveProject(const QString& filePath) const;
    bool loadProject(const QString& filePath);
    bool restoreJackConnections(WinJACKNexus::Audio::JackClient& client);
    bool isProjectDirty() const noexcept;
    void markProjectSaved() noexcept;
    void newProject();
    void setClientAlias(const std::string& clientId, const QString& alias);
    void setClientSplit(const std::string& clientId, bool split);
    void setSearchQuery(const QString& query);
    void setNodeSelectionHandler(std::function<void(const std::string&)> handler);
    void setConnectionDisconnectHandler(std::function<void(const std::string&, const std::string&)> handler);
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
    void wheelEvent(QWheelEvent* event) override;

private:
    void activatePort(const std::string& portId);
    void applySearchFilter();
    void beginPortDrag(const std::string& portId, const QPointF& position);
    void updatePortDrag(const QPointF& position);
    void finishPortDrag(const QPointF& position);
    void updateConnectionPaths();
    void scheduleSnapshotRefresh();
    void updateClientPositions();
    void notifySelection();

    WinJACKNexus::Patchbay::Core::Snapshot snapshot_;
    QString searchQuery_;
    std::string pendingPortId_;
    std::string draggedPortId_;
    QGraphicsPathItem* portDragPreview_ = nullptr;
    std::function<void(const std::string&)> nodeSelectionHandler_;
    std::function<void(const std::string&, const std::string&)> connectionDisconnectHandler_;
    QPoint panOrigin_;
    Qt::MouseButton panningButton_ = Qt::NoButton;
    bool panning_ = false;
    bool nodeDragActive_ = false;
    bool snapshotRefreshPending_ = false;
    bool projectDirty_ = false;
};