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
#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <optional>

namespace WinJACKNexus::UI {

class WINJACK_UI_API MetricReadout final : public QWidget {
public:
    explicit MetricReadout(QWidget* parent = nullptr);
    ~MetricReadout() override = default;

    void setLabel(const QString& label);
    QString label() const;
    void setValue(std::optional<double> value);
    void clearValue();
    std::optional<double> value() const noexcept;
    void setPeakValueRefreshEnabled(bool enabled);
    bool peakValueRefreshEnabled() const noexcept;
    void setUnit(const QString& unit);
    QString unit() const;
    void setPrecision(int precision);
    int precision() const noexcept;
    QString displayText() const;
    void setAccentColor(const QColor& color);
    QColor accentColor() const;
    void setShowSurface(bool visible);
    bool showSurface() const noexcept;
    void setThemePalette(const ThemePalette& palette);
    ThemePalette themePalette() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor resolvedAccentColor() const;

    QString label_;
    std::optional<double> value_;
    QElapsedTimer valueRefreshClock_;
    bool peakValueRefreshEnabled_ = false;
    QString unit_;
    int precision_ = 1;
    QColor accentColor_;
    bool showSurface_ = true;
    ThemePalette themePalette_ = ThemePalette::darkRack();
};

}
