#pragma once
#include <algorithm>
#include <WinJACKNexus/NativeFx/limiter/Processor.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/UI/Controls.hpp>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <array>
#include <functional>

namespace WinJACKNexus::UI {
using LimiterParameters=WinJACKNexus::Common::LimiterParameters;
using LimiterMeter=WinJACKNexus::Common::LimiterMeter;
class WJN_FX_API LimiterWidget final : public QWidget {
public:
    using ChangeHandler=std::function<void(std::size_t,const LimiterParameters&)>;
    using MeterReader=std::function<std::array<LimiterMeter,8>()>;
    explicit LimiterWidget(QWidget* parent=nullptr);
    void setParameters(const std::array<LimiterParameters,8>& values);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel) { channel_ = std::clamp(channel, -1, 7); numericClock_.invalidate(); update(); }
    void setMeterReader(MeterReader reader);
    void setMeters(const std::array<LimiterMeter,8>& meters);
    int selectedChannel() const noexcept { return channel_; }
    int hostPresetIndex() const noexcept { return preset_; }
    void chooseHostPreset(int index) { applyPreset(index); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    LimiterParameters shown() const noexcept;
    void apply(const std::function<void(LimiterParameters&)>& edit);
    void applyPreset(int index);
    int dialAt(QPointF p) const noexcept;
    void adjustDial(int index,float delta);
    void pollMeters();
    std::array<LimiterParameters,8> values_ {};
    std::array<LimiterMeter,8> meters_ {};
    std::array<Controls::MeterBallistics,8> inputBallistics_ {};
    std::array<Controls::MeterBallistics,8> outputBallistics_ {};
    QElapsedTimer meterClock_,numericClock_;
    float numericInputDb_=-120.0F,numericOutputDb_=-120.0F;
    bool numericMeterActive_=false;
    ChangeHandler onChange_;
    MeterReader meterReader_;
    QTimer meterTimer_;
    int channel_=-1,preset_=0,drag_=-1;
    QPoint dragPoint_;
};
}
