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
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::UI {

namespace {

QColor themeColor(const ThemePalette& palette, const char* key, const QColor& fallback)
{
    const auto color = palette.color(QString::fromLatin1(key));
    return color.isValid() ? color : fallback;
}

QString formatAxisValue(double value)
{
    return QString::number(value, 'f', std::abs(value - std::round(value)) < 0.01 ? 0 : 1);
}

}

HistoryChartWidget::HistoryChartWidget(QWidget* parent)
    : PaintableWidget(parent)
{
    setMinimumSize(320, 190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void HistoryChartWidget::setTimeRange(std::int64_t startMilliseconds, std::int64_t endMilliseconds)
{
    if (endMilliseconds <= startMilliseconds)
        return;
    startTimeMilliseconds_ = startMilliseconds;
    endTimeMilliseconds_ = endMilliseconds;
    update();
}

std::int64_t HistoryChartWidget::startTimeMilliseconds() const noexcept
{
    return startTimeMilliseconds_;
}

std::int64_t HistoryChartWidget::endTimeMilliseconds() const noexcept
{
    return endTimeMilliseconds_;
}

void HistoryChartWidget::setValueRange(double minimum, double maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
        return;
    minimumValue_ = minimum;
    maximumValue_ = maximum;
    update();
}

double HistoryChartWidget::minimumValue() const noexcept
{
    return minimumValue_;
}

double HistoryChartWidget::maximumValue() const noexcept
{
    return maximumValue_;
}

void HistoryChartWidget::setVerticalAxisLabel(const QString& label)
{
    if (verticalAxisLabel_ == label)
        return;
    verticalAxisLabel_ = label;
    update();
}

QString HistoryChartWidget::verticalAxisLabel() const
{
    return verticalAxisLabel_;
}

void HistoryChartWidget::setHorizontalAxisLabel(const QString& label)
{
    if (horizontalAxisLabel_ == label)
        return;
    horizontalAxisLabel_ = label;
    update();
}

QString HistoryChartWidget::horizontalAxisLabel() const
{
    return horizontalAxisLabel_;
}

void HistoryChartWidget::setSeries(std::vector<HistoryChartSeries> series)
{
    for (auto& item : series) {
        std::sort(item.points.begin(), item.points.end(), [](const HistoryChartPoint& left, const HistoryChartPoint& right) {
            return left.timestampMilliseconds < right.timestampMilliseconds;
        });
    }
    series_ = std::move(series);
    update();
}

const std::vector<HistoryChartSeries>& HistoryChartWidget::series() const noexcept
{
    return series_;
}

void HistoryChartWidget::setReferenceLines(std::vector<HistoryChartReferenceLine> lines)
{
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const HistoryChartReferenceLine& line) {
        return !std::isfinite(line.value);
    }), lines.end());
    referenceLines_ = std::move(lines);
    update();
}

const std::vector<HistoryChartReferenceLine>& HistoryChartWidget::referenceLines() const noexcept
{
    return referenceLines_;
}

void HistoryChartWidget::setThemePalette(const ThemePalette& palette)
{
    if (!palette.isValid())
        return;
    themePalette_ = palette;
    update();
}

ThemePalette HistoryChartWidget::themePalette() const
{
    return themePalette_;
}

QSize HistoryChartWidget::sizeHint() const
{
    return QSize(480, 260);
}

