#pragma once

#include <WinJACKNexus/Common/AuxMixerState.hpp>
#include <WinJACKNexus/Common/MixerPolarCurve.hpp>
#include <WinJACKNexus/UI/Export.hpp>

#include <QWidget>

#include <functional>
#include <memory>

namespace WinJACKNexus::UI {

class WINJACK_UI_API AuxSettingsWindow final : public QWidget {
public:
    explicit AuxSettingsWindow(std::shared_ptr<Common::AuxMixerState> state, QWidget* parent = nullptr);
    ~AuxSettingsWindow() override;

    void setSource(const QString& sourceKey, const QString& sourceName, std::uint32_t selfAuxId = 0,
                   Common::AuxChannelLayout layout = Common::AuxChannelLayout::stereo,
                   const QString& sourceNumber = {});
    void selectBus(std::uint32_t id);
    void addBus(Common::AuxChannelLayout layout);
    std::uint32_t selectedBusId() const;
    void setStateChangedCallback(std::function<void()> callback);
    void setStateCommitCallback(std::function<bool()> callback);
    void setMeterReadCallback(std::function<float(bool)> callback);
    void setPolarReadCallback(std::function<bool(std::uint32_t,
                               Common::MixerPolarSnapshot&)> callback);
    void acceptExternalState();
    void setChainEnabled(bool enabled);
    bool chainEnabled() const;
    void setChainEnabledHandler(std::function<void(bool)> handler);
    void setChainCommitHandler(std::function<bool(bool)> handler);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
