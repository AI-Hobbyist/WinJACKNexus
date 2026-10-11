/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>

#include <WinJACKNexus/NativeFx/graphic_eq/Processor.hpp>
#include <WinJACKNexus/NativeFx/graphic_eq/SpectrumAnalyzer.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>

#include <QTimer>
#include <QElapsedTimer>
#include <QWidget>

#include <array>
#include <functional>

namespace WinJACKNexus::UI
{

class WJN_FX_API GraphicEqWidget final : public QWidget
{
  public:
    using Parameters = WinJACKNexus::Common::GraphicEqParameters;
    using ChangeHandler = std::function<void(std::size_t, const Parameters &)>;
    using SpectrumFrameReader = std::function<bool(WinJACKNexus::Common::GraphicEqSpectrumFrame &)>;
    explicit GraphicEqWidget(QWidget *parent = nullptr);
    void setParameters(const std::array<Parameters, 8> &parameters);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel)
    {
        selectedChannel_ = std::clamp(channel, -1, 7);
        update();
    }
    int hostPresetIndex() const noexcept
    {
        return selectedPreset_;
    }
    bool hasSpectrumFrame() const noexcept
    {
        return hasSpectrumFrame_;
    }
    std::uint64_t spectrumFrameCount() const noexcept
    {
        return spectrumFrameCount_;
    }
    void chooseHostPreset(int index)
    {
        selectPreset(index);
    }
    void setSpectrumFrameReader(SpectrumFrameReader reader);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    Parameters displayed() const noexcept;
    void apply(const std::function<void(Parameters &)> &edit);
    void pollSpectrumFrame();
    void refreshSpectrumForSelection(bool smooth);
    void selectPreset(int index);
    void setBandAt(const QPointF &point);
    void loadPresetFile();
    void savePresetFile();
    std::array<Parameters, 8> parameters_{};
    ChangeHandler changeHandler_;
    SpectrumFrameReader spectrumFrameReader_;
    WinJACKNexus::Common::GraphicEqSpectrumFrame spectrumFrame_{};
    WinJACKNexus::Common::GraphicEqSpectrumPair spectrum_{};
    QTimer spectrumTimer_;
    QElapsedTimer spectrumClock_;
    int selectedChannel_ = -1;
    int selectedPreset_ = 0;
    int draggingBand_ = -1;
    int selectedBand_ = 0;
    int viewMode_ = 0;
    bool showFrequencyValues_ = true;
    bool showCurve_ = true;
    bool hasSpectrumFrame_ = false;
    std::uint64_t spectrumFrameCount_{};
};

}
