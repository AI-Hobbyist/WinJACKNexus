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

#include <WinJACKNexus/UI/AnimatedWidget.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>

#include <QString>

#include <optional>
#include <vector>

namespace WinJACKNexus::UI {

struct MeterLevelMark final {
    float value = 0.0F;
    QString label;
};

class WINJACK_UI_API MeterLevelBar final : public AnimatedWidget {
public:
    explicit MeterLevelBar(QWidget* parent = nullptr);
    ~MeterLevelBar() override = default;

    void setRange(float minimum, float maximum);
    float minimum() const noexcept;
    float maximum() const noexcept;
    void setThresholds(float warning, float danger);
    void setMarks(std::vector<MeterLevelMark> marks);
    const std::vector<MeterLevelMark>& marks() const noexcept;
    void setUnit(const QString& unit);
    QString unit() const;
    void setShowScaleLabels(bool show);
    bool showScaleLabels() const noexcept;

    void setValue(std::optional<float> value);
    void clearValue();
    std::optional<float> value() const noexcept;
    void setPeakValue(std::optional<float> value);
    std::optional<float> peakValue() const noexcept;
    void advanceAnimation();

    void setThemePalette(const ThemePalette& palette);
    ThemePalette themePalette() const;

    QSize sizeHint() const override;

protected:
    void paintContent(QPainter& painter) override;
    void advanceFrame() override;

private:
    float normalize(float value) const noexcept;
    float denormalize(float value) const noexcept;
    QString markLabel(const MeterLevelMark& mark) const;

    float minimum_ = -72.0F;
    float maximum_ = 12.0F;
    float warning_ = -18.0F;
    float danger_ = 0.0F;
    std::optional<float> value_;
    std::optional<float> peakValue_;
    float displayLevel_ = 0.0F;
    float displayPeak_ = 0.0F;
    int peakHoldFrames_ = 0;
    std::vector<MeterLevelMark> marks_;
    QString unit_;
    bool showScaleLabels_ = false;
    ThemePalette themePalette_ = ThemePalette::darkRack();
};

}