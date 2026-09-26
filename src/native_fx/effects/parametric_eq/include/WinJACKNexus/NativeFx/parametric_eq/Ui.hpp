/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>
#include <WinJACKNexus/NativeFx/parametric_eq/Processor.hpp>
#include <WinJACKNexus/NativeFx/graphic_eq/SpectrumAnalyzer.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <QTimer>
#include <QElapsedTimer>
#include <QWidget>
#include <array>
#include <functional>

namespace WinJACKNexus::UI {
class WJN_FX_API ParametricEqWidget final : public QWidget {
public:
    using Parameters = WinJACKNexus::Common::ParametricEqParameters;
    using ChangeHandler = std::function<void(std::size_t, const Parameters&)>;
    using SpectrumFrameReader = std::function<bool(WinJACKNexus::Common::GraphicEqSpectrumFrame&)>;
    explicit ParametricEqWidget(QWidget* parent = nullptr);
    void setParameters(const std::array<Parameters, 8>& parameters);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel) { selectedChannel_ = std::clamp(channel, -1, 7); refreshPresetSelection(); update(); }
    void setSpectrumFrameReader(SpectrumFrameReader reader);
    int selectedChannel() const noexcept { return selectedChannel_; }
    int hostPresetIndex() const noexcept { return selectedPreset_; }
    void chooseHostPreset(int index) { if (index < 5) selectPreset(index); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    Parameters displayed() const noexcept;
    void apply(const std::function<void(Parameters&)>& edit);
    void selectPreset(int index);
    void refreshPresetSelection();
    void applyKnobValue(int index, float value);
    void pollSpectrum();
    void editGraph(const QPointF& pos);
    void editKnob(const QPointF& pos);
    QPointF designPoint(const QPointF& pos) const;
    std::array<Parameters, 8> parameters_ {};
    ChangeHandler changeHandler_;
    SpectrumFrameReader spectrumFrameReader_;
    WinJACKNexus::Common::GraphicEqSpectrumFrame spectrumFrame_ {};
    WinJACKNexus::Common::GraphicEqSpectrumPair spectrum_ {};
    QTimer spectrumTimer_;
    QElapsedTimer spectrumClock_;
    int selectedChannel_ = -1;
    int selectedPreset_ = 0;
    int dragBand_ = -1;
    int dragKnob_ = -1;
    QPointF lastMouse_;
    bool showSpectrum_ = true, showResponse_ = true, showLabels_ = true, showGrid_ = true;
    int fftSize_ = 4096, smoothing_ = 1, displayRange_ = 18;
    bool hasSpectrum_ = false;
};
}
