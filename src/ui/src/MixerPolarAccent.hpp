/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/UI/MixerPolarPlot.hpp>

#include <QColor>

class QPainter;

namespace WinJACKNexus::UI {

// Internal overloads for SEL pages that know the selected channel type.
void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius,
                        const QString& themeScope, const QColor& accent);
void drawMixerPolarReferencePoint(QPainter& painter, QPointF point, double gain,
                                  const QString& themeScope, const QColor& accent);
void drawMixerPolarHandle(QPainter& painter, QPointF point, qreal influenceRadius,
                          bool selected, const QString& themeScope, const QColor& accent);
void drawMixerPolarCurves(QPainter& painter, QRectF panel, QPointF center, qreal radius,
                          const Common::MixerPolarContour& contour, bool showInput,
                          bool showOutput, const QString& themeScope, const QColor& accent);

}
