/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/deesser/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QMouseEvent>
#include <QMenu>
#include <QToolTip>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <utility>

namespace WinJACKNexus::UI {
namespace {
const QColor background(9, 23, 39), panel(13, 29, 47), border(36, 102, 157);
const QColor text(222, 235, 249), muted(143, 166, 189), purple(165, 91, 255);
const std::array<QString, 9> channels { QStringLiteral("All"), QStringLiteral("L"),
    QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"), QStringLiteral("Ls"),
    QStringLiteral("Rs"), QStringLiteral("Lb"), QStringLiteral("Rb") };
const std::array<QString, 6> captions { QStringLiteral("中心频率"), QStringLiteral("带宽 (Q)"),
    QStringLiteral("阈值"), QStringLiteral("起始时间"), QStringLiteral("释放时间"), QStringLiteral("输出增益") };
const std::array<QPointF, 6> knobs { QPointF(480, 142), QPointF(587, 142), QPointF(694, 142),
    QPointF(480, 273), QPointF(587, 273), QPointF(694, 273) };

void rounded(QPainter& p, const QRectF& r, qreal radius, const QColor& fill,
             const QColor& outline = Qt::transparent)
{
    p.setBrush(fill);
    p.setPen(outline == Qt::transparent ? Qt::NoPen : QPen(outline, 1.0));
    p.drawRoundedRect(r, radius, radius);
}

void textAt(QPainter& p, const QRectF& r, const QString& s, int px, const QColor& color,
            Qt::Alignment alignment = Qt::AlignCenter, bool bold = false)
{
    auto f = p.font(); f.setPixelSize(px); f.setBold(bold); p.setFont(f);
    p.setPen(color); p.drawText(r, alignment, s);
}

QString valueText(const std::array<float, 6>& v, int i)
{
    switch (i) {
    case 0: return QStringLiteral("%1 kHz").arg(v[0] / 1000.0F, 0, 'f', 1);
    case 1: return QStringLiteral("%1 kHz").arg(v[1] / 1000.0F, 0, 'f', 1);
    case 2: return QStringLiteral("%1 dB").arg(v[2], 0, 'f', 1);
    case 3: return QStringLiteral("%1 ms").arg(v[3], 0, 'f', 0);
    case 4: return QStringLiteral("%1 ms").arg(v[4], 0, 'f', 0);
    default: return QStringLiteral("%1 dB").arg(v[5], 0, 'f', 1);
    }
}

float normalized(const std::array<float, 6>& v, int i)
{
    const std::array<float, 6> low { 1000.0F, 250.0F, -60.0F, 0.0F, 5.0F, -24.0F };
    const std::array<float, 6> high { 12000.0F, 12000.0F, 0.0F, 100.0F, 1000.0F, 12.0F };
    if (i == 3 || i == 4) return std::clamp(std::log1p(v[i] - low[i]) / std::log1p(high[i] - low[i]), 0.0F, 1.0F);
    return std::clamp((v[i] - low[i]) / (high[i] - low[i]), 0.0F, 1.0F);
}

void drawKnob(QPainter& p, const QPointF& c, float n)
{
    QRectF arc(c.x() - 32, c.y() - 32, 64, 64);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(5, 15, 29), 7)); p.drawArc(arc, 225 * 16, -270 * 16);
    p.setPen(QPen(QColor(48, 34, 87), 5)); p.drawArc(arc.adjusted(2, 2, -2, -2), 225 * 16, -270 * 16);
    p.setPen(QPen(purple, 5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arc.adjusted(2, 2, -2, -2), 225 * 16, qRound(-270.0 * n * 16));
    QRadialGradient fill(c - QPointF(5, 7), 32);
    fill.setColorAt(0, QColor(36, 51, 75)); fill.setColorAt(1, QColor(11, 22, 37));
    p.setBrush(fill); p.setPen(QPen(QColor(41, 54, 75), 1)); p.drawEllipse(c, 26, 26);
    const auto a = (225.0 - 270.0 * n) * 3.14159265358979323846 / 180.0;
    p.setPen(QPen(QColor(207, 174, 255), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, c + QPointF(std::cos(a), -std::sin(a)) * 20.0);
}

void button(QPainter& p, QRectF r, const QString& label, bool selected, int size = 13)
{
    rounded(p, r, 5, selected ? QColor(0, 119, 191) : QColor(19, 40, 61),
            selected ? QColor(26, 204, 255) : QColor(54, 84, 111));
    if (selected) {
        p.setPen(QPen(QColor(29, 206, 255, 100), 3));
        p.setBrush(Qt::NoBrush); p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
    }
    textAt(p, r, label, size, selected ? QColor(218, 250, 255) : text, Qt::AlignCenter, selected);
}
}

DeEsserWidget::DeEsserWidget(QWidget* parent) : QWidget(parent, parent ? Qt::Widget : Qt::Window)
{
    if (!parent) setAttribute(Qt::WA_NativeWindow, true);
    setWindowTitle(QStringLiteral("De-Esser 去齿音"));
    setFixedSize(768, 650);
    setMouseTracking(true);
    for (std::size_t i = 0; i < spectrumFftSize_; ++i) {
        const auto phase = 2.0 * 3.14159265358979323846 * static_cast<double>(i)
            / static_cast<double>(spectrumFftSize_ - 1);
        window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
    }
    inputSpectrumDb_.fill(-120.0F);
    spectrumDb_.fill(-120.0F);
    connect(&spectrumTimer_, &QTimer::timeout, this, [this] { refreshSpectrum(); });
    spectrumTimer_.start(33);
}

void DeEsserWidget::setSpectrumSources(
    const std::array<WinJACKNexus::Common::AudioRingBuffer<
        WinJACKNexus::Common::DeEsserSpectrumSample>*, 8>& sources,
    const std::atomic<double>* sampleRate) noexcept
{
    spectrumSources_ = sources;
    spectrumSampleRate_ = sampleRate;
    // A closed effect window has no UI consumer, so its SPSC queues may contain
    // old audio (or be full). Start the analyzer at the live signal position.
    for (auto* source : spectrumSources_)
        if (source != nullptr) source->clear();
    historyWrite_.fill(0);
    historyCount_.fill(0);
    spectrumReady_ = false;
    inputSpectrumDb_.fill(-120.0F);
    spectrumDb_.fill(-120.0F);
}

void DeEsserWidget::addSpectrumSamples(
    std::size_t channel, const WinJACKNexus::Common::DeEsserSpectrumSample* samples,
    std::size_t count) noexcept
{
    auto& inputHistory = inputSpectrumHistory_[channel];
    auto& processedHistory = processedSpectrumHistory_[channel];
    auto& write = historyWrite_[channel];
    auto& available = historyCount_[channel];
    for (std::size_t i = 0; i < count; ++i) {
        inputHistory[write] = samples[i].input;
        processedHistory[write] = samples[i].processed;
        write = (write + 1) % spectrumFftSize_;
        available = std::min(available + 1, spectrumFftSize_);
    }
}

void DeEsserWidget::refreshSpectrum()
{
    const auto sampleRate = spectrumSampleRate_ == nullptr
        ? 48000.0 : std::max(8000.0, spectrumSampleRate_->load(std::memory_order_relaxed));
    if (sampleRate != lastSpectrumSampleRate_) {
        lastSpectrumSampleRate_ = sampleRate;
        for (auto* source : spectrumSources_)
            if (source != nullptr) source->clear();
        historyWrite_.fill(0);
        historyCount_.fill(0);
        spectrumReady_ = false;
        inputSpectrumDb_.fill(-120.0F);
        spectrumDb_.fill(-120.0F);
        update();
    }
    std::array<WinJACKNexus::Common::DeEsserSpectrumSample, 512> samples {};
    bool receivedSamples = false;
    for (std::size_t channel = 0; channel < spectrumSources_.size(); ++channel) {
        auto* source = spectrumSources_[channel];
        if (source == nullptr)
            continue;
        while (const auto count = source->read(samples.data(), samples.size())) {
            addSpectrumSamples(channel, samples.data(), count);
            receivedSamples = true;
        }
    }
    if (receivedSamples)
        calculateSpectrum();
}

void DeEsserWidget::transform(std::array<std::complex<float>, spectrumFftSize_>& values) noexcept
{
    constexpr std::size_t size = spectrumFftSize_;
    for (std::size_t i = 1, reversed = 0; i < size; ++i) {
        std::size_t bit = size >> 1;
        for (; reversed & bit; bit >>= 1)
            reversed ^= bit;
        reversed ^= bit;
        if (i < reversed)
            std::swap(values[i], values[reversed]);
    }

    constexpr float twoPi = 6.28318530717958647692F;
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto angle = -twoPi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t base = 0; base < size; base += length) {
            std::complex<float> phase(1.0F, 0.0F);
            const auto half = length >> 1;
            for (std::size_t offset = 0; offset < half; ++offset) {
                const auto even = values[base + offset];
                const auto odd = values[base + offset + half] * phase;
                values[base + offset] = even + odd;
                values[base + offset + half] = even - odd;
                phase *= step;
            }
        }
    }
}

