#pragma once

#include <WinJACKNexus/Patchbay/Core/Snapshot.hpp>

#include <QGraphicsView>

class PatchbayCanvas final : public QGraphicsView {
public:
    explicit PatchbayCanvas(QWidget* parent = nullptr);

    void setSnapshot(const WinJACKNexus::Patchbay::Core::Snapshot& snapshot);
    void loadDemoGraph();

    static void applyTheme(bool light);
};