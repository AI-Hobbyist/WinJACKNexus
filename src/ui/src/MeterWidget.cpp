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
#include <WinJACKNexus/UI/MeterWidget.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI
{

namespace
{

constexpr float levelDecay = 0.90f;
constexpr float peakDecay = 0.015f;

class MeterScaleWidget final : public QWidget
{
  public:
    explicit MeterScaleWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedWidth(34);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setRange(float minimum, float maximum)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
            return;
        minimum_ = minimum;
        maximum_ = maximum;
        update();
    }

    void setScaleMarks(const std::vector<float> &marks)
    {
        scaleMarks_ = marks;
        update();
    }

    QSize sizeHint() const override
    {
        return QSize(34, 190);
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const auto font = painter.font();
        painter.setFont(QFont(font.family(), 8));
        const QFontMetrics metrics(painter.font());
        const int bottomMargin = metrics.height() + 8;
        const QRectF scaleBounds = QRectF(rect()).adjusted(0, 8, 0, -bottomMargin);
        if (scaleBounds.height() <= 2)
            return;
        Controls::MeterStyle style;
        style.minimumDb = minimum_;
        style.maximumDb = maximum_;
        style.vertical = true;
        style.ticksLeft = true;
        style.tickBandPixels = scaleBounds.width();
        style.tickFontPixelSize = 8;
        style.labelColor = QColor("#9aada5");
        for (const auto mark : scaleMarks_)
            style.ticks.push_back({mark, formatValue(mark)});
        Controls::drawMeterScale(painter, scaleBounds, style);
    }

  private:
    static QString formatValue(float value)
    {
        const auto rounded = std::round(value);
        if (std::abs(value - rounded) < 0.01f)
            return QString::number(static_cast<int>(rounded));
        return QString::number(value, 'f', 1);
    }

    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    std::vector<float> scaleMarks_;
};

}

MeterWidget::MeterWidget(QWidget *parent) : AnimatedWidget(parent)
{
    setMinimumWidth(40);
    setMinimumHeight(150);
    startAnimation();
}

void MeterWidget::setRange(float minimum, float maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
        return;
    minimum_ = minimum;
    maximum_ = maximum;
    level_ = 0.0f;
    peak_ = 0.0f;
    animationClock_.invalidate();
    peakClock_.invalidate();
    peakHoldUntilMs_ = 0;
    update();
}

void MeterWidget::setThresholds(float warning, float danger)
{
    warning_ = normalize(warning);
    danger_ = normalize(danger);
    if (warning_ > danger_)
        std::swap(warning_, danger_);
    update();
}

void MeterWidget::setScaleMarks(const std::vector<float> &marks)
{
    scaleMarks_.clear();
    scaleMarks_.reserve(marks.size());
    for (const auto mark : marks)
    {
        if (std::isfinite(mark) && mark >= minimum_ && mark <= maximum_)
            scaleMarks_.push_back(mark);
    }
    update();
}

void MeterWidget::setValue(float value)
{
    const auto normalized = normalize(value);
    level_ = normalized;
    if (normalized >= peak_)
    {
        peak_ = normalized;
        if (!peakClock_.isValid())
            peakClock_.start();
        peakHoldUntilMs_ =
            peakClock_.elapsed() + static_cast<qint64>(std::llround(
                                       ApplicationSettings::instance().peakHoldSeconds() * 1000.0));
    }
}

void MeterWidget::clearValue()
{
    level_ = 0.0f;
    peak_ = 0.0f;
    animationClock_.invalidate();
    peakClock_.invalidate();
    peakHoldUntilMs_ = 0;
    update();
}

void MeterWidget::setChannelLabel(const QString &label)
{
    if (channelLabel_ == label)
        return;
    channelLabel_ = label;
    update();
}

void MeterWidget::setShowScaleLabels(bool show)
{
    if (showScaleLabels_ == show)
        return;
    showScaleLabels_ = show;
    setMinimumWidth(show ? 68 : 40);
    updateGeometry();
    update();
}

float MeterWidget::value() const noexcept
{
    return denormalize(level_);
}

float MeterWidget::peakValue() const noexcept
{
    return denormalize(peak_);
}

QSize MeterWidget::sizeHint() const
{
    return QSize(showScaleLabels_ ? 68 : 40, 190);
}

