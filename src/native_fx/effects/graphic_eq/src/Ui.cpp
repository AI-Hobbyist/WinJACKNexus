/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/graphic_eq/Ui.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <limits>

namespace WinJACKNexus::UI {
namespace {
using WinJACKNexus::Common::graphicEqBandCount;
using WinJACKNexus::Common::graphicEqFrequencies;
constexpr qreal selectorHeight = 146.0;
constexpr QRectF graphBounds { 50.0, 100.0, 578.0, 248.0 };
const QColor background(9, 23, 39), panel(15, 34, 52), border(38, 69, 96);
const QColor text(225, 237, 248), muted(139, 164, 185), cyan(43, 205, 239);

void rounded(QPainter& painter, const QRectF& bounds, const QColor& fill,
             const QColor& outline = Qt::transparent, qreal radius = 5.0)
{
    Controls::drawSurface(painter,bounds,fill,outline,radius);
}

void drawText(QPainter& painter, const QRectF& bounds, const QString& value, int size = 10,
              const QColor& color = text, Qt::Alignment align = Qt::AlignCenter, bool bold = false)
{
    auto font = painter.font();
    font.setPixelSize(size <= 13 ? std::max(size + 2, qRound(size * 1.18)) : size);
    font.setBold(bold);
    Controls::drawText(painter,bounds,value,font,color,align);
}

QString freqLabel(float frequency)
{
    if (frequency >= 1000.0F)
        return QStringLiteral("%1k").arg(frequency / 1000.0F, 0, 'g', 3);
    return QString::number(frequency, 'g', 3);
}

QRectF channelButton(int index)
{
    return { 22.0 + index * 80.0, 68.0, 72.0, 40.0 };
}

qreal frequencyX(qreal frequency)
{
    constexpr qreal minFrequency = 20.0;
    constexpr qreal maxFrequency = 20000.0;
    const auto normalized = std::log(frequency / minFrequency)
        / std::log(maxFrequency / minFrequency);
    return graphBounds.left() + normalized * graphBounds.width();
}

qreal gainY(qreal gainDb, qreal graphTop, qreal graphBottom)
{
    return graphTop + (12.0 - gainDb) / 24.0 * (graphBottom - graphTop);
}

QPainterPath smoothCurve(const std::array<QPointF, graphicEqBandCount>& points)
{
    QPainterPath path;
    path.moveTo(points.front());
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& p0 = points[i == 0 ? i : i - 1];
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];
        const auto& p3 = points[std::min(i + 2, points.size() - 1)];
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
    return path;
}

}

GraphicEqWidget::GraphicEqWidget(QWidget* parent) : QWidget(parent, parent ? Qt::Widget : Qt::Window), spectrumTimer_(this)
{
    if (!parent) setAttribute(Qt::WA_NativeWindow, true);
    setWindowTitle(QStringLiteral("Graphic EQ 图示均衡器"));
    setFixedSize(760, 658);
    setMouseTracking(true);
    spectrum_.inputDb.fill(-96.0F);
    spectrum_.outputDb.fill(-96.0F);
    spectrumTimer_.setObjectName(QStringLiteral("nativeFxSpectrumRefreshTimer"));
    bindRefreshTimer(spectrumTimer_, this);
    connect(&spectrumTimer_, &QTimer::timeout, this, &GraphicEqWidget::pollSpectrumFrame);
}

void GraphicEqWidget::setParameters(const std::array<Parameters, 8>& parameters)
{
    parameters_ = parameters;
    const auto matchesPreset = [this](const std::array<float, graphicEqBandCount>& gains) {
        return std::all_of(parameters_.cbegin(), parameters_.cend(), [&gains](const Parameters& item) {
            if (std::abs(item.outputGainDb) > 0.001F)
                return false;
            for (std::size_t i = 0; i < graphicEqBandCount; ++i)
                if (std::abs(item.bandGainsDb[i] - gains[i]) > 0.001F)
                    return false;
            return true;
        });
    };
    if (matchesPreset(WinJACKNexus::Common::graphicEqFlatGains)) selectedPreset_ = 0;
    else if (matchesPreset(WinJACKNexus::Common::graphicEqVocalBrightGains)) selectedPreset_ = 1;
    else if (matchesPreset(WinJACKNexus::Common::graphicEqBassBoostGains)) selectedPreset_ = 2;
    else if (matchesPreset(WinJACKNexus::Common::graphicEqClassicalGains)) selectedPreset_ = 3;
    else selectedPreset_ = 4;
    if (draggingBand_ < 0) update();
}