void DeEsserWidget::calculateSpectrum()
{
    spectrumReady_ = false;
    inputSpectrumDb_.fill(-120.0F);
    spectrumDb_.fill(-120.0F);
    const auto sampleRate = spectrumSampleRate_ == nullptr
        ? 48000.0 : std::max(8000.0, spectrumSampleRate_->load(std::memory_order_relaxed));
    std::array<double, spectrumBinCount_> inputPower {};
    std::array<double, spectrumBinCount_> processedPower {};
    std::size_t channels = 0;
    double windowSum = 0.0;
    for (const auto coefficient : window_)
        windowSum += coefficient;
    const auto scale = 1.0 / windowSum;
    constexpr double minimumActiveInputRms = 1.0e-5; // -100 dBFS

    const auto accumulate = [this, scale](
        const std::array<float, spectrumFftSize_>& history, std::size_t channel,
        std::array<double, spectrumBinCount_>& power) {
        std::array<std::complex<float>, spectrumFftSize_> values {};
        const auto oldest = historyWrite_[channel];
        for (std::size_t i = 0; i < spectrumFftSize_; ++i) {
            const auto sample = history[(oldest + i) % spectrumFftSize_];
            values[i] = std::complex<float>(sample * window_[i], 0.0F);
        }
        transform(values);
        for (std::size_t bin = 0; bin < spectrumBinCount_; ++bin) {
            const auto oneSidedScale = (bin == 0 || bin == spectrumFftSize_ / 2)
                ? scale : scale * 2.0;
            const auto magnitude = static_cast<double>(std::abs(values[bin])) * oneSidedScale;
            power[bin] += magnitude * magnitude;
        }
    };

    const auto first = selectedChannel_ < 0 ? 0 : selectedChannel_;
    const auto last = selectedChannel_ < 0 ? 8 : selectedChannel_ + 1;
    for (int channel = first; channel < last; ++channel) {
        const auto index = static_cast<std::size_t>(channel);
        if (historyCount_[index] < spectrumFftSize_)
            continue;
        if (selectedChannel_ < 0) {
            double inputSquareSum = 0.0;
            const auto oldest = historyWrite_[index];
            for (std::size_t i = 0; i < spectrumFftSize_; ++i) {
                const auto sample = inputSpectrumHistory_[index][(oldest + i) % spectrumFftSize_];
                inputSquareSum += static_cast<double>(sample) * sample;
            }
            const auto inputRms = std::sqrt(inputSquareSum / static_cast<double>(spectrumFftSize_));
            if (inputRms < minimumActiveInputRms)
                continue;
        }
        accumulate(inputSpectrumHistory_[index], index, inputPower);
        accumulate(processedSpectrumHistory_[index], index, processedPower);
        ++channels;
    }
    if (channels == 0) {
        update();
        return;
    }

    spectrumReady_ = true;
    for (std::size_t bin = 0; bin < spectrumBinCount_; ++bin) {
        const auto inputMagnitude = std::sqrt(inputPower[bin] / static_cast<double>(channels));
        const auto processedMagnitude = std::sqrt(processedPower[bin] / static_cast<double>(channels));
        inputSpectrumDb_[bin] = static_cast<float>(20.0 * std::log10(std::max(inputMagnitude, 1.0e-6)));
        spectrumDb_[bin] = static_cast<float>(20.0 * std::log10(std::max(processedMagnitude, 1.0e-6)));
    }
    update();
}

