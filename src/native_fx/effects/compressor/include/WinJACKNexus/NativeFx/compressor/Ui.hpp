/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>
#include <WinJACKNexus/NativeFx/compressor/Processor.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <array>
#include <cstdint>

namespace WinJACKNexus::UI
{
using CompressorParameters = WinJACKNexus::Common::CompressorParameters;

struct CompressorMeter final
{
    float inputDb = -120.0F;
    float outputDb = -120.0F;
    bool active = false;
    float gainReductionDb = 0.0F;
};

class WJN_FX_API CompressorWidget final : public QWidget
{
  public:
    using ChangeHandler = std::function<void(std::size_t, const CompressorParameters &)>;
    using MeterReader = std::function<std::array<CompressorMeter, 8>()>;
    explicit CompressorWidget(QWidget *parent = nullptr);
    void setParameters(const std::array<CompressorParameters, 8> &values);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel)
    {
        selectedChannel_ = std::clamp(channel, -1, 7);
        update();
    }
    int hostPresetIndex() const noexcept
    {
        return selectedPreset_ < 0 ? 3 : selectedPreset_;
    }
    void chooseHostPreset(int index)
    {
        if (index < 3)
            applyPreset(index);
    }
    void setMeterReader(MeterReader reader);
    void setMeters(const std::array<CompressorMeter, 8> &meters);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    CompressorParameters displayed() const noexcept;
    void apply(const std::function<void(CompressorParameters &)> &edit);
    int dialAt(QPointF point) const noexcept;
    void changeDial(int index, float amount);
    void applyPreset(int index);
    void pollMeters();
    std::array<CompressorParameters, 8> parameters_{};
    std::array<CompressorMeter, 8> meters_{};
    ChangeHandler changeHandler_;
    MeterReader meterReader_;
    QTimer meterTimer_;
    int selectedChannel_ = -1, selectedPreset_ = 0, draggedDial_ = -1;
    QPoint dragStart_;
    CompressorParameters dragParameters_;
};
}
