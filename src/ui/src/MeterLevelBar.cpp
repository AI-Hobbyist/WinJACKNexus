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
#include <WinJACKNexus/UI/MeterLevelBar.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {

namespace {

constexpr float peakDecayPerFrame = 0.012F;

QColor themeColor(const ThemePalette& palette, const char* key, const QColor& fallback)
{
    const auto color = palette.color(QString::fromLatin1(key));
    return color.isValid() ? color : fallback;
}

}

MeterLevelBar::MeterLevelBar(QWidget* parent)
    : AnimatedWidget(parent)
{
    setMinimumSize(36, 220);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    setMarks({
        { -72.0F, QStringLiteral("-72") },
        { -60.0F, QStringLiteral("-60") },
        { -40.0F, QStringLiteral("-40") },
        { -20.0F, QStringLiteral("-20") },
        { 0.0F, QStringLiteral("0") },
        { 12.0F, QStringLiteral("+12") }
    });
}

void MeterLevelBar::setRange(float minimum, float maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
        return;
    minimum_ = minimum;
    maximum_ = maximum;
    warning_ = std::clamp(warning_, minimum_, maximum_);
    danger_ = std::clamp(danger_, warning_, maximum_);
    if (value_.has_value())
        value_ = denormalize(normalize(*value_));
    if (peakValue_.has_value())
        peakValue_ = denormalize(normalize(*peakValue_));
    if (incomingPeak_.has_value())
        incomingPeak_ = denormalize(normalize(*incomingPeak_));
    displayLevel_ = value_.has_value() ? normalize(*value_) : 0.0F;
    displayPeak_ = peakValue_.has_value() ? normalize(*peakValue_) : 0.0F;
    animationClock_.invalidate();
    update();
}

float MeterLevelBar::minimum() const noexcept
{
    return minimum_;
}

float MeterLevelBar::maximum() const noexcept
{
    return maximum_;
}

void MeterLevelBar::setThresholds(float warning, float danger)
{
    if (!std::isfinite(warning) || !std::isfinite(danger))
        return;
    warning_ = std::clamp(warning, minimum_, maximum_);
    danger_ = std::clamp(danger, minimum_, maximum_);
    if (danger_ < warning_)
        std::swap(warning_, danger_);
    update();
}

void MeterLevelBar::setMarks(std::vector<MeterLevelMark> marks)
{
    marks.erase(std::remove_if(marks.begin(), marks.end(), [this](const MeterLevelMark& mark) {
        return !std::isfinite(mark.value) || mark.value < minimum_ || mark.value > maximum_;
    }), marks.end());
    std::sort(marks.begin(), marks.end(), [](const MeterLevelMark& left, const MeterLevelMark& right) {
        return left.value < right.value;
    });
    marks_ = std::move(marks);
    update();
}

const std::vector<MeterLevelMark>& MeterLevelBar::marks() const noexcept
{
    return marks_;
}

void MeterLevelBar::setUnit(const QString& unit)
{
    if (unit_ == unit)
        return;
    unit_ = unit;
    update();
}

QString MeterLevelBar::unit() const
{
    return unit_;
}

void MeterLevelBar::setShowScaleLabels(bool show)
{
    if (showScaleLabels_ == show)
        return;
    showScaleLabels_ = show;
    setMinimumWidth(show ? 72 : 36);
    updateGeometry();
    update();
}

bool MeterLevelBar::showScaleLabels() const noexcept
{
    return showScaleLabels_;
}

void MeterLevelBar::setValue(std::optional<float> value)
{
    if (!value.has_value() || !std::isfinite(*value)) {
        clearValue();
        return;
    }
    value_ = denormalize(normalize(*value));
    const auto normalized = normalize(*value_);
    displayLevel_ = std::max(displayLevel_, normalized);
    if (!peakValue_.has_value() || normalized > displayPeak_) {
        peakValue_ = *value_;
        displayPeak_ = normalized;
        if (!peakClock_.isValid()) peakClock_.start();
        peakHoldUntilMs_ = peakClock_.elapsed() + static_cast<qint64>(std::llround(
            ApplicationSettings::instance().peakHoldSeconds() * 1000.0));
    }
    update();
}

