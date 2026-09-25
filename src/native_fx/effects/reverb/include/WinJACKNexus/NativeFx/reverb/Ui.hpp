/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/NativeFx/reverb/Processor.hpp>
#include <QWidget>
#include <array>
#include <functional>

namespace WinJACKNexus::UI {
class WJN_FX_API ReverbWidget final : public QWidget {
public:
    using Parameters=WinJACKNexus::Common::ReverbParameters;
    using ChangeHandler=std::function<void(std::size_t,const Parameters&)>;
    explicit ReverbWidget(QWidget* parent=nullptr);
    void setParameters(const std::array<Parameters,8>& parameters);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel) { channel_ = std::clamp(channel, -1, 7); update(); }
    int selectedChannel() const noexcept { return channel_; }
    int hostPresetIndex() const noexcept { return preset_; }
    void chooseHostPreset(int index) { if (index < 6) choosePreset(index); }
    int selectedScene() const noexcept { return values_[channel_<0?0:channel_].scene; }
    Parameters displayedParameters() const noexcept { return values_[channel_<0?0:channel_]; }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    void edit(const std::function<void(Parameters&)>& change);
    void choosePreset(int index);
    void adjust(int control,float steps);
    int controlAt(const QPoint& point) const;
    std::array<Parameters,8> values_{};
    ChangeHandler handler_;
    int channel_=-1,preset_=1,dragged_=-1;
    QPoint dragStart_;
    Parameters dragValue_;
};
}
