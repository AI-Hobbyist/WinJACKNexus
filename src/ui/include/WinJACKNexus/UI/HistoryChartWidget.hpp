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

#include <WinJACKNexus/UI/PaintableWidget.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QColor>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace WinJACKNexus::UI {

struct HistoryChartPoint final {
    std::int64_t timestampMilliseconds = 0;
    std::optional<double> value;
};

struct HistoryChartSeries final {
    QString id;
    QString label;
    QColor color;
    bool visible = true;
    std::vector<HistoryChartPoint> points;
};

struct HistoryChartReferenceLine final {
    double value = 0.0;
    QString label;
    QColor color;
    Qt::PenStyle penStyle = Qt::DashLine;
    bool visible = true;
};

class WINJACK_UI_API HistoryChartWidget final : public PaintableWidget {
public:
    explicit HistoryChartWidget(QWidget* parent = nullptr);
    ~HistoryChartWidget() override = default;

    void setTimeRange(std::int64_t startMilliseconds, std::int64_t endMilliseconds);
    std::int64_t startTimeMilliseconds() const noexcept;
    std::int64_t endTimeMilliseconds() const noexcept;
    void setValueRange(double minimum, double maximum);
    double minimumValue() const noexcept;
    double maximumValue() const noexcept;
    void setVerticalAxisLabel(const QString& label);
    QString verticalAxisLabel() const;
    void setHorizontalAxisLabel(const QString& label);
    QString horizontalAxisLabel() const;
    void setSeries(std::vector<HistoryChartSeries> series);
    const std::vector<HistoryChartSeries>& series() const noexcept;
    void setReferenceLines(std::vector<HistoryChartReferenceLine> lines);
    const std::vector<HistoryChartReferenceLine>& referenceLines() const noexcept;
    void setThemePalette(const ThemePalette& palette);
    ThemePalette themePalette() const;

    QSize sizeHint() const override;

protected:
    void paintContent(QPainter& painter) override;

private:
    QColor seriesColor(std::size_t index, const HistoryChartSeries& series) const;
    QPointF pointPosition(const QRectF& chartRect, std::int64_t timestampMilliseconds, double value) const;

    std::int64_t startTimeMilliseconds_ = 0;
    std::int64_t endTimeMilliseconds_ = 60000;
    double minimumValue_ = -70.0;
    double maximumValue_ = 12.0;
    QString verticalAxisLabel_;
    QString horizontalAxisLabel_;
    std::vector<HistoryChartSeries> series_;
    std::vector<HistoryChartReferenceLine> referenceLines_;
    ThemePalette themePalette_ = ThemePalette::darkRack();
};

}