void MeterLevelBar::clearValue()
{
    value_.reset();
    peakValue_.reset();
    incomingPeak_.reset();
    displayLevel_ = 0.0F;
    displayPeak_ = 0.0F;
    animationClock_.invalidate();
    peakClock_.invalidate();
    peakHoldUntilMs_ = 0;
    update();
}

std::optional<float> MeterLevelBar::value() const noexcept
{
    return value_;
}

void MeterLevelBar::setPeakValue(std::optional<float> value)
{
    if (!value.has_value() || !std::isfinite(*value)) {
        incomingPeak_.reset();
        if (!value_.has_value()) {
            peakValue_.reset();
            displayPeak_ = 0.0F;
            peakClock_.invalidate();
            peakHoldUntilMs_ = 0;
        }
        update();
        return;
    }
    incomingPeak_ = denormalize(normalize(*value));
    const auto normalized = normalize(*incomingPeak_);
    if (!peakValue_.has_value() || normalized > displayPeak_) {
        peakValue_ = *incomingPeak_;
        displayPeak_ = normalized;
        if (!peakClock_.isValid()) peakClock_.start();
        peakHoldUntilMs_ = peakClock_.elapsed() + static_cast<qint64>(std::llround(
            ApplicationSettings::instance().peakHoldSeconds() * 1000.0));
    }
    update();
}

std::optional<float> MeterLevelBar::peakValue() const noexcept
{
    return peakValue_;
}

void MeterLevelBar::advanceAnimation()
{
    const auto previousDisplayLevel = displayLevel_;
    const auto previousDisplayPeak = displayPeak_;
    const auto previousPeakValue = peakValue_;
    advanceFrame();
    if (displayLevel_ != previousDisplayLevel || displayPeak_ != previousDisplayPeak
        || peakValue_ != previousPeakValue)
        update();
}

void MeterLevelBar::setThemePalette(const ThemePalette& palette)
{
    if (!palette.isValid())
        return;
    themePalette_ = palette;
    update();
}

ThemePalette MeterLevelBar::themePalette() const
{
    return themePalette_;
}

QSize MeterLevelBar::sizeHint() const
{
    return QSize(showScaleLabels_ ? 78 : 42, 260);
}