void DeEsserWidget::setParameters(const std::array<Parameters, 8>& parameters)
{
    parameters_ = parameters;
    const auto isDefault = [](const Parameters& p) {
        const Parameters defaults;
        return p.centerFrequencyHz == defaults.centerFrequencyHz
            && p.bandwidthHz == defaults.bandwidthHz && p.thresholdDb == defaults.thresholdDb
            && p.attackMs == defaults.attackMs && p.releaseMs == defaults.releaseMs
            && p.outputGainDb == defaults.outputGainDb && p.mode == defaults.mode
            && p.detector == defaults.detector && p.listen == defaults.listen
            && p.enabled == defaults.enabled;
    };
    selectedPreset_ = std::all_of(parameters_.begin(), parameters_.end(), isDefault) ? 0 : 3;
    update();
}

void DeEsserWidget::setChangeHandler(ChangeHandler handler)
{
    changeHandler_ = std::move(handler);
}

DeEsserWidget::Parameters DeEsserWidget::displayed() const noexcept
{
    if (selectedChannel_ >= 0)
        return parameters_[static_cast<std::size_t>(selectedChannel_)];
    Parameters result = parameters_[0];
    result.centerFrequencyHz = result.bandwidthHz = result.thresholdDb = 0.0F;
    result.attackMs = result.releaseMs = result.outputGainDb = 0.0F;
    result.enabled = true;
    for (const auto& item : parameters_) {
        result.centerFrequencyHz += item.centerFrequencyHz / 8.0F;
        result.bandwidthHz += item.bandwidthHz / 8.0F;
        result.thresholdDb += item.thresholdDb / 8.0F;
        result.attackMs += item.attackMs / 8.0F;
        result.releaseMs += item.releaseMs / 8.0F;
        result.outputGainDb += item.outputGainDb / 8.0F;
        result.enabled = result.enabled && item.enabled;
    }
    return result;
}