void MeterWidget::paintContent(QPainter &painter)
{
    const auto scope = property("wjnThemeScope").toString();
    const auto meterColor = [&scope](const char *role, const QColor &fallback) {
        return Controls::themeTokenColor(scope.isEmpty() ? QStringLiteral("global") : scope,
                                         QString::fromLatin1(role), fallback);
    };
    painter.fillRect(rect(), meterColor("surface-window", QColor(QStringLiteral("#101817"))));

    const auto font = painter.font();
    painter.setFont(QFont(font.family(), 8));
    const QFontMetrics metrics(painter.font());
    const int bottomMargin = metrics.height() + 8;
    const QRectF meterBounds = QRectF(rect()).adjusted(4, 8, -5, -bottomMargin);
    if (meterBounds.width() <= (showScaleLabels_ ? 32 : 2) || meterBounds.height() <= 2)
        return;
    Controls::MeterStyle style;
    style.minimumDb = minimum_;
    style.maximumDb = maximum_;
    style.zones = {
        {denormalize(warning_), meterColor("meter-level-zone-low-active", QColor("#42c875")),
         meterColor("meter-level-zone-low-inactive", QColor("#1b2928"))},
        {denormalize(danger_), meterColor("meter-level-zone-mid-active", QColor("#e1b84d")),
         meterColor("meter-level-zone-mid-inactive", QColor("#1b2928"))},
        {maximum_, meterColor("meter-level-zone-high-active", QColor("#ef615f")),
         meterColor("meter-level-zone-high-inactive", QColor("#1b2928"))}};
    style.vertical = true;
    style.border = meterColor("meter-level-border", QColor("#38504d"));
    style.peakColor = meterColor("meter-level-peak", QColor("#f4f3dc"));
    style.labelColor = meterColor("meter-level-label", QColor("#9aada5"));
    style.ticksLeft = true;
    style.showTickGrid = true;
    style.tickBandPixels = showScaleLabels_ ? 30 : 0;
    style.tickFontPixelSize = 8;
    for (const auto mark : scaleMarks_)
        style.ticks.push_back({mark, showScaleLabels_ ? formatValue(mark) : QString()});
    Controls::drawMeter(painter, meterBounds, denormalize(level_), denormalize(peak_), style);

    painter.setPen(QColor(QStringLiteral("#d6e2dc")));
    painter.drawText(
        QRect(0, rect().bottom() - metrics.height() - 3, rect().width(), metrics.height()),
        Qt::AlignHCenter | Qt::AlignVCenter, channelLabel_);
}

void MeterWidget::advanceFrame()
{
    const auto elapsedMilliseconds = animationClock_.isValid() ? animationClock_.restart() : -1;
    if (elapsedMilliseconds < 0)
        animationClock_.start();
    const auto elapsedSeconds = elapsedMilliseconds > 0
                                    ? std::min(elapsedMilliseconds / 1000.0f, 0.1f)
                                    : 1.0f / frameRate();
    level_ *= std::pow(levelDecay, 60.0f * elapsedSeconds);
    if (level_ < 0.0001f)
        level_ = 0.0f;
    const auto peakNow = peakClock_.isValid() ? peakClock_.elapsed() : 0;
    if (!peakClock_.isValid() || peakNow >= peakHoldUntilMs_)
    {
        const auto decaySeconds =
            peakClock_.isValid()
                ? std::min(elapsedSeconds,
                           std::max(0.0f, static_cast<float>(peakNow - peakHoldUntilMs_) / 1000.0f))
                : elapsedSeconds;
        peak_ = std::max(0.0f, peak_ - peakDecay * 60.0f * decaySeconds);
    }
}

bool MeterWidget::advanceFromArray()
{
    const auto previousLevel = level_;
    const auto previousPeak = peak_;
    advanceFrame();
    return level_ != previousLevel || peak_ != previousPeak;
}

float MeterWidget::normalize(float value) const noexcept
{
    if (!std::isfinite(value) || maximum_ <= minimum_)
        return 0.0f;
    return std::clamp((value - minimum_) / (maximum_ - minimum_), 0.0f, 1.0f);
}

float MeterWidget::denormalize(float value) const noexcept
{
    return minimum_ + std::clamp(value, 0.0f, 1.0f) * (maximum_ - minimum_);
}

QString MeterWidget::formatValue(float value) const
{
    const auto rounded = std::round(value);
    if (std::abs(value - rounded) < 0.01f)
        return QString::number(static_cast<int>(rounded));
    return QString::number(value, 'f', 1);
}

PeakMeterWidget::PeakMeterWidget(QWidget *parent) : MeterWidget(parent)
{
    setRange(-70.0f, 12.0f);
    setThresholds(-18.0f, 0.0f);
    setScaleMarks(
        {-70.0f, -60.0f, -40.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f});
}

MeterArrayWidget::MeterArrayWidget(QWidget *parent)
    : QWidget(parent), layout_(new QHBoxLayout(this)), animationTimer_(this)
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(3);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    scaleWidget_ = new MeterScaleWidget(this);
    scaleWidget_->setVisible(false);
    layout_->addWidget(scaleWidget_);
    setChannelCount(1);
    auto &settings = ApplicationSettings::instance();
    frameRate_ = settings.frameRate();
    connect(&settings, &ApplicationSettings::frameRateChanged, this,
            &MeterArrayWidget::applyFrameRate);
    animationTimer_.setTimerType(Qt::PreciseTimer);
    animationTimer_.setInterval(1000 / frameRate_);
    connect(&animationTimer_, &QTimer::timeout, this, [this] { advanceMeters(); });
}

