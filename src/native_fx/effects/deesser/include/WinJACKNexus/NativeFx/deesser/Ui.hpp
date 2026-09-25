/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>

#include <WinJACKNexus/NativeFx/deesser/Processor.hpp>
#include <WinJACKNexus/NativeFx/deesser/SpectrumSample.hpp>
#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>

#include <QWidget>
#include <QTimer>

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <functional>

namespace WinJACKNexus::UI {

class WJN_FX_API DeEsserWidget final : public QWidget {
public:
    using Parameters = WinJACKNexus::Common::DeEsserParameters;
    using ChangeHandler = std::function<void(std::size_t, const Parameters&)>;
    explicit DeEsserWidget(QWidget* parent = nullptr);
    void setParameters(const std::array<Parameters, 8>& parameters);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel) { selectedChannel_ = std::clamp(channel, -1, 7); update(); }
    int hostPresetIndex() const noexcept { return selectedPreset_; }
    void chooseHostPreset(int index) { if (index < 3) selectPreset(index); }
    void setSpectrumSources(
        const std::array<WinJACKNexus::Common::AudioRingBuffer<
            WinJACKNexus::Common::DeEsserSpectrumSample>*, 8>& sources,
        const std::atomic<double>* sampleRate) noexcept;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    Parameters displayed() const noexcept;
    void apply(const std::function<void(Parameters&)>& edit);
    void selectPreset(int index);
    void setKnobFromY(int index, qreal y);
    void refreshSpectrum();
    void addSpectrumSamples(std::size_t channel,
                            const WinJACKNexus::Common::DeEsserSpectrumSample* samples,
                            std::size_t count) noexcept;
    void calculateSpectrum();
    static void transform(std::array<std::complex<float>, 1024>& values) noexcept;
    static constexpr std::size_t spectrumFftSize_ = 1024;
    static constexpr std::size_t spectrumBinCount_ = spectrumFftSize_ / 2 + 1;
    std::array<Parameters, 8> parameters_ {};
    ChangeHandler changeHandler_;
    std::array<WinJACKNexus::Common::AudioRingBuffer<
        WinJACKNexus::Common::DeEsserSpectrumSample>*, 8> spectrumSources_ {};
    const std::atomic<double>* spectrumSampleRate_ = nullptr;
    std::array<std::array<float, spectrumFftSize_>, 8> inputSpectrumHistory_ {};
    std::array<std::array<float, spectrumFftSize_>, 8> processedSpectrumHistory_ {};
    std::array<std::size_t, 8> historyWrite_ {};
    std::array<std::size_t, 8> historyCount_ {};
    std::array<float, spectrumFftSize_> window_ {};
    std::array<float, spectrumBinCount_> inputSpectrumDb_ {};
    std::array<float, spectrumBinCount_> spectrumDb_ {};
    bool spectrumReady_ = false;
    double lastSpectrumSampleRate_ = 0.0;
    QTimer spectrumTimer_;
    int selectedChannel_ = -1;
    int draggingKnob_ = -1;
    int selectedPreset_ = 0;
    qreal dragStartY_ = 0.0;
    float dragStartNormalized_ = 0.0F;
};

}