void DeEsserWidget::apply(const std::function<void(Parameters&)>& edit)
{
    const int first = selectedChannel_ < 0 ? 0 : selectedChannel_;
    const int last = selectedChannel_ < 0 ? 8 : selectedChannel_ + 1;
    for (int i = first; i < last; ++i) {
        auto& parameters = parameters_[static_cast<std::size_t>(i)];
        edit(parameters);
        if (changeHandler_) changeHandler_(static_cast<std::size_t>(i), parameters);
    }
    selectedPreset_ = 3;
    update();
}

void DeEsserWidget::selectPreset(int index)
{
    const int preset = std::clamp(index, 0, 2);
    apply([index](Parameters& p) {
        p = Parameters {};
        if (index == 1) { p.centerFrequencyHz = 7200.0F; p.bandwidthHz = 3000.0F; p.thresholdDb = -16.0F; }
        if (index == 2) { p.centerFrequencyHz = 5000.0F; p.bandwidthHz = 4500.0F; p.thresholdDb = -14.0F; }
    });
    selectedPreset_ = preset;
    update();
}

void DeEsserWidget::paintEvent(QPaintEvent*)
{
    const auto shown = displayed();
    const std::array<float, 6> values {shown.centerFrequencyHz, shown.bandwidthHz,
        shown.thresholdDb, shown.attackMs, shown.releaseMs, shown.outputGainDb};
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Controls::themeColor(*this, "fxBackground", background));
    p.save(); p.scale(width() / 768.0, height() / 650.0);

    // Nine channel buttons keep the reference's dark, inset selector style while
    // exposing All plus all eight native-effect channels.
    rounded(p, {7, 8, 754, 128}, 11, QColor(21, 39, 59), QColor(47, 75, 104));
    textAt(p, {22, 18, 150, 34}, QStringLiteral("作用声道"), 20, text, Qt::AlignLeft, true);
    for (int i = 0; i < 9; ++i) {
        const QRectF r(22 + i * 80, 68, 72, 40);
        const bool selected = i == (selectedChannel_ < 0 ? 0 : selectedChannel_ + 1);
        rounded(p, r, 6, selected ? QColor(17, 58, 86) : QColor(26, 44, 63),
                selected ? QColor(18, 192, 255) : QColor(65, 91, 117));
        if (selected) {
            p.setPen(QPen(QColor(0, 162, 255, 35), 5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5);
        }
        textAt(p, r, channels[static_cast<std::size_t>(i)], 16,
               selected ? QColor(91, 222, 255) : QColor(168, 194, 229));
    }

    p.translate(0, 145);
    rounded(p, {10, 9, 747, 481}, 10, QColor(11, 26, 43), QColor(92, 65, 135));
    rounded(p, {9, 9, 750, 72}, 10, QColor(20, 38, 58));
    rounded(p, {21, 15, 56, 57}, 7, QColor(65, 34, 114));
    p.setPen(QPen(QColor(218, 182, 255), 2.4));
    QPainterPath logo; logo.moveTo(63, 33); logo.lineTo(57, 33); logo.cubicTo(53, 24, 42, 24, 40, 32);
    logo.cubicTo(38, 41, 54, 43, 54, 50); logo.cubicTo(54, 57, 44, 59, 39, 52); logo.lineTo(34, 52);
    p.drawPath(logo); p.drawLine(QPointF(28, 42), QPointF(34, 42)); p.drawLine(QPointF(62, 42), QPointF(68, 42));
    textAt(p, {95, 13, 240, 37}, QStringLiteral("De-Esser 去齿音"), 26, text, Qt::AlignLeft, true);
    textAt(p, {95, 48, 265, 22}, QStringLiteral("检测并抑制齿音频段（S、SH 等）"), 14, muted, Qt::AlignLeft);
    rounded(p, {326, 15, 58, 58}, 10, QColor(25, 42, 62));
    rounded(p, {337, 32, 37, 20}, 10, shown.enabled ? QColor(0, 137, 220) : QColor(57, 71, 89));
    p.setPen(Qt::NoPen); p.setBrush(Qt::white); p.drawEllipse(QPointF(shown.enabled ? 363 : 347, 42), 7.5, 7.5);
    textAt(p, {400, 26, 43, 34}, QStringLiteral("预设:"), 13, muted,
           Qt::AlignLeft | Qt::AlignVCenter);
    rounded(p, {453, 26, 166, 34}, 5, QColor(13, 28, 46), QColor(52, 82, 112));
    const std::array<QString, 4> presets { QStringLiteral("Vocal De-Esser"),
        QStringLiteral("Bright Vocal"), QStringLiteral("Soft Voice"), QStringLiteral("Custom") };
    textAt(p, {463, 26, 135, 34}, presets[static_cast<std::size_t>(selectedPreset_)], 14, text);
    textAt(p, {597, 26, 18, 34}, QStringLiteral("⌄"), 17, muted);
    for (int i = 0; i < 2; ++i) {
        const QRectF r(631 + i * 38, 27, 30, 32); rounded(p, r, 5, QColor(18, 40, 60), QColor(53, 83, 111));
        textAt(p, r, i == 0 ? QStringLiteral("‹") : QStringLiteral("›"), 23, muted);
    }
    const QRectF more(709, 27, 31, 32); rounded(p, more, 5, QColor(18, 40, 60), QColor(53, 83, 111));
    textAt(p, more, QStringLiteral("•••"), 11, muted);

    // Analyzer: frequency/dB grid, the processed-output spectrum, and the purple
    // de-essing bell centered at the selected sibilance band.
    const QRectF graph(70, 116, 339, 216);
    constexpr qreal pixelsPerOctave = 79.0;
    const auto frequencyToX = [&graph, pixelsPerOctave](double frequency) {
        return graph.left() + std::log2(std::max(frequency, 1000.0) / 1000.0)
            * pixelsPerOctave;
    };
    const auto xToFrequency = [&graph, pixelsPerOctave](qreal x) {
        return 1000.0 * std::exp2((x - graph.left()) / pixelsPerOctave);
    };
    rounded(p, graph, 2, QColor(8, 22, 38), QColor(47, 77, 104));
    for (int halfOctave = 0; halfOctave <= 8; ++halfOctave) {
        const auto x = frequencyToX(1000.0 * std::exp2(halfOctave * 0.5));
        const auto color = halfOctave % 2 == 0 ? QColor(37, 60, 83) : QColor(25, 46, 67);
        p.setPen(QPen(color, 1)); p.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
    }
    for (int i = 0; i <= 5; ++i) {
        const auto y = graph.top() + graph.height() * i / 5.0;
        p.setPen(QPen(QColor(30, 51, 72), 1)); p.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
        textAt(p, {21, y - 10, 39, 20}, i == 0 ? QStringLiteral("0") : QStringLiteral("-%1").arg(i * 12),
               14, muted, Qt::AlignRight | Qt::AlignVCenter);
    }
    const std::array<QString, 5> hz { QStringLiteral("1k"), QStringLiteral("2k"), QStringLiteral("4k"), QStringLiteral("8k"), QStringLiteral("16k") };
    for (int i = 0; i < 5; ++i) {
        const auto x = frequencyToX(1000.0 * std::exp2(i));
        textAt(p, {x - 21, 336, 42, 19}, hz[i], 14, muted);
    }
    p.save(); p.setClipRect(graph.adjusted(1, 1, -1, -1));
    p.setBrush(Qt::NoBrush);
    const qreal centerX = frequencyToX(shown.centerFrequencyHz);
    const qreal widthScale = std::clamp(shown.bandwidthHz / 4000.0F, 0.5F, 2.0F);
    constexpr qreal bellBaselineY = 247.0;
    constexpr qreal bellPeakY = 181.0;
    QPainterPath bell;
    bell.moveTo(centerX - 120.0 * widthScale, bellBaselineY);
    bell.cubicTo(centerX - 75.0 * widthScale, bellBaselineY,
                 centerX - 48.0 * widthScale, bellPeakY, centerX, bellPeakY);
    bell.cubicTo(centerX + 48.0 * widthScale, bellPeakY,
                 centerX + 75.0 * widthScale, bellBaselineY,
                 centerX + 120.0 * widthScale, bellBaselineY);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(purple, 2)); p.drawPath(bell);
    if (spectrumReady_) {
        const auto sampleRate = spectrumSampleRate_ == nullptr
            ? 48000.0 : std::max(8000.0, spectrumSampleRate_->load(std::memory_order_relaxed));
        const auto drawSpectrum = [&](const std::array<float, spectrumBinCount_>& db,
                                      const QColor& color) {
            QPainterPath spectrum;
            bool segmentActive = false;
            for (int x = qRound(graph.left()); x <= qRound(graph.right()); ++x) {
                const auto frequency = xToFrequency(x);
                if (frequency > sampleRate * 0.5) {
                    segmentActive = false;
                    continue;
                }
                const auto fractionalBin = frequency * spectrumFftSize_ / sampleRate;
                const auto lowerBin = static_cast<std::size_t>(fractionalBin);
                const auto upperBin = std::min(lowerBin + 1, spectrumBinCount_ - 1);
                const auto fraction = fractionalBin - static_cast<double>(lowerBin);
                const auto levelDb = db[lowerBin] * (1.0 - fraction) + db[upperBin] * fraction;
                const auto level = std::clamp((levelDb + 60.0) / 60.0, 0.0, 1.0);
                const auto y = graph.bottom() - level * graph.height();
                if (segmentActive) spectrum.lineTo(x, y); else spectrum.moveTo(x, y);
                segmentActive = true;
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(color, 1.0));
            p.drawPath(spectrum);
        };
        drawSpectrum(inputSpectrumDb_, QColor(67, 207, 224, 205));
        drawSpectrum(spectrumDb_, QColor(127, 83, 184, 205));
        rounded(p, {76, 119, 127, 19}, 4, QColor(8, 22, 38, 225));
        p.setPen(QPen(QColor(67, 207, 224, 230), 1.4)); p.drawLine(QPointF(82, 128), QPointF(94, 128));
        textAt(p, {97, 119, 30, 18}, QStringLiteral("输入"), 10, text);
        p.setPen(QPen(QColor(127, 83, 184, 230), 1.4)); p.drawLine(QPointF(131, 128), QPointF(143, 128));
        textAt(p, {146, 119, 54, 18}, QStringLiteral("处理后"), 10, text);
    }
    p.setPen(QPen(QColor(174, 100, 255), 1.4, Qt::DashLine)); p.drawLine(QPointF(centerX, 157), QPointF(centerX, 332));
    p.setBrush(QColor(228, 207, 255)); p.setPen(QPen(purple, 2)); p.drawEllipse(QPointF(centerX, 181), 5.5, 5.5);
    p.restore();
    const qreal tooltipX = std::clamp(centerX - 36, graph.left(), graph.right() - 73);
    rounded(p, {tooltipX, 111, 73, 47}, 6, QColor(32, 34, 63), QColor(88, 64, 133));
    textAt(p, {tooltipX + 4, 113, 65, 20}, QStringLiteral("中心频率"), 12, text);
    textAt(p, {tooltipX + 4, 132, 65, 20}, valueText(values, 0), 13, text);

    rounded(p, {429, 94, 314, 274}, 11, QColor(19, 36, 56), QColor(39, 66, 94));
    for (int i = 0; i < 6; ++i) {
        drawKnob(p, knobs[static_cast<std::size_t>(i)], normalized(values, i));
        textAt(p, {knobs[i].x() - 48, knobs[i].y() + 34, 96, 23}, valueText(values, i), 16, text);
        textAt(p, {knobs[i].x() - 49, knobs[i].y() + 56, 98, 22}, captions[i], 15, muted);
    }

    rounded(p, {20, 376, 389, 99}, 8, QColor(13, 31, 50), QColor(40, 75, 105));
    textAt(p, {30, 409, 52, 34}, QStringLiteral("模式:"), 14, muted,
           Qt::AlignLeft | Qt::AlignVCenter);
    const std::array<QString, 3> modes { QStringLiteral("宽带"), QStringLiteral("分频"), QStringLiteral("动态 EQ") };
    for (int i = 0; i < 3; ++i) button(p, {85.0 + i * 87.0, 409, 84, 34}, modes[i], static_cast<int>(shown.mode) == i, 14);
    rounded(p, {429, 376, 312, 99}, 8, QColor(13, 31, 50), QColor(40, 75, 105));
    textAt(p, {443, 382, 70, 34}, QStringLiteral("检测模式:"), 14, muted,
           Qt::AlignLeft | Qt::AlignVCenter);
    button(p, {521, 382, 84, 34}, QStringLiteral("RMS"), shown.detector == Common::DeEsserDetector::Rms, 14);
    button(p, {611, 382, 86, 34}, QStringLiteral("峰值"), shown.detector == Common::DeEsserDetector::Peak, 14);
    button(p, {702, 382, 29, 34}, QStringLiteral("i"), false, 13);
    textAt(p, {443, 423, 67, 34}, QStringLiteral("监听:"), 14, muted,
           Qt::AlignLeft | Qt::AlignVCenter);
    button(p, {521, 423, 65, 34}, QStringLiteral("关"), shown.listen == Common::DeEsserListen::Off, 14);
    button(p, {592, 423, 65, 34}, QStringLiteral("齿音"), shown.listen == Common::DeEsserListen::Sibilance, 13);
    button(p, {663, 423, 70, 34}, QStringLiteral("原始"), shown.listen == Common::DeEsserListen::Original, 13);
    p.restore();
}

