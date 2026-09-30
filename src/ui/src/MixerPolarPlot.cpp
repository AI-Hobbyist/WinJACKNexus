/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/MixerPolarPlot.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
QColor polarColor(const QString& scope, const char* role, const QColor& fallback)
{
    return Controls::themeTokenColor(scope, QString::fromLatin1(role), fallback);
}
}

void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius)
{
    drawMixerPolarGrid(painter, center, radius, QStringLiteral("sel-main"));
}

void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius,
                        const QString& themeScope)
{
    if (radius <= 0) return;
    painter.save();
    painter.setBrush(Qt::NoBrush);
    for (int ring = 1; ring <= 5; ++ring) {
        painter.setPen(QPen(polarColor(themeScope, "chart-grid-ring", QColor("#244a66")), 1));
        painter.drawEllipse(center, radius * ring / 5, radius * ring / 5);
    }
    constexpr double pi = 3.14159265358979323846;
    for (int angle = 0; angle < 360; angle += 30) {
        const auto radians = angle * pi / 180.0;
        painter.setPen(QPen(angle % 90 == 0
            ? polarColor(themeScope, "chart-grid-axis-major", QColor("#426884"))
            : polarColor(themeScope, "chart-grid-axis-minor", QColor("#1b3d56")), 1));
        painter.drawLine(center, center + QPointF(std::sin(radians), -std::cos(radians)) * radius);
    }
    painter.restore();
}

void drawMixerPolarReferencePoint(QPainter& painter, QPointF point, double gain)
{
    drawMixerPolarReferencePoint(painter, point, gain, QStringLiteral("sel-main"));
}

void drawMixerPolarReferencePoint(QPainter& painter, QPointF point, double gain,
                                  const QString& themeScope)
{
    gain = std::clamp(gain, 0.0, 1.0);
    painter.save();
    auto stroke = polarColor(themeScope, "chart-reference-stroke", QColor(138, 169, 214));
    auto fill = polarColor(themeScope, "chart-reference-fill", QColor(109, 141, 184));
    stroke.setAlpha(110 + qRound(gain * 145));
    fill.setAlpha(75 + qRound(gain * 165));
    painter.setPen(QPen(stroke, 2));
    painter.setBrush(fill);
    painter.drawEllipse(point, 10 + gain * 3, 10 + gain * 3);
    painter.restore();
}

void drawMixerPolarHandle(QPainter& painter, QPointF point,
                          qreal influenceRadius, bool selected)
{
    drawMixerPolarHandle(painter, point, influenceRadius, selected,
                         QStringLiteral("sel-main"));
}