void GraphicEqWidget::setChangeHandler(ChangeHandler handler) { changeHandler_ = std::move(handler); }

void GraphicEqWidget::setSpectrumFrameReader(SpectrumFrameReader reader)
{
    spectrumFrameReader_ = std::move(reader);
    if (spectrumFrameReader_)
        spectrumTimer_.start();
    else if (draggingBand_ < 0)
        spectrumTimer_.stop();
}

void GraphicEqWidget::pollSpectrumFrame()
{
    if (!spectrumFrameReader_ || !spectrumFrameReader_(spectrumFrame_)) {
        if (draggingBand_ >= 0) update();
        return;
    }

    hasSpectrumFrame_ = true;
    ++spectrumFrameCount_;
    refreshSpectrumForSelection(true);
}

void GraphicEqWidget::refreshSpectrumForSelection(bool smooth)
{
    const auto elapsedMilliseconds = smooth && spectrumClock_.isValid()
        ? spectrumClock_.restart() : -1;
    if (smooth && elapsedMilliseconds < 0) spectrumClock_.start();
    if (!smooth) spectrumClock_.invalidate();
    const auto elapsedSeconds = elapsedMilliseconds > 0
        ? elapsedMilliseconds / 1000.0
        : 1.0 / ApplicationSettings::instance().frameRate();
    const auto referenceFrames = elapsedSeconds * 60.0;
    WinJACKNexus::Common::GraphicEqSpectrumPair nextSpectrum {};
    const auto channel = selectedChannel_ < 0
        ? WinJACKNexus::Common::graphicEqSpectrumAllChannels
        : static_cast<std::size_t>(selectedChannel_);
    WinJACKNexus::Common::calculateGraphicEqSpectrum(spectrumFrame_, channel, nextSpectrum);
    for (std::size_t i = 0; i < spectrum_.inputDb.size(); ++i) {
        if (!smooth) {
            spectrum_.inputDb[i] = nextSpectrum.inputDb[i];
            spectrum_.outputDb[i] = nextSpectrum.outputDb[i];
            continue;
        }
        const auto inputBaseline = nextSpectrum.inputDb[i] > spectrum_.inputDb[i] ? 0.68F : 0.24F;
        const auto outputBaseline = nextSpectrum.outputDb[i] > spectrum_.outputDb[i] ? 0.68F : 0.24F;
        const auto inputCoefficient = 1.0 - std::pow(1.0 - inputBaseline, referenceFrames);
        const auto outputCoefficient = 1.0 - std::pow(1.0 - outputBaseline, referenceFrames);
        spectrum_.inputDb[i] += (nextSpectrum.inputDb[i] - spectrum_.inputDb[i]) * inputCoefficient;
        spectrum_.outputDb[i] += (nextSpectrum.outputDb[i] - spectrum_.outputDb[i]) * outputCoefficient;
    }
    spectrum_.activeChannelCount = nextSpectrum.activeChannelCount;
    update();
}

GraphicEqWidget::Parameters GraphicEqWidget::displayed() const noexcept
{
    if (selectedChannel_ >= 0)
        return parameters_[static_cast<std::size_t>(selectedChannel_)];
    Parameters result = parameters_[0];
    result.bandGainsDb.fill(0.0F);
    result.outputGainDb = 0.0F;
    result.enabled = true;
    for (const auto& item : parameters_) {
        for (std::size_t i = 0; i < graphicEqBandCount; ++i)
            result.bandGainsDb[i] += item.bandGainsDb[i] / 8.0F;
        result.outputGainDb += item.outputGainDb / 8.0F;
        result.enabled = result.enabled && item.enabled;
    }
    return result;
}

void GraphicEqWidget::apply(const std::function<void(Parameters&)>& edit)
{
    const int first = selectedChannel_ < 0 ? 0 : selectedChannel_;
    const int last = selectedChannel_ < 0 ? 8 : selectedChannel_ + 1;
    for (int i = first; i < last; ++i) {
        edit(parameters_[static_cast<std::size_t>(i)]);
        if (changeHandler_)
            changeHandler_(static_cast<std::size_t>(i), parameters_[static_cast<std::size_t>(i)]);
    }
    selectedPreset_ = 4;
    if (draggingBand_ < 0) update();
}

