#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QWidget>

#include <memory>

namespace WinJACKNexus::UI {

class WINJACK_UI_API MixerWindow final : public QWidget {
public:
    explicit MixerWindow(QWidget* parent = nullptr);
    ~MixerWindow() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}