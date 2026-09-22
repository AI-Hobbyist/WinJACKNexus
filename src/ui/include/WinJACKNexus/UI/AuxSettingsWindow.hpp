#pragma once

#include <WinJACKNexus/Common/AuxMixerState.hpp>
#include <WinJACKNexus/UI/Export.hpp>

#include <QWidget>

#include <functional>
#include <memory>

namespace WinJACKNexus::UI {

class WINJACK_UI_API AuxSettingsWindow final : public QWidget {
public:
    explicit AuxSettingsWindow(std::shared_ptr<Common::AuxMixerState> state, QWidget* parent = nullptr);
    ~AuxSettingsWindow() override;

    void setSource(const QString& sourceKey, const QString& sourceName, std::uint32_t selfAuxId = 0);
    void selectBus(std::uint32_t id);
    void setStateChangedCallback(std::function<void()> callback);

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