void DeEsserWidget::setKnobFromY(int index, qreal y)
{
    const float n = static_cast<float>(std::clamp(dragStartNormalized_ + (dragStartY_ - y) / 120.0, 0.0, 1.0));
    const std::array<float, 6> low { 1000, 250, -60, 0, 5, -24 }, high { 12000, 12000, 0, 100, 1000, 12 };
    const float value = (index == 3 || index == 4)
        ? low[index] + std::expm1(n * std::log1p(high[index] - low[index]))
        : low[index] + n * (high[index] - low[index]);
    apply([index, value](Parameters& p) {
        switch (index) {
        case 0: p.centerFrequencyHz = value; break;
        case 1: p.bandwidthHz = value; break;
        case 2: p.thresholdDb = value; break;
        case 3: p.attackMs = value; break;
        case 4: p.releaseMs = value; break;
        default: p.outputGainDb = value; break;
        }
    });
}

void DeEsserWidget::mousePressEvent(QMouseEvent* event)
{
    const QPointF pt = event->position();
    const QPointF local(pt.x() * 768.0 / width(), pt.y() * 650.0 / height());
    if (local.y() < 136) {
        const int channel = qRound((local.x() - 58) / 80.0);
        if (channel >= 0 && channel < 9 && QRectF(22 + channel * 80, 68, 72, 40).contains(local)) {
            selectedChannel_ = channel == 0 ? -1 : channel - 1;
            calculateSpectrum();
            update();
            return;
        }
    }
    const QPointF main(local.x(), local.y() - 145);
    for (int i = 0; i < 6; ++i)
        if (QLineF(main, knobs[static_cast<std::size_t>(i)]).length() <= 35) {
            const auto shown = displayed();
            const std::array<float, 6> values {shown.centerFrequencyHz, shown.bandwidthHz,
                shown.thresholdDb, shown.attackMs, shown.releaseMs, shown.outputGainDb};
            draggingKnob_ = i;
            dragStartY_ = main.y();
            dragStartNormalized_ = normalized(values, i);
            return;
        }
    if (QRectF(326, 15, 58, 58).contains(main)) { const bool enabled = !displayed().enabled; apply([enabled](Parameters& p) { p.enabled = enabled; }); return; }
    if (QRectF(453, 26, 166, 34).contains(main)) {
        QMenu menu(this);
        const std::array<QString, 3> presets {QStringLiteral("Vocal De-Esser"), QStringLiteral("Bright Vocal"), QStringLiteral("Soft Voice")};
        for (int i = 0; i < 3; ++i) {
            auto* action = menu.addAction(presets[static_cast<std::size_t>(i)]);
            connect(action, &QAction::triggered, this, [this, i] { selectPreset(i); });
        }
        menu.exec(mapToGlobal(QPoint(453, 205)));
        return;
    }
    if (QRectF(631, 27, 30, 32).contains(main)) { selectPreset((selectedPreset_ + 2) % 3); return; }
    if (QRectF(669, 27, 30, 32).contains(main)) { selectPreset((selectedPreset_ + 1) % 3); return; }
    if (QRectF(709, 27, 31, 32).contains(main)) {
        QMenu menu(this);
        auto* reset = menu.addAction(QStringLiteral("恢复 Vocal De-Esser 默认值"));
        connect(reset, &QAction::triggered, this, [this] { selectPreset(0); });
        menu.exec(mapToGlobal(QPoint(709, 205)));
        return;
    }
    for (int i = 0; i < 3; ++i) if (QRectF(85 + i * 87, 409, 84, 34).contains(main)) { apply([i](Parameters& p) { p.mode = static_cast<Common::DeEsserMode>(i); }); return; }
    if (QRectF(521, 382, 84, 34).contains(main)) { apply([](Parameters& p) { p.detector = Common::DeEsserDetector::Rms; }); return; }
    if (QRectF(611, 382, 86, 34).contains(main)) { apply([](Parameters& p) { p.detector = Common::DeEsserDetector::Peak; }); return; }
    if (QRectF(702, 382, 29, 34).contains(main)) {
        QToolTip::showText(mapToGlobal(pt.toPoint()),
                           QStringLiteral("RMS 使用平均能量检测；峰值响应瞬时齿音。"), this);
        return;
    }
    if (QRectF(521, 423, 65, 34).contains(main)) { apply([](Parameters& p) { p.listen = Common::DeEsserListen::Off; }); return; }
    if (QRectF(592, 423, 65, 34).contains(main)) { apply([](Parameters& p) { p.listen = Common::DeEsserListen::Sibilance; }); return; }
    if (QRectF(663, 423, 70, 34).contains(main)) { apply([](Parameters& p) { p.listen = Common::DeEsserListen::Original; }); }
}

void DeEsserWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingKnob_ < 0 || !(event->buttons() & Qt::LeftButton)) return;
    const qreal y = event->position().y() * 650.0 / height() - 145;
    setKnobFromY(draggingKnob_, y);
}

void DeEsserWidget::mouseReleaseEvent(QMouseEvent*) { draggingKnob_ = -1; }

}
