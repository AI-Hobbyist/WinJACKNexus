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
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>

#include <QFontMetrics>
#include <QHelpEvent>
#include <QToolTip>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>

#include <algorithm>

namespace WinJACKNexus::UI {

namespace {

QColor themeColor(const ThemePalette& palette, const char* key, const QColor& fallback)
{
    const auto color = palette.color(QString::fromLatin1(key));
    return color.isValid() ? color : fallback;
}

constexpr int entryHeight = 28;
constexpr int horizontalMargin = 6;

}

HistoryLegendWidget::HistoryLegendWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(entryHeight + horizontalMargin * 2);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
}

void HistoryLegendWidget::setEntries(std::vector<HistoryLegendEntry> entries)
{
    entries_ = std::move(entries);
    updateGeometry();
    update();
}

const std::vector<HistoryLegendEntry>& HistoryLegendWidget::entries() const noexcept
{
    return entries_;
}

bool HistoryLegendWidget::setEntryVisible(const QString& id, bool visible)
{
    const auto entry = std::find_if(entries_.begin(), entries_.end(), [&id](const HistoryLegendEntry& item) {
        return item.id == id;
    });
    if (entry == entries_.end() || !entry->enabled || entry->visible == visible)
        return false;
    entry->visible = visible;
    update();
    emit entryVisibilityChanged(entry->id, entry->visible);
    return true;
}

void HistoryLegendWidget::setThemePalette(const ThemePalette& palette)
{
    if (!palette.isValid())
        return;
    themePalette_ = palette;
    update();
}

ThemePalette HistoryLegendWidget::themePalette() const
{
    return themePalette_;
}

QSize HistoryLegendWidget::sizeHint() const
{
    const auto columns = std::min(4, std::max(1, static_cast<int>(entries_.size())));
    const auto rows = std::max(1, (static_cast<int>(entries_.size()) + columns - 1) / columns);
    return QSize(columns * 156 + horizontalMargin * 2, rows * entryHeight + horizontalMargin * 2);
}

void HistoryLegendWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto panelBackground = themeColor(themePalette_, "panel-bg", palette().color(QPalette::Window));
    const auto border = themeColor(themePalette_, "border", palette().color(QPalette::Mid));
    const auto text = themeColor(themePalette_, "text", palette().color(QPalette::WindowText));
    const auto muted = themeColor(themePalette_, "text-muted", palette().color(QPalette::Mid));
    const auto accent = themeColor(themePalette_, "accent", palette().color(QPalette::Highlight));
    painter.fillRect(rect(), panelBackground);

    const QFontMetrics metrics(painter.font());
    if (entries_.empty()) {
        painter.setPen(muted);
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("--"));
        return;
    }
    for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
        const auto& entry = entries_[static_cast<std::size_t>(index)];
        const auto bounds = entryRect(index).adjusted(1, 2, -1, -2);
        const auto box = QRect(bounds.left(), bounds.center().y() - 7, 14, 14);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(entry.visible && entry.enabled ? (entry.color.isValid() ? entry.color : accent) : Qt::NoBrush);
        painter.drawRoundedRect(box, 2.0, 2.0);
        if (entry.visible && entry.enabled) {
            painter.setPen(themeColor(themePalette_, "window-bg", palette().color(QPalette::Window)));
            painter.drawLine(box.left() + 3, box.center().y(), box.center().x() - 1, box.bottom() - 3);
            painter.drawLine(box.center().x() - 1, box.bottom() - 3, box.right() - 2, box.top() + 3);
        }
        painter.setPen(entry.enabled ? text : muted);
        painter.drawText(QRect(box.right() + 7, bounds.top(), bounds.right() - box.right() - 7, bounds.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(entry.label, Qt::ElideRight, std::max(0, bounds.right() - box.right() - 7)));
    }
}

bool HistoryLegendWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            if (!entryRect(index).contains(help->pos())) continue;
            QToolTip::showText(help->globalPos(),entries_[static_cast<std::size_t>(index)].label,this);
            event->accept(); return true;
        }
    }
    return QWidget::event(event);
}

void HistoryLegendWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            if (entryRect(index).contains(event->position().toPoint())) {
                const auto& entry = entries_[static_cast<std::size_t>(index)];
                setEntryVisible(entry.id, !entry.visible);
                event->accept();
                return;
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

int HistoryLegendWidget::columnCount() const noexcept
{
    return std::max(1, width() / 156);
}

QRect HistoryLegendWidget::entryRect(int index) const
{
    const auto columns = columnCount();
    const auto row = index / columns;
    const auto column = index % columns;
    const auto availableWidth = std::max(1, width() - horizontalMargin * 2);
    const auto entryWidth = std::max(1, availableWidth / columns);
    return QRect(horizontalMargin + column * entryWidth,
                 horizontalMargin + row * entryHeight,
                 entryWidth,
                 entryHeight);
}

}