void MeterLevelBar::paintContent(QPainter& painter)
{
    const auto panelBackground = themeColor(themePalette_, "panel-bg", palette().color(QPalette::Window));
    const auto panelRaised = themeColor(themePalette_, "panel-raised", palette().color(QPalette::Base));
    const auto border = themeColor(themePalette_, "border", palette().color(QPalette::Mid));
    const auto text = themeColor(themePalette_, "text", palette().color(QPalette::WindowText));
    const auto muted = themeColor(themePalette_, "text-muted", palette().color(QPalette::Mid));
    const auto success = themeColor(themePalette_, "success", palette().color(QPalette::Highlight));
    const auto warning = themeColor(themePalette_, "accent-alt", palette().color(QPalette::Link));
    const auto danger = themeColor(themePalette_, "danger", palette().color(QPalette::BrightText));

    painter.fillRect(rect(), panelBackground);
    const QFontMetrics metrics(painter.font());
    const auto scaleWidth = showScaleLabels_ ? std::max(32, metrics.horizontalAdvance(QStringLiteral("-70")) + 8) : 0;
    const auto bottomMargin = std::max(metrics.height() + 8, unit_.isEmpty() ? 8 : metrics.height() * 2 + 8);
    const QRectF meterBounds = QRectF(rect()).adjusted(4, 10, -8, -bottomMargin);
    if (meterBounds.width() <= scaleWidth + 8 || meterBounds.height() <= 4.0)
        return;
    Controls::MeterStyle style;
    style.minimumDb = minimum_;
    style.maximumDb = maximum_;
    const auto scope = property("wjnThemeScope").toString();
    const auto meterColor = [&scope](const char* role, const QColor& fallback) {
        return Controls::themeTokenColor(scope.isEmpty() ? QStringLiteral("global") : scope,
                                         QString::fromLatin1(role), fallback);
    };
    style.zones = {
        {warning_, meterColor("meter-level-zone-low-active", success),
                   meterColor("meter-level-zone-low-inactive", panelRaised)},
        {danger_, meterColor("meter-level-zone-mid-active", warning),
                  meterColor("meter-level-zone-mid-inactive", panelRaised)},
        {maximum_, meterColor("meter-level-zone-high-active", danger),
                   meterColor("meter-level-zone-high-inactive", panelRaised)}
    };
    style.vertical = true;
    style.border = meterColor("meter-level-border", border);
    style.peakColor = meterColor("meter-level-peak", text);
    style.labelColor = meterColor("meter-level-label", muted);
    style.tickColor = meterColor("meter-level-tick", border);
    style.ticksLeft = true;
    style.showTickGrid = true;
    style.tickBandPixels = showScaleLabels_ ? scaleWidth + 4 : 0;
    style.tickFontPixelSize = std::max(8, metrics.height() - 2);
    for (const auto& mark : marks_)
        style.ticks.push_back({mark.value, showScaleLabels_ ? markLabel(mark) : QString()});
    Controls::drawMeter(painter, meterBounds,
                        value_.has_value() ? denormalize(displayLevel_) : minimum_,
                        peakValue_.has_value() ? std::optional<double>(denormalize(displayPeak_))
                                               : std::nullopt,
                        style);
    if (!value_.has_value()) {
        painter.setPen(muted);
        painter.drawText(meterBounds.adjusted(style.tickBandPixels, 0, 0, 0),
                         Qt::AlignCenter, QStringLiteral("--"));
    }

    if (!unit_.isEmpty()) {
        painter.setPen(muted);
        painter.drawText(QRectF(scaleWidth, rect().bottom() - metrics.height() - 3, rect().width() - scaleWidth, metrics.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         unit_);
    }
}

void MeterLevelBar::advanceFrame()
{
    if (!value_.has_value()) {
        animationClock_.invalidate();
        return;
    }
    const auto elapsedMilliseconds = animationClock_.isValid() ? animationClock_.restart() : -1;
    if (elapsedMilliseconds < 0) animationClock_.start();
    const auto elapsedSeconds = elapsedMilliseconds > 0
        ? std::min(elapsedMilliseconds / 1000.0F, 0.1F) : 1.0F / frameRate();
    const auto target = normalize(*value_);
    displayLevel_ += (target - displayLevel_)
        * (1.0F - std::pow(0.65F, 60.0F * elapsedSeconds));
    const auto peakNow = peakClock_.isValid() ? peakClock_.elapsed() : 0;
    if (!peakClock_.isValid() || peakNow >= peakHoldUntilMs_) {
        const auto decaySeconds = peakClock_.isValid()
            ? std::min(elapsedSeconds, std::max(0.0F,
                static_cast<float>(peakNow - peakHoldUntilMs_) / 1000.0F))
            : elapsedSeconds;
        const auto peakTarget = incomingPeak_.has_value()
            ? std::max(target, normalize(*incomingPeak_)) : target;
        displayPeak_ = std::max(peakTarget,
            displayPeak_ - peakDecayPerFrame * 60.0F * decaySeconds);
        peakValue_ = denormalize(displayPeak_);
    }
}

float MeterLevelBar::normalize(float value) const noexcept
{
    if (!std::isfinite(value) || maximum_ <= minimum_)
        return 0.0F;
    return std::clamp((value - minimum_) / (maximum_ - minimum_), 0.0F, 1.0F);
}

float MeterLevelBar::denormalize(float value) const noexcept
{
    return minimum_ + std::clamp(value, 0.0F, 1.0F) * (maximum_ - minimum_);
}

QString MeterLevelBar::markLabel(const MeterLevelMark& mark) const
{
    if (!mark.label.isEmpty())
        return mark.label;
    return QString::number(mark.value, 'f', std::abs(mark.value - std::round(mark.value)) < 0.01F ? 0 : 1);
}

}