void GraphicEqWidget::selectPreset(int index)
{
    const auto preset = std::clamp(index, 0, 4);
    if (preset == 4) {
        selectedPreset_ = preset;
        update();
        return;
    }
    apply([preset](Parameters& parameters) {
        parameters.outputGainDb = 0.0F;
        parameters.bandGainsDb = preset == 0 ? WinJACKNexus::Common::graphicEqFlatGains
            : preset == 1 ? WinJACKNexus::Common::graphicEqVocalBrightGains
            : preset == 2 ? WinJACKNexus::Common::graphicEqBassBoostGains
                          : WinJACKNexus::Common::graphicEqClassicalGains;
    });
    selectedPreset_ = preset;
    update();
}

void GraphicEqWidget::paintEvent(QPaintEvent*)
{
    const auto themed = [](const char* role, const QColor& fallback) {
        return Controls::themeTokenColor(QStringLiteral("native-fx-graphic-eq"),
                                         QString::fromLatin1(role), fallback);
    };
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Controls::themeColor(*this, "fxBackground",
                                           themed("surface-window", background)));
    p.save();
    p.scale(width() / 760.0, height() / 658.0);
    Controls::Palette controlDefaults;
    controlDefaults.background = themed("surface-window", background);
    controlDefaults.panel = themed("surface-panel", panel);
    controlDefaults.text = themed("text-primary", text);
    controlDefaults.accent = themed("control-button-border-selected", cyan);
    controlDefaults.border = themed("control-button-border-normal", border);
    const auto controls = Controls::paletteFor(*this, controlDefaults);
    auto sliderDefaults = controlDefaults;
    sliderDefaults.panel = themed("slider-effect-band-track", panel);
    sliderDefaults.border = themed("slider-effect-band-border", border);
    sliderDefaults.accent = themed("slider-effect-band-fill", cyan);
    sliderDefaults.text = themed("slider-effect-band-thumb", text);
    const auto sliderColors = Controls::paletteFor(*this, sliderDefaults);
    auto knobDefaults = controlDefaults;
    knobDefaults.background = themed("control-knob-background-base", background);
    knobDefaults.border = themed("control-knob-track", border);
    knobDefaults.accent = themed("control-knob-indicator-normal", cyan);
    const auto knobColors = Controls::paletteFor(*this, knobDefaults);

    // Channel strip follows the supplied reference while retaining all eight bed channels.
    rounded(p, { 7, 8, 740, 128 }, QColor(21, 39, 59), QColor(47, 75, 104), 11);
    drawText(p, { 22, 23, 110, 31 }, QStringLiteral("作用声道"), 18, text, Qt::AlignLeft, true);
    const std::array<QString, 9> channels { QStringLiteral("All"), QStringLiteral("L"), QStringLiteral("R"),
        QStringLiteral("C"), QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs"),
        QStringLiteral("Lb"), QStringLiteral("Rb") };
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        const auto button = channelButton(i);
        const bool selected = i == (selectedChannel_ < 0 ? 0 : selectedChannel_ + 1);
        Controls::drawButton(p, button, channels[static_cast<std::size_t>(i)], selected, controls, 14);
    }

    p.translate(0, selectorHeight);
    // Main control surface is drawn on the same 760x512 grid as the reference image.
    rounded(p, { 7, 7, 746, 497 }, QColor(9, 24, 41), QColor(34, 109, 166), 9);
    rounded(p, { 8, 8, 744, 73 }, QColor(23, 44, 66), Qt::transparent, 9);

    // The reference EQ mark is a compact four-bar icon.
    rounded(p, { 20, 15, 56, 56 }, QColor(8, 69, 111), Qt::transparent, 7);
    p.setPen(Qt::NoPen);
    p.setBrush(cyan);
    p.drawRoundedRect(QRectF(29, 43, 7, 17), 1, 1);
    p.drawRoundedRect(QRectF(40, 31, 7, 29), 1, 1);
    p.drawRoundedRect(QRectF(51, 20, 7, 40), 1, 1);
    p.drawRoundedRect(QRectF(62, 39, 7, 21), 1, 1);
    drawText(p, { 87, 15, 263, 33 }, QStringLiteral("Graphic EQ 图示均衡器"), 23, text, Qt::AlignLeft, true);
    drawText(p, { 88, 48, 238, 20 }, QStringLiteral("31 段图示均衡 · 精确调整频响响应"), 12, muted, Qt::AlignLeft);

    const auto params = displayed();
    // Compact switch matches the pill switch shown in the reference.
    rounded(p, { 348, 15, 58, 58 }, QColor(23, 44, 66), Qt::transparent, 10);
    Controls::drawToggle(p, { 356, 29, 38, 22 }, params.enabled,
        themed("control-toggle-indicator-on", cyan),
        themed("control-toggle-indicator-off", QColor(49, 68, 84)));
    drawText(p, { 410, 27, 35, 33 }, QStringLiteral("预设:"), 12, muted,
             Qt::AlignLeft | Qt::AlignVCenter);
    const std::array<QString, 5> presets { QStringLiteral("Default"), QStringLiteral("Vocal Bright"),
        QStringLiteral("Bass Boost"), QStringLiteral("Classical"), QStringLiteral("Custom") };
    Controls::drawDropdown(p, { 446, 27, 174, 33 },
        presets[static_cast<std::size_t>(std::clamp(selectedPreset_, 0, 4))], controls, 14);
    for (int i = 0; i < 2; ++i) {
        const QRectF button(628 + i * 38, 27, 30, 32);
        rounded(p, button, QColor(17, 39, 59), QColor(47, 81, 109), 5);
        drawText(p, button, i == 0 ? QStringLiteral("‹") : QStringLiteral("›"), 17, muted);
    }
    const QRectF moreButton(708, 28, 31, 31);
    rounded(p, moreButton, QColor(17, 39, 59), QColor(47, 81, 109), 5);
    drawText(p, moreButton, QStringLiteral("•••"), 11, muted);

    const auto graphTop = graphBounds.top() + 24.0;
    const auto graphBottom = graphBounds.bottom();
    const auto graphHeight = graphBottom - graphTop;
    Controls::drawChart(p, graphBounds, 0, 0, controls);
    const QRectF gridBounds(graphBounds.left(),graphTop,graphBounds.width(),graphHeight);
    Controls::ChartGridStyle gainGrid;
    gainGrid.grid=themed("chart-grid-major", QColor(37,59,79));
    gainGrid.penStyle=Qt::DashLine;
    for (int db = 12; db >= -12; db -= 6) {
        const auto y = gainY(db, graphTop, graphBottom);
        gainGrid.horizontalFractions.push_back((y-graphTop)/graphHeight);
        const auto label = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
        drawText(p, { 17, y - 9, 29, 18 }, label, 12, muted, Qt::AlignRight | Qt::AlignVCenter);
    }
    Controls::drawChartGrid(p,gridBounds,gainGrid);

    const auto paramsForCurve = params;
    std::array<QPointF, graphicEqBandCount> points {};
    for (std::size_t i = 0; i < graphicEqBandCount; ++i) {
        const auto x = std::clamp(frequencyX(graphicEqFrequencies[i]), graphBounds.left(), graphBounds.right());
        points[i] = { x, gainY(paramsForCurve.bandGainsDb[i], graphTop, graphBottom) };
        if (showFrequencyValues_ && (i % 2 == 0 || i + 1 == graphicEqBandCount)) {
            const auto xLabel = frequencyX(graphicEqFrequencies[i]);
            if (xLabel >= graphBounds.left() - 14 && xLabel <= graphBounds.right() + 14)
                drawText(p, { xLabel - 19, graphBottom + 5, 38, 16 }, freqLabel(graphicEqFrequencies[i]), 10, muted);
        }
    }

    // Faint frequency divisions sit behind the stems, curve, and slider caps.
    Controls::ChartGridStyle frequencyGrid;
    frequencyGrid.grid=themed("chart-grid-minor", QColor(21,42,60));
    for (std::size_t i = 0; i < graphicEqBandCount; ++i) {
        const auto x = std::clamp(frequencyX(graphicEqFrequencies[i]), graphBounds.left(), graphBounds.right());
        frequencyGrid.verticalFractions.push_back((x-graphBounds.left())/graphBounds.width());
    }
    Controls::drawChartGrid(p,gridBounds,frequencyGrid);
    if (hasSpectrumFrame_) {
        QPainterPath outputSpectrumArea;
        QPainterPath outputSpectrumLine;
        QPainterPath inputSpectrumLine;
        constexpr qreal spectrumFloorDb = -90.0;
        constexpr qreal spectrumCeilingDb = 6.0;
        for (std::size_t i = 0; i < spectrum_.outputDb.size(); ++i) {
            const auto normalized = (static_cast<float>(i) + 0.5F)
                / static_cast<float>(spectrum_.outputDb.size());
            const auto frequency = 20.0 * std::pow(1000.0, normalized);
            const auto x = frequencyX(frequency);
            const auto inputLevel = std::clamp<qreal>(spectrum_.inputDb[i], spectrumFloorDb, spectrumCeilingDb);
            const auto outputLevel = std::clamp<qreal>(spectrum_.outputDb[i], spectrumFloorDb, spectrumCeilingDb);
            const auto inputY = graphBottom - (inputLevel - spectrumFloorDb)
                / (spectrumCeilingDb - spectrumFloorDb) * (graphBottom - graphTop);
            const auto outputY = graphBottom - (outputLevel - spectrumFloorDb)
                / (spectrumCeilingDb - spectrumFloorDb) * (graphBottom - graphTop);
            if (i == 0) {
                outputSpectrumArea.moveTo(x, graphBottom);
                outputSpectrumArea.lineTo(x, outputY);
                outputSpectrumLine.moveTo(x, outputY);
                inputSpectrumLine.moveTo(x, inputY);
            } else {
                outputSpectrumArea.lineTo(x, outputY);
                outputSpectrumLine.lineTo(x, outputY);
                inputSpectrumLine.lineTo(x, inputY);
            }
        }
        outputSpectrumArea.lineTo(frequencyX(20000.0), graphBottom);
        outputSpectrumArea.closeSubpath();
        QLinearGradient spectrumFill(0.0, graphTop, 0.0, graphBottom);
        spectrumFill.setColorAt(0.0, themed("chart-series-output-spectrum-fill-start", QColor(20, 155, 198, 58)));
        spectrumFill.setColorAt(1.0, themed("chart-series-output-spectrum-fill-end", QColor(20, 155, 198, 12)));
        Controls::CurveStyle spectrumAreaStyle;
        spectrumAreaStyle.showFill = true;
        spectrumAreaStyle.showStroke = false;
        spectrumAreaStyle.fillBrush = QBrush(spectrumFill);
        Controls::drawCurvePath(p, outputSpectrumLine, spectrumAreaStyle, &outputSpectrumArea);
        Controls::CurveStyle inputSpectrumStyle;
        inputSpectrumStyle.stroke = themed("chart-series-input-spectrum-stroke", QColor(242, 184, 98, 220));
        inputSpectrumStyle.width = 1.6;
        inputSpectrumStyle.penStyle = Qt::DashLine;
        Controls::drawCurvePath(p, inputSpectrumLine, inputSpectrumStyle);
        Controls::CurveStyle outputSpectrumStyle;
        outputSpectrumStyle.stroke = themed("chart-series-output-spectrum-stroke", QColor(65, 205, 232, 180));
        outputSpectrumStyle.width = 1.6;
        Controls::drawCurvePath(p, outputSpectrumLine, outputSpectrumStyle);

        p.setPen(QPen(QColor::fromRgb(inputSpectrumStyle.stroke.rgb()), 1.6, Qt::DashLine));
        p.drawLine(QPointF(405, 112), QPointF(423, 112));
        drawText(p, { 426, 103, 58, 18 }, QStringLiteral("输入参考"), 10, muted);
        p.setPen(QPen(QColor::fromRgb(outputSpectrumStyle.stroke.rgb()), 1.6));
        p.drawLine(QPointF(490, 112), QPointF(508, 112));
        drawText(p, { 511, 103, 55, 18 }, QStringLiteral("EQ 后"), 10, muted);
        if (selectedChannel_ < 0)
            drawText(p, { 568, 103, 56, 18 }, QStringLiteral("%1/8 路").arg(spectrum_.activeChannelCount), 10, muted);
    }
    const bool drawCurve = showCurve_ && viewMode_ != 1;
    const bool drawFaders = viewMode_ != 2;
    if (drawFaders) {
        for (const auto& point : points) {
            p.setPen(QPen(QColor(48, 96, 128), 1));
            p.drawLine(QPointF(point.x(), graphTop), QPointF(point.x(), graphBottom));
        }
    }
    const auto curve = smoothCurve(points);
    if (drawCurve) {
        Controls::CurveStyle responseStyle;
        responseStyle.stroke = themed("chart-series-response-stroke", cyan);
        responseStyle.width = 2.5;
        Controls::drawCurvePath(p, curve, responseStyle);
    }
    if (drawFaders) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& point = points[i];
            const bool dragging = static_cast<int>(i) == draggingBand_;
            Controls::drawSlider(p, {point.x()-4, graphTop, 8, graphBottom-graphTop},
                (graphBottom-point.y())/(graphBottom-graphTop), true, sliderColors);
        }
    }
    Q_UNUSED(graphHeight);

    const std::array<QString, 4> actions { QStringLiteral("重置"), QStringLiteral("平直"),
        QStringLiteral("加载…"), QStringLiteral("保存…") };
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        const QRectF button(650, 149 + i * 54, 90, 42);
        Controls::drawButton(p, button, actions[static_cast<std::size_t>(i)], false, controls, 14);
    }

    // Three independent lower cards mirror the supplied layout.
    rounded(p, { 18, 392, 168, 100 }, QColor(14, 33, 51), QColor(35, 65, 90), 8);
    rounded(p, { 197, 392, 387, 100 }, QColor(14, 33, 51), QColor(35, 65, 90), 8);
    rounded(p, { 592, 392, 148, 100 }, QColor(14, 33, 51), QColor(35, 65, 90), 8);
    drawText(p, { 31, 399, 139, 20 }, QStringLiteral("输出增益"), 12, muted, Qt::AlignLeft);
    const auto outputNormalized = (params.outputGainDb + 24.0) / 36.0;
    Controls::drawKnob(p, { 119, 442 }, outputNormalized, 29, knobColors);
    drawText(p, { 81, 469, 76, 17 }, QStringLiteral("%1 dB").arg(params.outputGainDb, 0, 'f', 1), 12, text);
    drawText(p, { 211, 399, 345, 20 }, QStringLiteral("显示模式"), 12, muted, Qt::AlignLeft);
    const std::array<QString, 3> viewLabels { QStringLiteral("31 段"), QStringLiteral("分频视图"), QStringLiteral("曲线视图") };
    for (int i = 0; i < 3; ++i) {
        const QRectF button(211 + i * 126, 428, 119, 34);
        const bool active = viewMode_ == i;
        rounded(p, button, active ? QColor(7, 75, 110) : QColor(16, 37, 56),
                active ? cyan : QColor(41, 72, 97), 5);
        drawText(p, button, viewLabels[static_cast<std::size_t>(i)], 11, active ? cyan : text);
    }
    const auto selectedBand = static_cast<std::size_t>(std::clamp(selectedBand_, 0,
        static_cast<int>(graphicEqBandCount) - 1));
    drawText(p, { 211, 467, 171, 21 },
        QStringLiteral("选中 %1").arg(freqLabel(graphicEqFrequencies[selectedBand])),
        11, muted, Qt::AlignLeft);
    rounded(p, { 391, 466, 174, 23 }, QColor(16, 37, 56), QColor(41, 72, 97), 4);
    drawText(p, { 394, 467, 168, 21 },
        QStringLiteral("%1 dB").arg(params.bandGainsDb[selectedBand], 0, 'f', 1), 11, text);
    drawText(p, { 604, 399, 125, 20 }, QStringLiteral("显示选项"), 12, muted, Qt::AlignLeft);
    const auto checkbox = [&](qreal y, bool checked, const QString& label) {
        const QRectF box(608, y, 18, 18);
        rounded(p, box, checked ? QColor(0, 116, 170) : QColor(14, 32, 49),
                checked ? cyan : QColor(44, 74, 98), 3);
        if (checked) drawText(p, box, QStringLiteral("✓"), 12, Qt::white);
        drawText(p, { 635, y - 2, 93, 22 }, label, 11, text, Qt::AlignLeft);
    };
    checkbox(428, showFrequencyValues_, QStringLiteral("显示频率值"));
    checkbox(460, showCurve_, QStringLiteral("显示曲线"));
    p.restore();
}