void HistoryChartWidget::paintContent(QPainter& painter)
{
    const auto panelBackground = themeColor(themePalette_, "panel-bg", palette().color(QPalette::Window));
    const auto panelRaised = themeColor(themePalette_, "panel-raised", palette().color(QPalette::Base));
    const auto border = themeColor(themePalette_, "border", palette().color(QPalette::Mid));
    const auto text = themeColor(themePalette_, "text", palette().color(QPalette::WindowText));
    const auto muted = themeColor(themePalette_, "text-muted", palette().color(QPalette::Mid));
    const auto accent = themeColor(themePalette_, "accent", palette().color(QPalette::Highlight));
    const auto accentAlt = themeColor(themePalette_, "accent-alt", palette().color(QPalette::Link));
    const auto success = themeColor(themePalette_, "success", palette().color(QPalette::Highlight));
    const auto danger = themeColor(themePalette_, "danger", palette().color(QPalette::BrightText));

    painter.fillRect(rect(), panelBackground);
    const QFontMetrics metrics(painter.font());
    const auto leftMargin = std::max(42, metrics.horizontalAdvance(formatAxisValue(minimumValue_)) + 16);
    const auto chartRect = QRectF(rect()).adjusted(leftMargin, metrics.height() + 9, -12, -(metrics.height() * 2 + 13));
    if (chartRect.width() <= 8.0 || chartRect.height() <= 8.0)
        return;

    painter.setPen(QPen(border, 1.0));
    painter.setBrush(panelRaised);
    painter.drawRect(chartRect);
    constexpr int horizontalGridLines = 5;
    constexpr int verticalGridLines = 6;
    for (int index = 0; index <= horizontalGridLines; ++index) {
        const auto fraction = static_cast<double>(index) / horizontalGridLines;
        const auto y = chartRect.bottom() - fraction * chartRect.height();
        const auto axisValue = minimumValue_ + fraction * (maximumValue_ - minimumValue_);
        painter.setPen(QPen(border, 1.0, Qt::DotLine));
        painter.drawLine(QPointF(chartRect.left(), y), QPointF(chartRect.right(), y));
        painter.setPen(muted);
        painter.drawText(QRectF(0.0, y - metrics.height() / 2.0, leftMargin - 6.0, metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         formatAxisValue(axisValue));
    }
    for (int index = 0; index <= verticalGridLines; ++index) {
        const auto fraction = static_cast<double>(index) / verticalGridLines;
        const auto x = chartRect.left() + fraction * chartRect.width();
        painter.setPen(QPen(border, 1.0, Qt::DotLine));
        painter.drawLine(QPointF(x, chartRect.top()), QPointF(x, chartRect.bottom()));
    }

    painter.setPen(muted);
    painter.drawText(QRectF(chartRect.left(), 1.0, chartRect.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter, verticalAxisLabel_);
    painter.drawText(QRectF(chartRect.left(), chartRect.bottom() + 5.0, chartRect.width(), metrics.height()), Qt::AlignRight | Qt::AlignVCenter, horizontalAxisLabel_);

    painter.save();
    painter.setClipRect(chartRect);
    for (const auto& line : referenceLines_) {
        if (!line.visible || line.value < minimumValue_ || line.value > maximumValue_)
            continue;
        const auto position = pointPosition(chartRect, startTimeMilliseconds_, line.value);
        painter.setPen(QPen(line.color.isValid() ? line.color : accentAlt, 1.25, line.penStyle));
        painter.drawLine(QPointF(chartRect.left(), position.y()), QPointF(chartRect.right(), position.y()));
    }

    bool hasVisiblePoint = false;
    for (std::size_t seriesIndex = 0; seriesIndex < series_.size(); ++seriesIndex) {
        const auto& item = series_[seriesIndex];
        if (!item.visible)
            continue;
        QPainterPath path;
        bool hasPreviousPoint = false;
        for (const auto& point : item.points) {
            if (!point.value.has_value() || !std::isfinite(*point.value)
                || point.timestampMilliseconds < startTimeMilliseconds_
                || point.timestampMilliseconds > endTimeMilliseconds_) {
                hasPreviousPoint = false;
                continue;
            }
            const auto position = pointPosition(chartRect, point.timestampMilliseconds, *point.value);
            if (!hasPreviousPoint)
                path.moveTo(position);
            else
                path.lineTo(position);
            hasPreviousPoint = true;
            hasVisiblePoint = true;
        }
        if (!path.isEmpty()) {
            painter.setPen(QPen(seriesColor(seriesIndex, item), 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        }
    }
    painter.restore();

    if (!hasVisiblePoint) {
        painter.setPen(muted);
        painter.drawText(chartRect, Qt::AlignCenter, QStringLiteral("--"));
    }

    Q_UNUSED(text)
    Q_UNUSED(success)
    Q_UNUSED(danger)
}

QColor HistoryChartWidget::seriesColor(std::size_t index, const HistoryChartSeries& series) const
{
    if (series.color.isValid())
        return series.color;
    const std::array colors {
        themeColor(themePalette_, "accent", palette().color(QPalette::Highlight)),
        themeColor(themePalette_, "accent-alt", palette().color(QPalette::Link)),
        themeColor(themePalette_, "success", palette().color(QPalette::Highlight)),
        themeColor(themePalette_, "danger", palette().color(QPalette::BrightText))
    };
    return colors[index % colors.size()];
}

QPointF HistoryChartWidget::pointPosition(const QRectF& chartRect, std::int64_t timestampMilliseconds, double value) const
{
    const auto timeSpan = static_cast<double>(endTimeMilliseconds_ - startTimeMilliseconds_);
    const auto valueSpan = maximumValue_ - minimumValue_;
    const auto xFraction = timeSpan > 0.0
        ? std::clamp(static_cast<double>(timestampMilliseconds - startTimeMilliseconds_) / timeSpan, 0.0, 1.0)
        : 0.0;
    const auto yFraction = valueSpan > 0.0
        ? std::clamp((value - minimumValue_) / valueSpan, 0.0, 1.0)
        : 0.0;
    return QPointF(chartRect.left() + xFraction * chartRect.width(), chartRect.bottom() - yFraction * chartRect.height());
}

}