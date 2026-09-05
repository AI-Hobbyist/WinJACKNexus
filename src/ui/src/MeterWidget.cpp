#include <WinJACKNexus/UI/MeterWidget.hpp>

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {

namespace {

constexpr int peakHoldFrames = 24;
constexpr float levelDecay = 0.90f;
constexpr float peakDecay = 0.015f;

class MeterScaleWidget final : public QWidget {
public:
    explicit MeterScaleWidget(QWidget* parent = nullptr)
        : QWidget(parent)
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

    void setScaleMarks(const std::vector<float>& marks)
    {
        scaleMarks_ = marks;
        update();
    }

    QSize sizeHint() const override
    {
        return QSize(34, 190);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const auto font = painter.font();
        painter.setFont(QFont(font.family(), 8));
        const QFontMetrics metrics(painter.font());
        const int bottomMargin = metrics.height() + 8;
        const QRect track = rect().adjusted(0, 8, 0, -bottomMargin);
        if (track.height() <= 2)
            return;

        const auto yFor = [this, &track](float value) {
            const auto normalized = maximum_ > minimum_
                ? std::clamp((value - minimum_) / (maximum_ - minimum_), 0.0f, 1.0f)
                : 0.0f;
            return track.bottom() - static_cast<int>(std::round(normalized * track.height()));
        };
        for (const auto mark : scaleMarks_) {
            const auto markY = yFor(mark);
            painter.setPen(QColor(QStringLiteral("#9aada5")));
            painter.drawText(QRect(0, markY - metrics.height() / 2, width() - 4, metrics.height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             formatValue(mark));
            painter.setPen(QPen(QColor(QStringLiteral("#526763")), 1));
            painter.drawLine(width() - 3, markY, width(), markY);
        }
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

MeterWidget::MeterWidget(QWidget* parent)
    : AnimatedWidget(parent)
{
    setFrameRate(60);
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
    peakHoldFrames_ = 0;
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

void MeterWidget::setScaleMarks(const std::vector<float>& marks)
{
    scaleMarks_.clear();
    scaleMarks_.reserve(marks.size());
    for (const auto mark : marks) {
        if (std::isfinite(mark) && mark >= minimum_ && mark <= maximum_)
            scaleMarks_.push_back(mark);
    }
    update();
}

void MeterWidget::setValue(float value)
{
    const auto normalized = normalize(value);
    level_ = normalized;
    if (normalized >= peak_) {
        peak_ = normalized;
        peakHoldFrames_ = peakHoldFrames;
    }
    update();
}

void MeterWidget::clearValue()
{
    level_ = 0.0f;
    peak_ = 0.0f;
    peakHoldFrames_ = 0;
    update();
}

void MeterWidget::setChannelLabel(const QString& label)
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

void MeterWidget::paintContent(QPainter& painter)
{
    painter.fillRect(rect(), QColor(QStringLiteral("#101817")));

    const auto font = painter.font();
    painter.setFont(QFont(font.family(), 8));
    const QFontMetrics metrics(painter.font());
    const int labelWidth = showScaleLabels_ ? 30 : 0;
    const int bottomMargin = metrics.height() + 8;
    const QRect track = rect().adjusted(labelWidth + 4, 8, -5, -bottomMargin);
    if (track.width() <= 2 || track.height() <= 2)
        return;

    painter.setPen(QPen(QColor(QStringLiteral("#38504d")), 1));
    painter.setBrush(QColor(QStringLiteral("#1b2928")));
    painter.drawRect(track);

    const auto yFor = [&track](float normalized) {
        return track.bottom() - static_cast<int>(std::round(std::clamp(normalized, 0.0f, 1.0f) * track.height()));
    };
    const auto warning = std::clamp(warning_, 0.0f, 1.0f);
    const auto danger = std::clamp(danger_, warning, 1.0f);
    const auto drawSegment = [&](float lower, float upper, const QColor& color) {
        const auto start = std::clamp(lower, 0.0f, 1.0f);
        const auto end = std::clamp(upper, start, 1.0f);
        if (level_ <= start)
            return;
        const auto visibleEnd = std::min(level_, end);
        const QRect segment(track.left() + 2,
                            yFor(visibleEnd),
                            std::max(1, track.width() - 3),
                            std::max(1, yFor(start) - yFor(visibleEnd)));
        painter.fillRect(segment, color);
    };
    drawSegment(0.0f, warning, QColor(QStringLiteral("#42c875")));
    drawSegment(warning, danger, QColor(QStringLiteral("#e1b84d")));
    drawSegment(danger, 1.0f, QColor(QStringLiteral("#ef615f")));

    painter.setPen(QPen(QColor(QStringLiteral("#f4f3dc")), 2));
    const auto peakY = yFor(peak_);
    painter.drawLine(track.left() + 1, peakY, track.right() - 1, peakY);

    painter.setPen(QColor(QStringLiteral("#526763")));
    for (const auto mark : scaleMarks_) {
        const auto markY = yFor(normalize(mark));
        painter.drawLine(track.left() - 2, markY, track.right(), markY);
        if (showScaleLabels_) {
            const QRect labelRect(0, markY - metrics.height() / 2, labelWidth, metrics.height());
            painter.setPen(QColor(QStringLiteral("#9aada5")));
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, formatValue(mark));
            painter.setPen(QColor(QStringLiteral("#526763")));
        }
    }

    painter.setPen(QColor(QStringLiteral("#d6e2dc")));
    painter.drawText(QRect(0, rect().bottom() - metrics.height() - 3, rect().width(), metrics.height()),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     channelLabel_);
}

void MeterWidget::advanceFrame()
{
    level_ *= levelDecay;
    if (peakHoldFrames_ > 0) {
        --peakHoldFrames_;
    } else {
        peak_ = std::max(0.0f, peak_ - peakDecay);
    }
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

PeakMeterWidget::PeakMeterWidget(QWidget* parent)
    : MeterWidget(parent)
{
    setRange(-70.0f, 12.0f);
    setThresholds(-18.0f, 0.0f);
    setScaleMarks({ -70.0f, -60.0f, -40.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f });
}

MeterArrayWidget::MeterArrayWidget(QWidget* parent)
    : QWidget(parent)
    , layout_(new QHBoxLayout(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(3);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    scaleWidget_ = new MeterScaleWidget(this);
    scaleWidget_->setVisible(false);
    layout_->addWidget(scaleWidget_);
    setChannelCount(1);
}

void MeterArrayWidget::setChannelCount(std::uint16_t count)
{
    const auto normalizedCount = static_cast<std::uint16_t>(std::clamp<std::uint16_t>(count, 1, 16));
    if (channelCount_ == normalizedCount)
        return;

    while (meters_.size() < normalizedCount) {
        auto* meter = new PeakMeterWidget(this);
        meters_.push_back(meter);
        layout_->addWidget(meter, 1);
    }
    while (meters_.size() > normalizedCount) {
        auto* item = layout_->takeAt(layout_->count() - 1);
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
    static_cast<MeterScaleWidget*>(scaleWidget_)->setRange(minimum_, maximum_);
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

void MeterArrayWidget::setScaleMarks(const std::vector<float>& marks)
{
    scaleMarks_ = marks;
    static_cast<MeterScaleWidget*>(scaleWidget_)->setScaleMarks(scaleMarks_);
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

void MeterArrayWidget::setValues(const std::array<float, 16>& values, std::uint16_t count)
{
    const auto normalizedCount = static_cast<std::uint16_t>(std::clamp<std::uint16_t>(count, 1, 16));
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
    for (auto* meter : meters_)
        meter->clearValue();
}

QSize MeterArrayWidget::sizeHint() const
{
    const auto labelWidth = showScaleLabels_ ? 34 : 0;
    const auto spacingWidth = static_cast<int>(channelCount_) * layout_->spacing();
    return QSize(labelWidth + static_cast<int>(channelCount_) * 43 + spacingWidth, 190);
}

void MeterArrayWidget::configureMeter(MeterWidget& meter, std::size_t index)
{
    meter.setRange(minimum_, maximum_);
    meter.setThresholds(warning_, danger_);
    meter.setScaleMarks(scaleMarks_);
    meter.setShowScaleLabels(false);
    meter.setChannelLabel(QString::number(static_cast<int>(index + 1)));
}

MidiVelocityWidget::MidiVelocityWidget(QWidget* parent)
    : MeterArrayWidget(parent)
{
    setRange(0.0f, 127.0f);
    setThresholds(96.0f, 120.0f);
    setScaleMarks({ 0.0f, 32.0f, 64.0f, 96.0f, 127.0f });
    setShowScaleLabels(true);
    setChannelCount(16);
}

}