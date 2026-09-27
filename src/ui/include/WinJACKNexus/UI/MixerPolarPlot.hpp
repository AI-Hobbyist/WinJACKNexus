/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/MixerPolarCurve.hpp>
#include <WinJACKNexus/UI/Export.hpp>

#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace WinJACKNexus::UI {

WINJACK_UI_API void drawMixerPolarCurves(QPainter& painter, QRectF panel, QPointF center, qreal radius,
    const Common::MixerPolarContour& contour, bool showInput, bool showOutput);
WINJACK_UI_API void drawMixerPolarCurves(QPainter& painter, QRectF panel, QPointF center, qreal radius,
    const Common::MixerPolarContour& contour, bool showInput, bool showOutput,
    const QString& themeScope);
WINJACK_UI_API void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius);
WINJACK_UI_API void drawMixerPolarGrid(QPainter& painter, QPointF center, qreal radius,
                                      const QString& themeScope);
WINJACK_UI_API void drawMixerPolarReferencePoint(QPainter& painter, QPointF point, double gain);
WINJACK_UI_API void drawMixerPolarReferencePoint(QPainter& painter, QPointF point, double gain,
                                                const QString& themeScope);
WINJACK_UI_API void drawMixerPolarHandle(QPainter& painter, QPointF point,
                                         qreal influenceRadius, bool selected);
WINJACK_UI_API void drawMixerPolarHandle(QPainter& painter, QPointF point,
                                         qreal influenceRadius, bool selected,
                                         const QString& themeScope);
WINJACK_UI_API void drawMixerPolarCenter(QPainter& painter, QPointF center);
WINJACK_UI_API void drawMixerPolarCenter(QPainter& painter, QPointF center,
                                         const QString& themeScope);

}
