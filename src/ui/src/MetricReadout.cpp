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
    value_ = value;
    update();
}

void MetricReadout::clearValue()
{
    if (!value_.has_value())
        return;
    value_.reset();
    update();
}

std::optional<double> MetricReadout::value() const noexcept
{
    return value_;
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
    return QSize(148, 78);
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

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(panelBackground);
    painter.drawRoundedRect(bounds, 4.0, 4.0);
    painter.fillRect(QRectF(bounds.left() + 1.0, bounds.top() + 8.0, 3.0, std::max(1.0, bounds.height() - 16.0)), resolvedAccentColor());

    const QFontMetrics metrics(painter.font());
    const auto content = bounds.adjusted(12.0, 8.0, -10.0, -8.0);
    painter.setPen(muted);
    painter.drawText(QRectF(content.left(), content.top(), content.width(), metrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(label_, Qt::ElideRight, static_cast<int>(content.width())));

    QFont valueFont = painter.font();
    valueFont.setPointSizeF(std::max(11.0, valueFont.pointSizeF() + 5.0));
    valueFont.setBold(true);
    painter.setFont(valueFont);
    const QFontMetrics valueMetrics(valueFont);
    painter.setPen(text);
    painter.drawText(QRectF(content.left(), content.bottom() - valueMetrics.height(), content.width(), valueMetrics.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     displayText());

    if (!unit_.isEmpty()) {
        QFont unitFont = painter.font();
        unitFont.setPointSizeF(std::max(8.0, unitFont.pointSizeF() - 3.0));
        unitFont.setBold(false);
        painter.setFont(unitFont);
        painter.setPen(muted);
        const QFontMetrics unitMetrics(unitFont);
        painter.drawText(QRectF(content.left(), content.bottom() - unitMetrics.height(), content.width(), unitMetrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         metrics.elidedText(unit_, Qt::ElideLeft, static_cast<int>(content.width() / 2.0)));
    }

    Q_UNUSED(panelRaised)
}

QColor MetricReadout::resolvedAccentColor() const
{
    if (accentColor_.isValid())
        return accentColor_;
    return themeColor(themePalette_, "accent", palette().color(QPalette::Highlight));
}

}