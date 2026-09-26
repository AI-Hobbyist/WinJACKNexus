/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {

namespace {

QColor themeColor(const ThemePalette& palette, const char* key, const QColor& fallback)
{
    const auto color = palette.color(QString::fromLatin1(key));
    return color.isValid() ? color : fallback;
}

}

MetricReadout::MetricReadout(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(124, 72);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
}

void MetricReadout::setLabel(const QString& label)
{
    if (label_ == label)
        return;
    label_ = label;
    update();
}

QString MetricReadout::label() const
{
    return label_;
}

void MetricReadout::setValue(std::optional<double> value)
{
    if (!value.has_value() || !std::isfinite(*value)) {
        clearValue();
        return;
    }
    if (value_ == value)
        return;
    if (peakValueRefreshEnabled_ && value_.has_value() && valueRefreshClock_.isValid()
        && valueRefreshClock_.elapsed()
            < ApplicationSettings::instance().peakValueRefreshMilliseconds())
        return;
    value_ = value;
    if (peakValueRefreshEnabled_) valueRefreshClock_.start();
    update();
}

void MetricReadout::clearValue()
{
    if (!value_.has_value())
        return;
    value_.reset();
    valueRefreshClock_.invalidate();
    update();
}

std::optional<double> MetricReadout::value() const noexcept
{
    return value_;
}

void MetricReadout::setPeakValueRefreshEnabled(bool enabled)
{
    if (peakValueRefreshEnabled_ == enabled) return;
    peakValueRefreshEnabled_ = enabled;
    valueRefreshClock_.invalidate();
}

bool MetricReadout::peakValueRefreshEnabled() const noexcept
{
    return peakValueRefreshEnabled_;
}

void MetricReadout::setUnit(const QString& unit)
{
    if (unit_ == unit)
        return;
    unit_ = unit;
    update();
}

QString MetricReadout::unit() const
{
    return unit_;
}

void MetricReadout::setPrecision(int precision)
{
    const auto normalized = std::clamp(precision, 0, 4);
    if (precision_ == normalized)
        return;
    precision_ = normalized;
    update();
}

int MetricReadout::precision() const noexcept
{
    return precision_;
}

QString MetricReadout::displayText() const
{
    return value_.has_value() ? QString::number(*value_, 'f', precision_) : QStringLiteral("--");
}

void MetricReadout::setAccentColor(const QColor& color)
{
    if (accentColor_ == color)
        return;
    accentColor_ = color;
    update();
}

QColor MetricReadout::accentColor() const
{
    return accentColor_;
}

void MetricReadout::setShowSurface(bool visible)
{
    if (showSurface_ == visible)
        return;
    showSurface_ = visible;
    setMinimumSize(showSurface_ ? QSize(124, 72) : QSize(96, 58));
    updateGeometry();
    update();
}

bool MetricReadout::showSurface() const noexcept
{
    return showSurface_;
}

void MetricReadout::setThemePalette(const ThemePalette& palette)
{
    if (!palette.isValid())
        return;
    themePalette_ = palette;
    update();
}

ThemePalette MetricReadout::themePalette() const
{
    return themePalette_;
}

QSize MetricReadout::sizeHint() const
{
    return showSurface_ ? QSize(148, 78) : QSize(104, 60);
}

void MetricReadout::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto panelBackground = themeColor(themePalette_, "panel-bg", palette().color(QPalette::Window));
    const auto panelRaised = themeColor(themePalette_, "panel-raised", palette().color(QPalette::Base));
    const auto border = themeColor(themePalette_, "border", palette().color(QPalette::Mid));
    const auto text = themeColor(themePalette_, "text", palette().color(QPalette::WindowText));
    const auto muted = themeColor(themePalette_, "text-muted", palette().color(QPalette::Mid));
    const auto accent = resolvedAccentColor();

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (showSurface_) {
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(panelBackground);
        painter.drawRoundedRect(bounds, 4.0, 4.0);
        const QRectF labelBand(bounds.left() + 1.0,
                               bounds.top() + 1.0,
                               std::max(1.0, bounds.width() - 2.0),
                               std::min(24.0, std::max(1.0, bounds.height() - 2.0)));
        painter.fillRect(labelBand, panelRaised);
        painter.fillRect(QRectF(labelBand.left(), labelBand.bottom() - 1.0, labelBand.width(), 1.0), accent);
        painter.fillRect(QRectF(bounds.left() + 1.0, bounds.top() + 6.0, 3.0, std::max(1.0, bounds.height() - 12.0)), accent);
    }

    const QFontMetrics metrics(painter.font());
    const auto content = showSurface_ ? bounds.adjusted(12.0, 8.0, -10.0, -8.0) : bounds.adjusted(0.0, 2.0, 0.0, -2.0);
    QFont valueFont = painter.font();
    valueFont.setPointSizeF(std::max(11.0, valueFont.pointSizeF() + 5.0));
    valueFont.setBold(true);
    const QFontMetrics valueMetrics(valueFont);
    QFont unitFont = valueFont;
    unitFont.setPointSizeF(std::max(8.0, unitFont.pointSizeF() - 3.0));
    unitFont.setBold(false);
    const QFontMetrics unitMetrics(unitFont);
    const auto groupHeight = metrics.height() + 3.0 + valueMetrics.height();
    const auto groupTop = content.top() + std::max(0.0, (content.height() - groupHeight) / 2.0);
    const QRectF labelRect(content.left(), groupTop, content.width(), metrics.height());
    const QRectF valueRect(content.left(), groupTop + metrics.height() + 3.0, content.width(), valueMetrics.height());

    painter.setPen(muted);
    painter.drawText(labelRect,
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(label_, Qt::ElideRight, static_cast<int>(content.width())));

    painter.setFont(valueFont);
    painter.setPen(showSurface_ ? text : accent);
    painter.drawText(valueRect,
                     Qt::AlignLeft | Qt::AlignVCenter,
                     displayText());

    if (!unit_.isEmpty()) {
        painter.setFont(unitFont);
        painter.setPen(muted);
        painter.drawText(QRectF(valueRect.left(),
                                valueRect.center().y() - unitMetrics.height() / 2.0,
                                valueRect.width(),
                                unitMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         metrics.elidedText(unit_, Qt::ElideLeft, static_cast<int>(content.width() / 2.0)));
    }

}

QColor MetricReadout::resolvedAccentColor() const
{
    if (accentColor_.isValid())
        return accentColor_;
    return themeColor(themePalette_, "accent", palette().color(QPalette::Highlight));
}

}
