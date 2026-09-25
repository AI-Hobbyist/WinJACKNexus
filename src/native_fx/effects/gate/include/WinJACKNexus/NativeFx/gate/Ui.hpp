/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>
#include <WinJACKNexus/NativeFx/gate/Processor.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <QWidget>
#include <array>
#include <functional>

namespace WinJACKNexus::UI {
class WJN_FX_API GateEffectWidget final : public QWidget {
public:
    using Parameters = WinJACKNexus::Common::GateParameters;
    using ChangeHandler = std::function<void(std::size_t, const Parameters&)>;
    explicit GateEffectWidget(QWidget* parent = nullptr);
    void setParameters(const std::array<Parameters, 8>& parameters);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel) { selectedChannel_ = std::clamp(channel, -1, 7); update(); }
    int hostPresetIndex() const noexcept { return selectedPreset_ < 0 ? 3 : selectedPreset_; }
    void chooseHostPreset(int index) { if (index < 3) applyPreset(index); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    Parameters displayed() const noexcept;
    void apply(const std::function<void(Parameters&)>& edit);
    void adjustDial(int index, float steps);
    int dialAt(const QPointF& point) const noexcept;
    void applyPreset(int index);
    std::array<Parameters, 8> parameters_ {};
    ChangeHandler changeHandler_;
    int selectedChannel_ = -1;
    int selectedPreset_ = 0;
    int draggedDial_ = -1;
    QPoint dragStart_;
    Parameters dragParameters_;
};
}