void drawMixerPolarHandle(QPainter& painter, QPointF point,
                          qreal influenceRadius, bool selected,
                          const QString& themeScope)
{
    painter.save();
    auto influenceStroke = polarColor(themeScope, selected
        ? "chart-handle-influence-stroke-selected" : "chart-handle-influence-stroke-normal",
        selected ? QColor("#79e3ff") : QColor(85, 189, 233));
    if (!selected) influenceStroke.setAlpha(115);
    auto influenceFill = polarColor(themeScope, "chart-handle-influence-fill", QColor(22, 141, 255));
    influenceFill.setAlpha(selected ? 28 : 9);
    painter.setPen(QPen(influenceStroke, selected ? 2.5 : 1.2));
    painter.setBrush(influenceFill);
    painter.drawEllipse(point, influenceRadius, influenceRadius);
    if (selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(polarColor(themeScope, "chart-handle-marker", QColor("#79e3ff")));
        painter.drawEllipse(point + QPointF(influenceRadius, 0), 4, 4);
        painter.setPen(QPen(polarColor(themeScope, "chart-handle-ring", QColor("#7ee8ff")), 5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(point, 14, 14);
    }
    painter.setPen(QPen(polarColor(themeScope, selected ? "chart-handle-border-selected"
        : "chart-handle-border-normal", selected ? QColor("#e8fbff") : QColor("#9dc3ff")),
        selected ? 3 : 2));
    painter.setBrush(polarColor(themeScope, selected ? "chart-handle-surface-selected"
        : "chart-handle-surface-normal", selected ? QColor("#42a8ff") : QColor("#2e9dff")));
    painter.drawEllipse(point, 10, 10);
    painter.restore();
}

void drawMixerPolarCenter(QPainter& painter, QPointF center)
{
    drawMixerPolarCenter(painter, center, QStringLiteral("sel-main"));
}

void drawMixerPolarCenter(QPainter& painter, QPointF center,
                          const QString& themeScope)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(polarColor(themeScope, "chart-center-marker", QColor(Qt::white)));
    painter.drawEllipse(center, 4, 4);
    painter.restore();
}

void drawMixerPolarCurves(QPainter& painter, QRectF panel, QPointF center, qreal radius,
    const Common::MixerPolarContour& contour, bool showInput, bool showOutput)
{
    drawMixerPolarCurves(painter, panel, center, radius, contour, showInput, showOutput,
                         QStringLiteral("sel-main"));
}

void drawMixerPolarCurves(QPainter& painter, QRectF panel, QPointF center, qreal radius,
    const Common::MixerPolarContour& contour, bool showInput, bool showOutput,
    const QString& themeScope)
{
    if (!contour.valid) return;
    const auto inputStroke = Controls::themeTokenColor(themeScope,
        QStringLiteral("chart-series-polar-input-stroke"), QColor("#28c9f3"));
    const auto outputStroke = Controls::themeTokenColor(themeScope,
        QStringLiteral("chart-series-polar-output-stroke"), QColor("#ffa44a"));
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(polarColor(themeScope, "chart-grid-zero", QColor("#4b708a")),
                        1, Qt::DotLine));
    const auto zeroDbRadius = radius * 72.0 / 84.0;
    painter.drawEllipse(center, zeroDbRadius, zeroDbRadius);
    const auto draw = [&](const std::array<float, 360>& values, QColor stroke,
                          Qt::PenStyle penStyle, int alpha) {
        Controls::CurveStyle style;
        style.stroke = stroke;
        style.fill = stroke;
        style.fill.setAlpha(alpha);
        style.width = 2.4;
        style.penStyle = penStyle;
        style.showFill = true;
        Controls::drawPolarCurve(painter, center, radius, values, style);
    };
    if (showInput) draw(contour.inputRadius, inputStroke, Qt::SolidLine, 24);
    if (showOutput) draw(contour.outputRadius, outputStroke, Qt::DashLine, 30);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(polarColor(themeScope, "chart-axis-label", QColor("#9ab8cc")));
    painter.drawText(QRectF(center.x() - radius, center.y() - radius - 17, 55, 16),
                     Qt::AlignLeft, QStringLiteral("0 dBFS"));
    // The compact legend fits in the clear corner above and beside the dial.
    const QRectF legend(panel.right() - 108, panel.top() + 8, 100, 42);
    painter.setPen(QPen(polarColor(themeScope, "chart-legend-border", QColor("#385773")), 1));
    painter.setBrush(polarColor(themeScope, "chart-legend-surface", QColor(9, 28, 43, 215)));
    painter.drawRoundedRect(legend, 5, 5);
    painter.setBrush(Qt::NoBrush);
    if (showInput) {
        painter.setPen(QPen(inputStroke, 2));
        painter.drawLine(QPointF(legend.left() + 7, legend.top() + 11),
                         QPointF(legend.left() + 24, legend.top() + 11));
        painter.setPen(polarColor(themeScope, "chart-legend-input-text", inputStroke));
        painter.drawText(QRectF(legend.left() + 28, legend.top() + 1, 70, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("输入"));
    }
    if (showOutput) {
        painter.setPen(QPen(outputStroke, 2, Qt::DashLine));
        painter.drawLine(QPointF(legend.left() + 7, legend.top() + 31),
                         QPointF(legend.left() + 24, legend.top() + 31));
        painter.setPen(polarColor(themeScope, "chart-legend-output-text", outputStroke));
        painter.drawText(QRectF(legend.left() + 28, legend.top() + 21, 70, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("输出"));
    }
    if (contour.inputOverrange || contour.outputOverrange) {
        painter.setPen(polarColor(themeScope, "chart-overrange-label", QColor("#ff6c6c")));
        painter.drawText(QRectF(center.x() - radius + 60, center.y() - radius - 18, 80, 18),
                         Qt::AlignRight, QStringLiteral("+12 dBFS"));
    }
    painter.restore();
}

}