void GraphicEqWidget::setBandAt(const QPointF& point)
{
    const auto localY = point.y() - selectorHeight;
    const auto graphTop = graphBounds.top() + 24.0;
    const auto graphBottom = graphBounds.bottom();
    const auto gain = std::clamp(static_cast<float>(12.0 - (localY - graphTop)
        / (graphBottom - graphTop) * 24.0), -12.0F, 12.0F);
    if (draggingBand_ < 0) {
        qreal nearestDistance = std::numeric_limits<qreal>::max();
        for (std::size_t i = 0; i < graphicEqBandCount; ++i) {
            const auto x = std::clamp(frequencyX(graphicEqFrequencies[i]), graphBounds.left(), graphBounds.right());
            const auto distance = std::abs(point.x() - x);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                draggingBand_ = static_cast<int>(i);
            }
        }
    }
    if (draggingBand_ < 0)
        return;
    selectedBand_ = draggingBand_;
    const auto band = static_cast<std::size_t>(draggingBand_);
    apply([&](Parameters& parameters) { parameters.bandGainsDb[band] = gain; });
}

void GraphicEqWidget::mousePressEvent(QMouseEvent* event)
{
    const auto point = event->position();
    if (point.y() >= 68 && point.y() <= 110) {
        for (int i = 0; i < 9; ++i) {
            if (channelButton(i).contains(point)) {
                const auto nextChannel = i == 0 ? -1 : i - 1;
                if (nextChannel != selectedChannel_) {
                    selectedChannel_ = nextChannel;
                    if (hasSpectrumFrame_)
                        refreshSpectrumForSelection(false);
                    else
                        update();
                }
                return;
            }
        }
    }
    const auto localY = point.y() - selectorHeight;
    if (QRectF(81, selectorHeight + 469, 76, 19).contains(point)) {
        Controls::editNumericValue(this, QRect(81, qRound(selectorHeight + 469), 76, 19),
            displayed().outputGainDb, {-24, 12, 0, 1}, [this](double value) {
                apply([value](Parameters& p) { p.outputGainDb = static_cast<float>(value); });
            });
        return;
    }
    if (QRectF(391, selectorHeight + 466, 174, 23).contains(point)) {
        const auto band = static_cast<std::size_t>(std::clamp(selectedBand_, 0,
            static_cast<int>(graphicEqBandCount) - 1));
        Controls::editNumericValue(this, QRect(391, qRound(selectorHeight + 466), 174, 23),
            displayed().bandGainsDb[band], {-12, 12, 0, 1}, [this, band](double value) {
                apply([band, value](Parameters& p) {
                    p.bandGainsDb[band] = static_cast<float>(value);
                });
            });
        return;
    }
    if (localY >= graphBounds.top() + 24.0 && localY <= graphBounds.bottom()
        && point.x() >= graphBounds.left() - 7 && point.x() <= graphBounds.right() + 7) {
        draggingBand_ = -1;
        setBandAt(point);
        if (draggingBand_ >= 0) spectrumTimer_.start();
        return;
    }
    if (point.x() >= 650 && point.x() <= 740 && localY >= 149 && localY <= 353) {
        const int action = std::clamp(static_cast<int>((localY - 149) / 54), 0, 3);
        if (action == 0)
            apply([](Parameters& p) { p.bandGainsDb.fill(0.0F); p.outputGainDb = 0.0F; });
        else if (action == 1)
            apply([](Parameters& p) { p.bandGainsDb.fill(0.0F); });
        else if (action == 2)
            loadPresetFile();
        else
            savePresetFile();
        if (action == 0 || (action == 1 && std::abs(displayed().outputGainDb) < 0.001F))
            selectedPreset_ = 0;
        return;
    }
    if (localY >= 29 && localY <= 52 && point.x() >= 356 && point.x() <= 394) {
        const auto enabled = !displayed().enabled;
        apply([enabled](Parameters& p) { p.enabled = enabled; });
        return;
    }
    if (localY >= 27 && localY <= 60 && point.x() >= 446 && point.x() <= 620) {
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose, true);
        menu->setStyleSheet(QStringLiteral(
            "QMenu { background: #10263a; color: #e1edf8; border: 1px solid #315775; padding: 4px; }"
            "QMenu::item { padding: 7px 22px; }"
            "QMenu::item:selected { background: #075478; color: #56d9f4; }"));
        const std::array<QString, 5> presets { QStringLiteral("Default"), QStringLiteral("Vocal Bright"),
            QStringLiteral("Bass Boost"), QStringLiteral("Classical"), QStringLiteral("Custom") };
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            auto* action = menu->addAction(presets[static_cast<std::size_t>(i)]);
            action->setCheckable(true);
            action->setChecked(i == selectedPreset_);
        }
        connect(menu, &QMenu::triggered, this, [this, menu](QAction* action) {
            selectPreset(menu->actions().indexOf(action));
        });
        menu->popup(mapToGlobal(QPoint(446, qRound(selectorHeight + 60.0))));
        return;
    }
    if (localY >= 27 && localY <= 60 && point.x() >= 628 && point.x() <= 696) {
        const auto delta = point.x() < 666 ? 4 : 1;
        const auto next = (selectedPreset_ + delta) % 5;
        selectPreset(next);
        return;
    }
    if (localY >= 428 && localY <= 462 && point.x() >= 211 && point.x() <= 582) {
        viewMode_ = std::clamp(static_cast<int>((point.x() - 211) / 126), 0, 2);
        update();
        return;
    }
    if (localY >= 428 && localY <= 478 && point.x() >= 608 && point.x() <= 730) {
        if (localY < 460) showFrequencyValues_ = !showFrequencyValues_;
        else showCurve_ = !showCurve_;
        update();
        return;
    }
    if (localY >= 418 && localY <= 465 && point.x() >= 95 && point.x() <= 144) {
        const auto center = QPointF(119, 442);
        const auto angle = std::atan2(-(point.y() - selectorHeight - center.y()), point.x() - center.x());
        const auto degrees = std::clamp(225.0 - angle * 180.0 / 3.14159265358979323846, 0.0, 270.0);
        const auto gain = static_cast<float>(-24.0 + degrees / 270.0 * 36.0);
        apply([gain](Parameters& p) { p.outputGainDb = gain; });
    }
}

void GraphicEqWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto point = event->position();
    const auto localY = point.y() - selectorHeight;
    if (QRectF(90, selectorHeight + 413, 58, 55).contains(point)) {
        Controls::resetNumericValue({-24, 12, 0, 1}, [this](double value) {
            apply([value](Parameters& p) { p.outputGainDb = static_cast<float>(value); });
        });
        return;
    }
    if (viewMode_ == 2 || localY < graphBounds.top() + 24.0
        || localY > graphBounds.bottom() || point.x() < graphBounds.left() - 7
        || point.x() > graphBounds.right() + 7) return;
    qreal nearestDistance = std::numeric_limits<qreal>::max();
    for (std::size_t band = 0; band < graphicEqBandCount; ++band) {
        const auto x = std::clamp(frequencyX(graphicEqFrequencies[band]),
            graphBounds.left(), graphBounds.right());
        const auto distance = std::abs(point.x() - x);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            selectedBand_ = static_cast<int>(band);
        }
    }
    draggingBand_ = -1;
    const auto band = static_cast<std::size_t>(selectedBand_);
    Controls::resetNumericValue({-12, 12, 0, 1}, [this, band](double value) {
        apply([band, value](Parameters& p) {
            p.bandGainsDb[band] = static_cast<float>(value);
        });
    });
}