void MeterArrayWidget::setChannelCount(std::uint16_t count)
{
    const auto normalizedCount =
        static_cast<std::uint16_t>(std::clamp<std::uint16_t>(count, 1, 16));
    if (channelCount_ == normalizedCount)
        return;

    while (meters_.size() < normalizedCount)
    {
        auto *meter = new PeakMeterWidget(this);
        meter->stopAnimation();
        meters_.push_back(meter);
        layout_->addWidget(meter, 1);
    }
    while (meters_.size() > normalizedCount)
    {
        auto *item = layout_->takeAt(layout_->count() - 1);
        delete meters_.back();
        meters_.pop_back();
        delete item;
    }
    channelCount_ = normalizedCount;
    for (std::size_t index = 0; index < meters_.size(); ++index)
        configureMeter(*meters_[index], index);
    updateGeometry();
}

std::uint16_t MeterArrayWidget::channelCount() const noexcept
{
    return channelCount_;
}

void MeterArrayWidget::setRange(float minimum, float maximum)
{
    if (maximum <= minimum)
        return;
    minimum_ = minimum;
    maximum_ = maximum;
    static_cast<MeterScaleWidget *>(scaleWidget_)->setRange(minimum_, maximum_);
    for (std::size_t index = 0; index < meters_.size(); ++index)
        configureMeter(*meters_[index], index);
}

void MeterArrayWidget::setThresholds(float warning, float danger)
{
    warning_ = warning;
    danger_ = danger;
    for (std::size_t index = 0; index < meters_.size(); ++index)
        configureMeter(*meters_[index], index);
}

void MeterArrayWidget::setScaleMarks(const std::vector<float> &marks)
{
    scaleMarks_ = marks;
    static_cast<MeterScaleWidget *>(scaleWidget_)->setScaleMarks(scaleMarks_);
    for (std::size_t index = 0; index < meters_.size(); ++index)
        configureMeter(*meters_[index], index);
}

void MeterArrayWidget::setShowScaleLabels(bool show)
{
    if (showScaleLabels_ == show)
        return;
    showScaleLabels_ = show;
    scaleWidget_->setVisible(showScaleLabels_);
    for (std::size_t index = 0; index < meters_.size(); ++index)
        configureMeter(*meters_[index], index);
    updateGeometry();
}

void MeterArrayWidget::setFrameRate(int framesPerSecond)
{
    ApplicationSettings::instance().setFrameRate(framesPerSecond);
}

void MeterArrayWidget::applyFrameRate(int framesPerSecond)
{
    if (frameRate_ == framesPerSecond)
        return;
    frameRate_ = framesPerSecond;
    animationTimer_.setInterval(std::max(1, 1000 / frameRate_));
}

int MeterArrayWidget::frameRate() const noexcept
{
    return frameRate_;
}

void MeterArrayWidget::setValues(const std::array<float, 16> &values, std::uint16_t count)
{
    const auto normalizedCount =
        static_cast<std::uint16_t>(std::clamp<std::uint16_t>(count, 1, 16));
    if (channelCount_ != normalizedCount)
        setChannelCount(normalizedCount);
    for (std::uint16_t channel = 0; channel < channelCount_; ++channel)
        meters_[channel]->setValue(values[channel]);
}

void MeterArrayWidget::setValue(std::uint16_t channel, float value)
{
    if (channel < channelCount_)
        meters_[channel]->setValue(value);
}

void MeterArrayWidget::clear()
{
    for (auto *meter : meters_)
        meter->clearValue();
}

QSize MeterArrayWidget::sizeHint() const
{
    const auto labelWidth = showScaleLabels_ ? 34 : 0;
    const auto spacingWidth = static_cast<int>(channelCount_) * layout_->spacing();
    return QSize(labelWidth + static_cast<int>(channelCount_) * 43 + spacingWidth, 190);
}

void MeterArrayWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    synchronizeAnimationTimer();
}

void MeterArrayWidget::hideEvent(QHideEvent *event)
{
    animationTimer_.stop();
    QWidget::hideEvent(event);
}

void MeterArrayWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        synchronizeAnimationTimer();
}

void MeterArrayWidget::synchronizeAnimationTimer()
{
    if (isVisible() && !isMinimized())
    {
        if (!animationTimer_.isActive())
            animationTimer_.start();
    }
    else
    {
        animationTimer_.stop();
    }
}

void MeterArrayWidget::advanceMeters()
{
    if (!isVisible() || isMinimized())
    {
        synchronizeAnimationTimer();
        return;
    }
    for (auto *meter : meters_)
    {
        if (meter->advanceFromArray())
            meter->update();
    }
}

void MeterArrayWidget::configureMeter(MeterWidget &meter, std::size_t index)
{
    meter.stopAnimation();
    meter.setRange(minimum_, maximum_);
    meter.setThresholds(warning_, danger_);
    meter.setScaleMarks(scaleMarks_);
    meter.setShowScaleLabels(false);
    meter.setChannelLabel(QString::number(static_cast<int>(index + 1)));
}

MidiVelocityWidget::MidiVelocityWidget(QWidget *parent) : MeterArrayWidget(parent)
{
    setRange(0.0f, 127.0f);
    setThresholds(96.0f, 120.0f);
    setScaleMarks({0.0f, 32.0f, 64.0f, 96.0f, 127.0f});
    setShowScaleLabels(true);
    setChannelCount(16);
}

}
