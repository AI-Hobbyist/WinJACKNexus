/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/MixerPolarPlot.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include "MixerPolarAccent.hpp"
#include "SelThemeAccent.hpp"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
QColor polarColor(const QString& scope, const char* role, const QColor& fallback)
{
    return Controls::themeTokenColor(scope, QString::fromLatin1(role), fallback);
}

QColor withAccent(const QString& scope, const char* role,
                  const QColor& semantic, const QColor& accent)
{
    if (!accent.isValid() || !selThemeRoleUsesDefault(scope, QString::fromLatin1(role))) return semantic;
    constexpr qreal accentWeight = 0.8;
    return QColor::fromRgbF(semantic.redF() * (1.0 - accentWeight) + accent.redF() * accentWeight,
                            semantic.greenF() * (1.0 - accentWeight) + accent.greenF() * accentWeight,
                            semantic.blueF() * (1.0 - accentWeight) + accent.blueF() * accentWeight,
                            semantic.alphaF());
}
}

void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius)
{
    drawMixerPolarGrid(painter, center, radius, QStringLiteral("sel-main"));
}

void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius,
                        const QString& themeScope)
{
    drawMixerPolarGrid(painter, center, radius, themeScope, {});
}

void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius,
                        const QString& themeScope, const QColor& accent)
{
    if (radius <= 0) return;
    painter.save();
    painter.setBrush(Qt::NoBrush);
    for (int ring = 1; ring <= 5; ++ring) {
        painter.setPen(QPen(withAccent(themeScope, "chart-grid-ring",
                                       polarColor(themeScope, "chart-grid-ring", QColor("#244a66")),
                                       accent.isValid() ? accent.darker(250) : accent), 1));
        painter.drawEllipse(center, radius * ring / 5, radius * ring / 5);
    }
    constexpr double pi = 3.14159265358979323846;
    for (int angle = 0; angle < 360; angle += 30) {
        const auto radians = angle * pi / 180.0;
        const char* role = angle % 90 == 0 ? "chart-grid-axis-major" : "chart-grid-axis-minor";
        painter.setPen(QPen(withAccent(themeScope, role,
            polarColor(themeScope, role, angle % 90 == 0 ? QColor("#426884") : QColor("#1b3d56")),
            accent.isValid() ? accent.darker(250) : accent), 1));
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
    drawMixerPolarReferencePoint(painter, point, gain, themeScope, {});
}

void drawMixerPolarReferencePoint(QPainter& painter, QPointF point, double gain,
                                  const QString& themeScope, const QColor& accent)
{
    gain = std::clamp(gain, 0.0, 1.0);
    painter.save();
    auto stroke = withAccent(themeScope, "chart-reference-stroke",
        polarColor(themeScope, "chart-reference-stroke", QColor(138, 169, 214)), accent);
    auto fill = withAccent(themeScope, "chart-reference-fill",
        polarColor(themeScope, "chart-reference-fill", QColor(109, 141, 184)), accent);
    if (selThemeRoleUsesDefault(themeScope, QStringLiteral("chart-reference-stroke")))
        stroke.setAlpha(110 + qRound(gain * 145));
    if (selThemeRoleUsesDefault(themeScope, QStringLiteral("chart-reference-fill")))
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
    drawMixerPolarHandle(painter, point, influenceRadius, selected, themeScope, {});
}

void drawMixerPolarHandle(QPainter& painter, QPointF point,
                          qreal influenceRadius, bool selected,
                          const QString& themeScope, const QColor& accent)
{
    painter.save();
    const char* influenceRole = selected ? "chart-handle-influence-stroke-selected"
        : "chart-handle-influence-stroke-normal";
    auto influenceStroke = withAccent(themeScope, influenceRole,
        polarColor(themeScope, influenceRole,
                   selected ? QColor("#79e3ff") : QColor(85, 189, 233)), accent);
    if (!selected && selThemeRoleUsesDefault(themeScope, QString::fromLatin1(influenceRole)))
        influenceStroke.setAlpha(115);
    auto influenceFill = withAccent(themeScope, "chart-handle-influence-fill",
        polarColor(themeScope, "chart-handle-influence-fill", QColor(22, 141, 255)), accent);
    if (selThemeRoleUsesDefault(themeScope, QStringLiteral("chart-handle-influence-fill")))
        influenceFill.setAlpha(selected ? 28 : 9);
    painter.setPen(QPen(influenceStroke, selected ? 2.5 : 1.2));
    painter.setBrush(influenceFill);
    painter.drawEllipse(point, influenceRadius, influenceRadius);
    if (selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAccent(themeScope, "chart-handle-marker",
            polarColor(themeScope, "chart-handle-marker", QColor("#79e3ff")), accent));
        painter.drawEllipse(point + QPointF(influenceRadius, 0), 4, 4);
        painter.setPen(QPen(withAccent(themeScope, "chart-handle-ring",
            polarColor(themeScope, "chart-handle-ring", QColor("#7ee8ff")), accent), 5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(point, 14, 14);
    }
    const char* borderRole = selected ? "chart-handle-border-selected" : "chart-handle-border-normal";
    painter.setPen(QPen(withAccent(themeScope, borderRole,
        polarColor(themeScope, borderRole, selected ? QColor("#e8fbff") : QColor("#9dc3ff")),
        selected ? QColor() : accent),
        selected ? 3 : 2));
    const char* surfaceRole = selected ? "chart-handle-surface-selected" : "chart-handle-surface-normal";
    painter.setBrush(withAccent(themeScope, surfaceRole,
        polarColor(themeScope, surfaceRole, selected ? QColor("#42a8ff") : QColor("#2e9dff")), accent));
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
    drawMixerPolarCurves(painter, panel, center, radius, contour, showInput, showOutput,
                         themeScope, {});
}

void drawMixerPolarCurves(QPainter& painter, QRectF panel, QPointF center, qreal radius,
    const Common::MixerPolarContour& contour, bool showInput, bool showOutput,
    const QString& themeScope, const QColor& accent)
{
    if (!contour.valid) return;
    const auto inputStroke = withAccent(themeScope, "chart-series-polar-input-stroke",
        polarColor(themeScope, "chart-series-polar-input-stroke", QColor("#28c9f3")), accent);
    const auto outputStroke = withAccent(themeScope, "chart-series-polar-output-stroke",
        polarColor(themeScope, "chart-series-polar-output-stroke", QColor("#ffa44a")), accent);
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(withAccent(themeScope, "chart-grid-zero",
        polarColor(themeScope, "chart-grid-zero", QColor("#4b708a")),
        accent.isValid() ? accent.darker(250) : accent), 1, Qt::DotLine));
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
    painter.setPen(QPen(withAccent(themeScope, "chart-legend-border",
        polarColor(themeScope, "chart-legend-border", QColor("#385773")),
        accent.isValid() ? accent.darker(190) : accent), 1));
    painter.setBrush(polarColor(themeScope, "chart-legend-surface", QColor(9, 28, 43, 215)));
    painter.drawRoundedRect(legend, 5, 5);
    painter.setBrush(Qt::NoBrush);
    if (showInput) {
        painter.setPen(QPen(inputStroke, 2));
        painter.drawLine(QPointF(legend.left() + 7, legend.top() + 11),
                         QPointF(legend.left() + 24, legend.top() + 11));
        painter.setPen(accent.isValid() && selThemeRoleUsesDefault(themeScope, QStringLiteral("chart-legend-input-text"))
            ? inputStroke : polarColor(themeScope, "chart-legend-input-text", inputStroke));
        painter.drawText(QRectF(legend.left() + 28, legend.top() + 1, 70, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("输入"));
    }
    if (showOutput) {
        painter.setPen(QPen(outputStroke, 2, Qt::DashLine));
        painter.drawLine(QPointF(legend.left() + 7, legend.top() + 31),
                         QPointF(legend.left() + 24, legend.top() + 31));
        painter.setPen(accent.isValid() && selThemeRoleUsesDefault(themeScope, QStringLiteral("chart-legend-output-text"))
            ? outputStroke : polarColor(themeScope, "chart-legend-output-text", outputStroke));
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
