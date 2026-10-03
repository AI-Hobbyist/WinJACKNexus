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
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
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

constexpr int entryMinimumWidth = 112;
constexpr int entryHeight = 78;
constexpr int margin = 6;

}

MetricStatisticsWidget::MetricStatisticsWidget(QWidget* parent)
    : QWidget(parent)
{
    if (auto* manager = I18nManager::installed())
        manager->bind(this, [this] { update(); });
    setMinimumSize(entryMinimumWidth + margin * 2, entryHeight + margin * 2);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void MetricStatisticsWidget::setEntries(std::vector<MetricStatisticEntry> entries)
{
    for (auto& entry : entries) {
        const auto normalize = [](std::optional<double>& value) {
            if (value.has_value() && !std::isfinite(*value))
                value.reset();
        };
        normalize(entry.currentValue);
        normalize(entry.minimumValue);
        normalize(entry.maximumValue);
    }
    entries_ = std::move(entries);
    updateGeometry();
    update();
}

const std::vector<MetricStatisticEntry>& MetricStatisticsWidget::entries() const noexcept
{
    return entries_;
}

void MetricStatisticsWidget::setPrecision(int precision)
{
    const auto normalized = std::clamp(precision, 0, 4);
    if (precision_ == normalized)
        return;
    precision_ = normalized;
    update();
}

int MetricStatisticsWidget::precision() const noexcept
{
    return precision_;
}

void MetricStatisticsWidget::setThemePalette(const ThemePalette& palette)
{
    if (!palette.isValid())
        return;
    themePalette_ = palette;
    update();
}

ThemePalette MetricStatisticsWidget::themePalette() const
{
    return themePalette_;
}

QSize MetricStatisticsWidget::sizeHint() const
{
    const auto columns = std::max(1, static_cast<int>(entries_.size()));
    const auto rows = std::max(1, (static_cast<int>(entries_.size()) + columns - 1) / columns);
    return QSize(columns * entryMinimumWidth + margin * 2, rows * entryHeight + margin * 2);
}

void MetricStatisticsWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto panelBackground = themeColor(themePalette_, "panel-bg", palette().color(QPalette::Window));
    const auto panelRaised = themeColor(themePalette_, "panel-raised", palette().color(QPalette::Base));
    const auto border = themeColor(themePalette_, "border", palette().color(QPalette::Mid));
    const auto text = themeColor(themePalette_, "text", palette().color(QPalette::WindowText));
    const auto muted = themeColor(themePalette_, "text-muted", palette().color(QPalette::Mid));
    painter.fillRect(rect(), panelBackground);

    if (entries_.empty()) {
        painter.setPen(muted);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("--"));
        return;
    }

    const auto labelFont = painter.font();
    QFont valueFont = labelFont;
    valueFont.setPointSize(std::max(12, labelFont.pointSize() + 6));
    valueFont.setBold(true);
    const QFontMetrics labelMetrics(labelFont);
    const QFontMetrics valueMetrics(valueFont);
    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        const auto& entry = entries_[static_cast<std::size_t>(index)];
        const auto bounds = entryRect(index).adjusted(1, 1, -1, -1);
        const auto accentColor = entryColor(static_cast<std::size_t>(index), entry);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(panelRaised);
        painter.drawRect(bounds);
        painter.fillRect(QRect(bounds.left(), bounds.top(), 3, bounds.height()), accentColor);

        const auto contentBounds = bounds.adjusted(10, 6, -8, -6);
        painter.setFont(labelFont);
        painter.setPen(muted);
        painter.drawText(QRect(contentBounds.left(), contentBounds.top(), contentBounds.width(), labelMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         labelMetrics.elidedText(entry.label, Qt::ElideRight, contentBounds.width()));

        const auto currentText = displayValue(entry.currentValue);
        painter.setFont(valueFont);
        painter.setPen(text);
        const auto unitWidth = entry.unit.isEmpty() ? 0 : labelMetrics.horizontalAdvance(entry.unit) + 5;
        painter.drawText(QRect(contentBounds.left(), contentBounds.top() + labelMetrics.height() + 1,
                               std::max(0, contentBounds.width() - unitWidth), valueMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         valueMetrics.elidedText(currentText, Qt::ElideRight, std::max(0, contentBounds.width() - unitWidth)));
        if (!entry.unit.isEmpty()) {
            painter.setFont(labelFont);
            painter.setPen(muted);
            painter.drawText(QRect(contentBounds.right() - unitWidth + 5, contentBounds.top() + labelMetrics.height() + 1,
                                   unitWidth - 5, valueMetrics.height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             entry.unit);
        }

        painter.setFont(labelFont);
        painter.setPen(muted);
        const auto detail = localizedText("ui", "statistics.extrema", "Min: %1    Max: %2").arg(displayValue(entry.minimumValue), displayValue(entry.maximumValue));
        painter.drawText(QRect(contentBounds.left(), bounds.bottom() - labelMetrics.height() - 4,
                               contentBounds.width(), labelMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         labelMetrics.elidedText(detail, Qt::ElideRight, contentBounds.width()));
    }
}

QString MetricStatisticsWidget::displayValue(const std::optional<double>& value) const
{
    return value.has_value() ? QString::number(*value, 'f', precision_) : QStringLiteral("--");
}

QColor MetricStatisticsWidget::entryColor(std::size_t index, const MetricStatisticEntry& entry) const
{
    if (entry.color.isValid())
        return entry.color;
    const std::array colors {
        themeColor(themePalette_, "accent", palette().color(QPalette::Highlight)),
        themeColor(themePalette_, "accent-alt", palette().color(QPalette::Link)),
        themeColor(themePalette_, "success", palette().color(QPalette::Highlight)),
        themeColor(themePalette_, "danger", palette().color(QPalette::BrightText))
    };
    return colors[index % colors.size()];
}

int MetricStatisticsWidget::columnCount() const noexcept
{
    const auto availableColumns = std::max(1, width() / entryMinimumWidth);
    return std::max(1, std::min(static_cast<int>(entries_.size()), availableColumns));
}

QRect MetricStatisticsWidget::entryRect(int index) const
{
    const auto columns = columnCount();
    const auto row = index / columns;
    const auto column = index % columns;
    const auto availableWidth = std::max(1, width() - margin * 2);
    const auto left = margin + column * availableWidth / columns;
    const auto right = margin + (column + 1) * availableWidth / columns;
    return QRect(left,
                 margin + row * entryHeight,
                 std::max(1, right - left),
                 entryHeight);
}

}