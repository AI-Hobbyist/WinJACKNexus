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

#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

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

QString formatTimeAxisLabel(std::int64_t timestampMilliseconds,
                            const std::optional<std::int64_t>& displayOffsetMilliseconds)
{
    if (displayOffsetMilliseconds.has_value())
        return QDateTime::fromMSecsSinceEpoch(timestampMilliseconds + *displayOffsetMilliseconds).toString(QStringLiteral("HH:mm:ss"));

    const auto clampedTimestamp = std::max<std::int64_t>(0, timestampMilliseconds);
    const auto totalSeconds = clampedTimestamp / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QString formatHoverTime(std::int64_t timestampMilliseconds,
                        const std::optional<std::int64_t>& displayOffsetMilliseconds)
{
    if (displayOffsetMilliseconds.has_value())
        return QDateTime::fromMSecsSinceEpoch(timestampMilliseconds + *displayOffsetMilliseconds).toString(QStringLiteral("HH:mm:ss.zzz"));

    const auto clampedTimestamp = std::max<std::int64_t>(0, timestampMilliseconds);
    const auto totalSeconds = clampedTimestamp / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg((clampedTimestamp % 1000) / 100);
}

}

HistoryChartWidget::HistoryChartWidget(QWidget* parent)
    : PaintableWidget(parent)
{
    setMinimumSize(320, 190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void HistoryChartWidget::setTimeRange(std::int64_t startMilliseconds, std::int64_t endMilliseconds)
{
    if (endMilliseconds <= startMilliseconds
        || (startTimeMilliseconds_ == startMilliseconds && endTimeMilliseconds_ == endMilliseconds))
        return;
    startTimeMilliseconds_ = startMilliseconds;
    endTimeMilliseconds_ = endMilliseconds;
    if (hoverPosition_.has_value())
        updateHover(*hoverPosition_);
    else
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

void HistoryChartWidget::setTimeDisplayOffsetMilliseconds(std::optional<std::int64_t> offsetMilliseconds)
{
    if (timeDisplayOffsetMilliseconds_ == offsetMilliseconds)
        return;
    timeDisplayOffsetMilliseconds_ = offsetMilliseconds;
    update();
}

std::optional<std::int64_t> HistoryChartWidget::timeDisplayOffsetMilliseconds() const noexcept
{
    return timeDisplayOffsetMilliseconds_;
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

void HistoryChartWidget::setRightValueRange(double minimum, double maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
        return;
    rightMinimumValue_ = minimum;
    rightMaximumValue_ = maximum;
    update();
}

double HistoryChartWidget::rightMinimumValue() const noexcept
{
    return rightMinimumValue_;
}

double HistoryChartWidget::rightMaximumValue() const noexcept
{
    return rightMaximumValue_;
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

void HistoryChartWidget::setRightVerticalAxisLabel(const QString& label)
{
    if (rightVerticalAxisLabel_ == label)
        return;
    rightVerticalAxisLabel_ = label;
    update();
}

QString HistoryChartWidget::rightVerticalAxisLabel() const
{
    return rightVerticalAxisLabel_;
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

void HistoryChartWidget::setTimeCursorMilliseconds(std::optional<std::int64_t> timestampMilliseconds)
{
    if (timeCursorMilliseconds_ == timestampMilliseconds)
        return;
    timeCursorMilliseconds_ = timestampMilliseconds;
    update();
}

std::optional<std::int64_t> HistoryChartWidget::timeCursorMilliseconds() const noexcept
{
    return timeCursorMilliseconds_;
}

std::optional<std::int64_t> HistoryChartWidget::hoverTimeMilliseconds() const noexcept
{
    return hoverTimeMilliseconds_;
}

const std::vector<HistoryChartHoverEntry>& HistoryChartWidget::hoverEntries() const noexcept
{
    return hoverEntries_;
}

void HistoryChartWidget::setSeries(std::vector<HistoryChartSeries> series)
{
    for (auto& item : series) {
        if (item.preservePointOrder)
            continue;
        std::sort(item.points.begin(), item.points.end(), [](const HistoryChartPoint& left, const HistoryChartPoint& right) {
            return left.timestampMilliseconds < right.timestampMilliseconds;
        });
    }
    series_ = std::move(series);
    if (hoverPosition_.has_value())
        updateHover(*hoverPosition_);
    else
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

QRectF HistoryChartWidget::chartRect(const QFontMetrics& metrics) const
{
    const auto leftMargin = std::max(42, metrics.horizontalAdvance(formatAxisValue(minimumValue_)) + 16);
    const auto rightMargin = hasRightAxis()
        ? std::max(42, metrics.horizontalAdvance(formatAxisValue(rightMaximumValue_)) + 16)
        : 12;
    return QRectF(rect()).adjusted(leftMargin, metrics.height() + 9, -rightMargin, -(metrics.height() * 2 + 13));
}

bool HistoryChartWidget::hasRightAxis() const noexcept
{
    return !rightVerticalAxisLabel_.isEmpty()
        || std::any_of(series_.cbegin(), series_.cend(), [](const HistoryChartSeries& series) {
               return series.axis == HistoryChartAxis::right;
           })
        || std::any_of(referenceLines_.cbegin(), referenceLines_.cend(), [](const HistoryChartReferenceLine& line) {
               return line.axis == HistoryChartAxis::right;
           });
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
    const auto showRightAxis = this->hasRightAxis();
    const auto chartRect = this->chartRect(metrics);
    if (chartRect.width() <= 8.0 || chartRect.height() <= 8.0)
        return;
    const auto leftMargin = chartRect.left();
    const auto rightMargin = rect().right() - chartRect.right();

    painter.setPen(QPen(border, 1.0));
    painter.setBrush(panelRaised);
    painter.drawRect(chartRect);
    constexpr int horizontalGridLines = 7;
    constexpr int verticalGridLines = 6;
    const auto timeSpan = endTimeMilliseconds_ - startTimeMilliseconds_;
    const auto timeLabelWidth = std::max<qreal>(1.0,
                                                 metrics.horizontalAdvance(formatTimeAxisLabel(endTimeMilliseconds_, timeDisplayOffsetMilliseconds_)) + 6.0);
    const auto timeLabelInterval = std::max(1,
                                            static_cast<int>(std::ceil(timeLabelWidth * 1.15
                                                                       / std::max<qreal>(1.0, chartRect.width() / verticalGridLines))));
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
        if (showRightAxis) {
            const auto rightAxisValue = rightMinimumValue_ + fraction * (rightMaximumValue_ - rightMinimumValue_);
            painter.drawText(QRectF(chartRect.right() + 6.0, y - metrics.height() / 2.0, rightMargin - 6.0, metrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             formatAxisValue(rightAxisValue));
        }
    }
    for (int index = 0; index <= verticalGridLines; ++index) {
        const auto fraction = static_cast<double>(index) / verticalGridLines;
        const auto x = chartRect.left() + fraction * chartRect.width();
        painter.setPen(QPen(border, 1.0, Qt::DotLine));
        painter.drawLine(QPointF(x, chartRect.top()), QPointF(x, chartRect.bottom()));
        if (index % timeLabelInterval == 0 || index == verticalGridLines) {
            const auto timestamp = startTimeMilliseconds_ + static_cast<std::int64_t>(std::llround(fraction * timeSpan));
            const auto label = formatTimeAxisLabel(timestamp, timeDisplayOffsetMilliseconds_);
            const auto labelWidth = std::max<qreal>(timeLabelWidth, metrics.horizontalAdvance(label) + 6.0);
            const auto labelLeft = std::clamp(x - labelWidth / 2.0,
                                              0.0,
                                              std::max<qreal>(0.0, rect().width() - labelWidth));
            painter.setPen(muted);
            painter.drawText(QRectF(labelLeft, chartRect.bottom() + 5.0, labelWidth, metrics.height()),
                             Qt::AlignHCenter | Qt::AlignVCenter,
                             label);
        }
    }

    painter.setPen(muted);
    painter.drawText(QRectF(chartRect.left(), 1.0, chartRect.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter, verticalAxisLabel_);
    if (showRightAxis) {
        painter.drawText(QRectF(chartRect.left(), 1.0, chartRect.width(), metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         rightVerticalAxisLabel_);
    }
    painter.drawText(QRectF(chartRect.left(), chartRect.bottom() + metrics.height() + 5.0, chartRect.width(), metrics.height()),
                     Qt::AlignRight | Qt::AlignVCenter,
                     horizontalAxisLabel_);

    const auto hasHover = hoverTimeMilliseconds_.has_value()
        && !hoverEntries_.empty()
        && *hoverTimeMilliseconds_ >= startTimeMilliseconds_
        && *hoverTimeMilliseconds_ <= endTimeMilliseconds_;
    painter.save();
    painter.setClipRect(chartRect);
    for (const auto& line : referenceLines_) {
        const auto minimum = minimumValueForAxis(line.axis);
        const auto maximum = maximumValueForAxis(line.axis);
        if (!line.visible || line.value < minimum || line.value > maximum)
            continue;
        const auto position = pointPosition(chartRect, startTimeMilliseconds_, line.value, line.axis);
        painter.setPen(QPen(line.color.isValid() ? line.color : accentAlt, 1.25, line.penStyle));
        painter.drawLine(QPointF(chartRect.left(), position.y()), QPointF(chartRect.right(), position.y()));
    }

    bool hasVisiblePoint = false;
    painter.setBrush(Qt::NoBrush);
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
            const auto position = pointPosition(chartRect, point.timestampMilliseconds, *point.value, item.axis);
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
    if (timeCursorMilliseconds_.has_value()
        && *timeCursorMilliseconds_ >= startTimeMilliseconds_
        && *timeCursorMilliseconds_ <= endTimeMilliseconds_) {
        const auto cursorPosition = pointPosition(chartRect,
                                                   *timeCursorMilliseconds_,
                                                   minimumValue_,
                                                   HistoryChartAxis::left);
        painter.setPen(QPen(accent, 1.25, Qt::DashLine));
        painter.drawLine(QPointF(cursorPosition.x(), chartRect.top()), QPointF(cursorPosition.x(), chartRect.bottom()));
    }
    if (hasHover) {
        const auto hoverPosition = pointPosition(chartRect,
                                                 *hoverTimeMilliseconds_,
                                                 minimumValue_,
                                                 HistoryChartAxis::left);
        QColor hoverColor = accent;
        hoverColor.setAlpha(220);
        painter.setPen(QPen(hoverColor, 1.25, Qt::DashLine));
        painter.drawLine(QPointF(hoverPosition.x(), chartRect.top()), QPointF(hoverPosition.x(), chartRect.bottom()));
        for (const auto& entry : hoverEntries_) {
            if (!entry.value.has_value())
                continue;
            const auto point = pointPosition(chartRect, *hoverTimeMilliseconds_, *entry.value, entry.axis);
            painter.setPen(QPen(panelRaised, 1.5));
            painter.setBrush(entry.color);
            painter.drawEllipse(point, 3.5, 3.5);
        }
    }
    painter.restore();

    if (hasHover) {
        const auto hoverPosition = pointPosition(chartRect,
                                                 *hoverTimeMilliseconds_,
                                                 minimumValue_,
                                                 HistoryChartAxis::left);
        const auto columns = hoverEntries_.size() > 4 ? 2 : 1;
        const auto rows = static_cast<int>((hoverEntries_.size() + columns - 1) / columns);
        const auto rowHeight = std::max(15, metrics.height() + 3);
        const auto headerHeight = metrics.height() + 7;
        constexpr qreal padding = 8.0;
        constexpr qreal columnGap = 8.0;
        qreal preferredColumnWidth = 120.0;
        for (const auto& entry : hoverEntries_) {
            const auto valueText = entry.value.has_value()
                ? QStringLiteral("%1 %2").arg(formatAxisValue(*entry.value), entry.unit)
                : QStringLiteral("--");
            preferredColumnWidth = std::max<qreal>(preferredColumnWidth,
                                                    metrics.horizontalAdvance(entry.label + QStringLiteral("  ") + valueText) + 16.0);
        }
        const auto availableTooltipWidth = std::max<qreal>(1.0, chartRect.width() - 12.0);
        const auto tooltipWidth = std::min(availableTooltipWidth,
                                           std::max<qreal>(150.0,
                                                           preferredColumnWidth * columns + columnGap * (columns - 1) + padding * 2.0));
        const auto tooltipHeight = padding * 2.0 + headerHeight + rows * rowHeight;
        const auto minimumX = chartRect.left() + 4.0;
        const auto maximumX = std::max(minimumX, chartRect.right() - tooltipWidth - 4.0);
        auto tooltipX = hoverPosition.x() + 12.0;
        if (tooltipX + tooltipWidth > chartRect.right() - 4.0)
            tooltipX = hoverPosition.x() - tooltipWidth - 12.0;
        tooltipX = std::clamp(tooltipX, minimumX, maximumX);
        const auto tooltipY = std::clamp(chartRect.top() + 8.0,
                                         chartRect.top() + 4.0,
                                         std::max(chartRect.top() + 4.0, chartRect.bottom() - tooltipHeight - 4.0));
        const QRectF tooltipRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight);
        QColor tooltipBackground = panelBackground;
        tooltipBackground.setAlpha(238);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(tooltipBackground);
        painter.drawRoundedRect(tooltipRect, 4.0, 4.0);

        QFont headerFont = painter.font();
        headerFont.setBold(true);
        painter.setFont(headerFont);
        painter.setPen(text);
        painter.drawText(QRectF(tooltipRect.left() + padding,
                                tooltipRect.top() + padding,
                                tooltipRect.width() - padding * 2.0,
                                headerHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("时间 %1").arg(formatHoverTime(*hoverTimeMilliseconds_, timeDisplayOffsetMilliseconds_)));
        painter.setFont(font());
        const auto innerWidth = tooltipRect.width() - padding * 2.0 - columnGap * (columns - 1);
        const auto columnWidth = std::max<qreal>(1.0, innerWidth / columns);
        for (int index = 0; index < static_cast<int>(hoverEntries_.size()); ++index) {
            const auto& entry = hoverEntries_[static_cast<std::size_t>(index)];
            const auto column = index / rows;
            const auto row = index % rows;
            const QRectF rowRect(tooltipRect.left() + padding + column * (columnWidth + columnGap),
                                 tooltipRect.top() + padding + headerHeight + row * rowHeight,
                                 columnWidth,
                                 rowHeight);
            const auto valueText = entry.value.has_value()
                ? QStringLiteral("%1 %2").arg(formatAxisValue(*entry.value), entry.unit)
                : QStringLiteral("--");
            const auto valueWidth = metrics.horizontalAdvance(valueText);
            painter.fillRect(QRectF(rowRect.left(), rowRect.top() + 3.0, 3.0, std::max<qreal>(1.0, rowRect.height() - 6.0)), entry.color);
            painter.setPen(muted);
            painter.drawText(QRectF(rowRect.left() + 8.0,
                                    rowRect.top(),
                                    std::max<qreal>(1.0, rowRect.width() - valueWidth - 14.0),
                                    rowRect.height()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(entry.label,
                                                 Qt::ElideRight,
                                                 static_cast<int>(std::max<qreal>(1.0, rowRect.width() - valueWidth - 14.0))));
            painter.setPen(text);
            painter.drawText(QRectF(rowRect.right() - valueWidth,
                                    rowRect.top(),
                                    valueWidth,
                                    rowRect.height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             valueText);
        }
    }

    if (!hasVisiblePoint) {
        painter.setPen(muted);
        painter.drawText(chartRect, Qt::AlignCenter, QStringLiteral("--"));
    }

    Q_UNUSED(text)
    Q_UNUSED(success)
    Q_UNUSED(danger)
}

void HistoryChartWidget::mouseMoveEvent(QMouseEvent* event)
{
    updateHover(event->position());
    QWidget::mouseMoveEvent(event);
}

void HistoryChartWidget::leaveEvent(QEvent* event)
{
    clearHover();
    QWidget::leaveEvent(event);
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

QPointF HistoryChartWidget::pointPosition(const QRectF& chartRect,
                                          std::int64_t timestampMilliseconds,
                                          double value,
                                          HistoryChartAxis axis) const
{
    const auto timeSpan = static_cast<double>(endTimeMilliseconds_ - startTimeMilliseconds_);
    const auto minimum = minimumValueForAxis(axis);
    const auto maximum = maximumValueForAxis(axis);
    const auto valueSpan = maximum - minimum;
    const auto xFraction = timeSpan > 0.0
        ? std::clamp(static_cast<double>(timestampMilliseconds - startTimeMilliseconds_) / timeSpan, 0.0, 1.0)
        : 0.0;
    const auto yFraction = valueSpan > 0.0
        ? std::clamp((value - minimum) / valueSpan, 0.0, 1.0)
        : 0.0;
    return QPointF(chartRect.left() + xFraction * chartRect.width(), chartRect.bottom() - yFraction * chartRect.height());
}

std::optional<std::int64_t> HistoryChartWidget::nearestTimestamp(std::int64_t timestampMilliseconds) const
{
    std::optional<std::int64_t> nearest;
    auto nearestDistance = std::numeric_limits<std::int64_t>::max();
    for (const auto& item : series_) {
        if (!item.visible)
            continue;
        for (const auto& point : item.points) {
            if (!point.value.has_value() || !std::isfinite(*point.value)
                || point.timestampMilliseconds < startTimeMilliseconds_
                || point.timestampMilliseconds > endTimeMilliseconds_)
                continue;
            const auto distance = point.timestampMilliseconds >= timestampMilliseconds
                ? point.timestampMilliseconds - timestampMilliseconds
                : timestampMilliseconds - point.timestampMilliseconds;
            if (!nearest.has_value() || distance <= nearestDistance) {
                nearest = point.timestampMilliseconds;
                nearestDistance = distance;
            }
        }
    }
    return nearest;
}

std::optional<double> HistoryChartWidget::valueAtTimestamp(const HistoryChartSeries& item,
                                                            std::int64_t timestampMilliseconds) const
{
    std::optional<HistoryChartPoint> previous;
    std::optional<double> value;
    for (const auto& point : item.points) {
        if (point.timestampMilliseconds < startTimeMilliseconds_ || point.timestampMilliseconds > endTimeMilliseconds_)
            continue;
        if (!point.value.has_value() || !std::isfinite(*point.value)) {
            previous.reset();
            continue;
        }
        if (point.timestampMilliseconds == timestampMilliseconds)
            value = point.value;
        else if (previous.has_value()
                 && previous->timestampMilliseconds < timestampMilliseconds
                 && timestampMilliseconds < point.timestampMilliseconds) {
            const auto span = static_cast<double>(point.timestampMilliseconds - previous->timestampMilliseconds);
            const auto fraction = static_cast<double>(timestampMilliseconds - previous->timestampMilliseconds) / span;
            value = *previous->value + (*point.value - *previous->value) * fraction;
        }
        previous = point;
    }
    return value;
}

void HistoryChartWidget::updateHover(const QPointF& position)
{
    const auto currentChartRect = chartRect(QFontMetrics(font()));
    if (currentChartRect.width() <= 8.0 || currentChartRect.height() <= 8.0 || !currentChartRect.contains(position)) {
        clearHover();
        return;
    }
    const auto timeSpan = endTimeMilliseconds_ - startTimeMilliseconds_;
    if (timeSpan <= 0) {
        clearHover();
        return;
    }
    const auto fraction = std::clamp((position.x() - currentChartRect.left()) / currentChartRect.width(), 0.0, 1.0);
    const auto timestamp = startTimeMilliseconds_ + static_cast<std::int64_t>(std::llround(fraction * timeSpan));
    const auto nearest = nearestTimestamp(timestamp);
    if (!nearest.has_value()) {
        clearHover();
        return;
    }

    std::vector<HistoryChartHoverEntry> entries;
    entries.reserve(series_.size());
    for (std::size_t index = 0; index < series_.size(); ++index) {
        const auto& item = series_[index];
        if (!item.visible)
            continue;
        entries.push_back({ item.id, item.label, QString {}, seriesColor(index, item), valueAtTimestamp(item, *nearest), item.axis });
    }
    hoverPosition_ = position;
    hoverTimeMilliseconds_ = nearest;
    hoverEntries_ = std::move(entries);
    setCursor(Qt::CrossCursor);
    update();
}

void HistoryChartWidget::clearHover()
{
    if (!hoverPosition_.has_value() && !hoverTimeMilliseconds_.has_value() && hoverEntries_.empty())
        return;
    hoverPosition_.reset();
    hoverTimeMilliseconds_.reset();
    hoverEntries_.clear();
    unsetCursor();
    update();
}

double HistoryChartWidget::minimumValueForAxis(HistoryChartAxis axis) const noexcept
{
    return axis == HistoryChartAxis::right ? rightMinimumValue_ : minimumValue_;
}

double HistoryChartWidget::maximumValueForAxis(HistoryChartAxis axis) const noexcept
{
    return axis == HistoryChartAxis::right ? rightMaximumValue_ : maximumValue_;
}

}