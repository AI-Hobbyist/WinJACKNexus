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
#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

namespace WinJACKNexus::UI {

struct HistoryLegendEntry final {
    QString id;
    QString label;
    QColor color;
    bool visible = true;
    bool enabled = true;
};

class WINJACK_UI_API HistoryLegendWidget final : public QWidget {
    Q_OBJECT

public:
    explicit HistoryLegendWidget(QWidget* parent = nullptr);
    ~HistoryLegendWidget() override = default;

    void setEntries(std::vector<HistoryLegendEntry> entries);
    const std::vector<HistoryLegendEntry>& entries() const noexcept;
    bool setEntryVisible(const QString& id, bool visible);
    void setThemePalette(const ThemePalette& palette);
    ThemePalette themePalette() const;

    QSize sizeHint() const override;

Q_SIGNALS:
    void entryVisibilityChanged(const QString& id, bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int columnCount() const noexcept;
    QRect entryRect(int index) const;

    std::vector<HistoryLegendEntry> entries_;
    ThemePalette themePalette_ = ThemePalette::darkRack();
};

}