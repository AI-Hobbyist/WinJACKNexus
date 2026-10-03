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

#include <optional>
#include <vector>

namespace WinJACKNexus::UI {

struct MetricStatisticEntry final {
    QString id;
    QString label;
    QString unit;
    std::optional<double> currentValue;
    std::optional<double> minimumValue;
    std::optional<double> maximumValue;
    QColor color;
};

class WINJACK_UI_API MetricStatisticsWidget final : public QWidget {
public:
    explicit MetricStatisticsWidget(QWidget* parent = nullptr);
    ~MetricStatisticsWidget() override = default;

    void setEntries(std::vector<MetricStatisticEntry> entries);
    const std::vector<MetricStatisticEntry>& entries() const noexcept;
    void setPrecision(int precision);
    int precision() const noexcept;
    void setThemePalette(const ThemePalette& palette);
    ThemePalette themePalette() const;

    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString displayValue(const std::optional<double>& value) const;
    QColor entryColor(std::size_t index, const MetricStatisticEntry& entry) const;
    int columnCount() const noexcept;
    QRect entryRect(int index) const;

    std::vector<MetricStatisticEntry> entries_;
    int precision_ = 1;
    ThemePalette themePalette_ = ThemePalette::darkRack();
};

}
