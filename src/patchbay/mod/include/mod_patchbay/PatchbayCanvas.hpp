#pragma once

#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <QGraphicsView>

#include <string>

class PatchbayCanvas final : public QGraphicsView {
public:
    explicit PatchbayCanvas(QWidget* parent = nullptr);

    void setSnapshot(const WinJACKNexus::Patchbay::Core::Snapshot& snapshot);
    void loadDemoGraph();
    void setSearchQuery(const QString& query);
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
    void updateClientPositions();

    WinJACKNexus::Patchbay::Core::Snapshot snapshot_;
    QString searchQuery_;
    std::string pendingPortId_;
    QPoint panOrigin_;
    bool panning_ = false;
};