void GraphicEqWidget::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && draggingBand_ >= 0)
        setBandAt(event->position());
}

void GraphicEqWidget::mouseReleaseEvent(QMouseEvent*)
{
    draggingBand_ = -1;
    if (!spectrumFrameReader_) spectrumTimer_.stop();
    update();
}

void GraphicEqWidget::loadPresetFile()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("加载均衡器预设"), {}, QStringLiteral("EQ preset (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return;
    const auto values = document.object().value(QStringLiteral("bands")).toArray();
    if (values.size() != static_cast<int>(graphicEqBandCount)) return;
    std::array<float, graphicEqBandCount> gains {};
    for (int i = 0; i < values.size(); ++i) {
        if (!values[i].isDouble()) return;
        gains[static_cast<std::size_t>(i)] = static_cast<float>(std::clamp(values[i].toDouble(), -12.0, 12.0));
    }
    apply([&](Parameters& p) { p.bandGainsDb = gains; });
}

void GraphicEqWidget::savePresetFile()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存均衡器预设"), {}, QStringLiteral("EQ preset (*.json)"));
    if (path.isEmpty()) return;
    QJsonArray bands;
    for (const auto gain : displayed().bandGainsDb) bands.append(gain);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(QJsonObject {{ QStringLiteral("bands"), bands }}).toJson(QJsonDocument::Indented));
}

}
