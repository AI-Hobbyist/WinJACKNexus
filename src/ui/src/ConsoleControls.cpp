// SPDX-License-Identifier: AGPL-3.0-only
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI::Controls
{
namespace CM = Common::ClassicMixer;
namespace
{
QColor color(const QWidget &widget, const QString &scope, const QString &role,
             QPalette::ColorRole fallback)
{
    return themeTokenColor(scope, role,
                           themeTokenColor("global", role, widget.palette().color(fallback)));
}
QString decibels(double value)
{
    return value <= CM::silenceDb ? QString::fromUtf8("−∞")
                                  : QString::number(value, 'f', 1) + " dB";
}
void focusOutline(QPainter &painter, QWidget &widget, const QString &scope)
{
    if (!widget.hasFocus())
        return;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(color(widget, scope, "border-focus", QPalette::Highlight));
    painter.drawRoundedRect(QRectF(widget.rect()).adjusted(1, 1, -1, -1), 4, 4);
}
} // namespace

ConsoleKnob::ConsoleKnob(QString scope, NumericControlSpec spec, bool logarithmic, QWidget *parent)
    : QWidget(parent), scope_(std::move(scope)), spec_(spec), logarithmic_(logarithmic),
      value_(spec.defaultValue)
{
    setFixedSize(82, 64);
    setFocusPolicy(Qt::StrongFocus);
}
void ConsoleKnob::setValue(double value)
{
    value_ = std::clamp(value, spec_.minimum, spec_.maximum);
    update();
}
void ConsoleKnob::setUnit(const QString &unit)
{
    unit_ = unit;
    update();
}
void ConsoleKnob::setPanMode(bool enabled)
{
    panMode_ = enabled;
    update();
}
void ConsoleKnob::onChanged(std::function<void(double)> callback)
{
    changed_ = std::move(callback);
}
double ConsoleKnob::normalized(double value) const
{
    if (logarithmic_)
        return std::log(value / spec_.minimum) / std::log(spec_.maximum / spec_.minimum);
    return (value - spec_.minimum) / (spec_.maximum - spec_.minimum);
}
double ConsoleKnob::fromNormalized(double value) const
{
    value = std::clamp(value, 0.0, 1.0);
    return logarithmic_ ? spec_.minimum * std::pow(spec_.maximum / spec_.minimum, value)
                        : spec_.minimum + value * (spec_.maximum - spec_.minimum);
}
void ConsoleKnob::apply(double value)
{
    setValue(value);
    if (changed_)
        changed_(value_);
}
QRect ConsoleKnob::readout() const
{
    return {0, height() - 20, width(), 20};
}
void ConsoleKnob::edit()
{
    if (panMode_)
        editNumericValue(this, readout(), value_ * 100, {-100, 100, 0, 0},
                         [this](double percent) { apply(percent / 100); });
    else
        editNumericValue(this, readout(), value_, spec_, [this](double value) { apply(value); });
}
void ConsoleKnob::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Palette colors;
    colors.background = color(*this, scope_, "control-knob-background-base", QPalette::Base);
    colors.border = color(*this, scope_, "control-knob-track", QPalette::Mid);
    colors.accent = color(*this, scope_, "control-knob-indicator-normal", QPalette::Highlight);
    colors.text = color(*this, scope_, "text-primary", QPalette::WindowText);
    QString text;
    if (panMode_)
    {
        colors.panel = color(*this, scope_, "control-knob-panel", QPalette::Base);
        drawPanKnob(painter, {width() / 2.0, 23}, value_, 21, colors);
        const int percent = qRound(std::abs(value_) * 100);
        const auto direction = percent == 0 ? "C" : (value_ < 0 ? "L" : "R");
        text = QString::fromLatin1(direction) + QString::number(percent) + '%';
    }
    else
    {
        drawKnob(painter, {width() / 2.0, 23}, normalized(value_), 21, colors);
        text = spec_.minimum == CM::silenceDb && value_ <= CM::silenceDb
                   ? QString::fromUtf8("−∞")
                   : QString::number(value_, 'f', spec_.decimals) + unit_;
    }
    drawText(painter, readout(), text, font(), colors.text);
    focusOutline(painter, *this, scope_);
}
void ConsoleKnob::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    if (readout().contains(event->position().toPoint()))
        edit();
    else
    {
        dragging_ = true;
        dragStart_ = event->position().toPoint();
        dragValue_ = normalized(value_);
    }
}
void ConsoleKnob::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_)
        apply(fromNormalized(dragValue_ + (dragStart_.y() - event->position().y()) / 150.0));
}
void ConsoleKnob::mouseReleaseEvent(QMouseEvent *)
{
    dragging_ = false;
}
void ConsoleKnob::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        resetNumericValue(spec_, [this](double value) { apply(value); });
}
void ConsoleKnob::wheelEvent(QWheelEvent *event)
{
    if (hasFocus())
        apply(fromNormalized(normalized(value_) + event->angleDelta().y() / 12000.0));
    else
        event->ignore();
}
void ConsoleKnob::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Right)
        apply(fromNormalized(normalized(value_) + 0.01));
    else if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Left)
        apply(fromNormalized(normalized(value_) - 0.01));
    else if (event->key() == Qt::Key_Home)
        apply(spec_.defaultValue);
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        edit();
    else
        QWidget::keyPressEvent(event);
}

ConsoleFader::ConsoleFader(QString scope, QString role, int channels, QWidget *parent)
    : QWidget(parent), scope_(std::move(scope)), role_(std::move(role)),
      channels_(std::clamp(channels, 1, 2))
{
    setMinimumSize(128, 192);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void ConsoleFader::setValue(double db)
{
    value_ = db;
    update();
}
void ConsoleFader::onChanged(std::function<void(double)> callback)
{
    changed_ = std::move(callback);
}
void ConsoleFader::setLevels(const std::array<float, 2> &peaks, double elapsed, double hold)
{
    for (int side = 0; side < channels_; ++side)
    {
        const double peak = std::isfinite(peaks[side]) ? std::abs(peaks[side]) : 0;
        meters_[side].advance(peak > 0 ? 20 * std::log10(peak) : -72, elapsed, hold);
    }
    update();
}
double ConsoleFader::displayedLevelDb(int channel) const noexcept
{
    return channel >= 0 && channel < channels_ ? meters_[channel].levelDb() : -72;
}
QRectF ConsoleFader::track() const
{
    return {28, 22, 6, static_cast<double>(height() - 62)};
}
void ConsoleFader::apply(double db)
{
    setValue(std::clamp(db, CM::silenceDb, 12.0));
    if (changed_)
        changed_(value_);
}
void ConsoleFader::applyPosition(double y)
{
    apply(CM::faderDb(std::clamp((y - track().top()) / track().height(), 0.0, 1.0)));
}
void ConsoleFader::edit()
{
    editNumericValue(this, {0, height() - 25, width(), 24}, value_, {CM::silenceDb, 12, 0, 1},
                     [this](double db) { apply(db); });
}
void ConsoleFader::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    FaderStyle fader;
    const auto role = "fader-" + role_ + '-';
    fader.accent = color(*this, scope_, role + "thumb", QPalette::Button);
    fader.track = color(*this, scope_, role + "track", QPalette::Base);
    fader.grip = color(*this, scope_, role + "grip", QPalette::WindowText);
    fader.thumbHeight = 30;
    drawFader(painter, track(), CM::faderPosition(value_), fader);
    MeterStyle meter;
    meter.vertical = true;
    meter.tickFontPixelSize = fontMetrics().height() - 3;
    const char *zones[]{"low", "mid", "high"};
    for (std::size_t index = 0; index < meter.zones.size(); ++index)
    {
        const auto zone = QStringLiteral("meter-level-zone-%1-").arg(zones[index]);
        meter.zones[index].active = color(*this, scope_, zone + "active", QPalette::Highlight);
        meter.zones[index].inactive = color(*this, scope_, zone + "inactive", QPalette::Base);
    }
    meter.border = color(*this, scope_, "meter-level-border", QPalette::Mid);
    meter.peakColor = color(*this, scope_, "meter-level-peak", QPalette::WindowText);
    meter.labelColor = color(*this, scope_, "meter-level-label", QPalette::WindowText);
    meter.tickColor = color(*this, scope_, "meter-level-tick", QPalette::Mid);
    for (int side = 0; side < channels_; ++side)
        drawMeter(painter, {55.0 + side * 14, track().top(), 12, track().height()},
                  meters_[side].levelDb(), meters_[side].peakDb(), meter);
    for (int db : {-72, -60, -48, -36, -24, -12, 0, 12})
        meter.ticks.push_back(
            {double(db), db > 0 ? "+" + QString::number(db) : QString::number(db)});
    drawMeterScale(painter, {84, track().top(), 36, track().height()}, meter);
    drawText(painter, {0, double(height() - 25), double(width()), 24}, decibels(value_), font(),
             color(*this, scope_, "text-primary", QPalette::WindowText));
    focusOutline(painter, *this, scope_);
}
void ConsoleFader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    if (event->position().y() >= height() - 25)
        edit();
    else if (event->position().x() < 54)
    {
        dragging_ = true;
        applyPosition(event->position().y());
    }
}
void ConsoleFader::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_)
        applyPosition(event->position().y());
}
void ConsoleFader::mouseReleaseEvent(QMouseEvent *)
{
    dragging_ = false;
}
void ConsoleFader::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        apply(0);
}
void ConsoleFader::wheelEvent(QWheelEvent *event)
{
    if (hasFocus())
        apply(value_ <= CM::silenceDb ? -72 : value_ + event->angleDelta().y() / 240.0);
    else
        event->ignore();
}
void ConsoleFader::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Right)
        apply(value_ <= CM::silenceDb ? -72 : value_ + 0.5);
    else if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Left)
        apply(value_ <= -72 ? CM::silenceDb : value_ - 0.5);
    else if (event->key() == Qt::Key_Home)
        apply(0);
    else if (event->key() == Qt::Key_End)
        apply(CM::silenceDb);
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        edit();
    else
        QWidget::keyPressEvent(event);
}

ConsoleSeparator::ConsoleSeparator(QString scope, QWidget *parent)
    : QWidget(parent), scope_(std::move(scope))
{
    setFixedWidth(1);
}
void ConsoleSeparator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), color(*this, scope_, "channel-divider", QPalette::Mid));
}

void bindConsoleTheme(QWidget *root, const QString &scope)
{
    const auto apply = [root, scope] {
        Palette colors;
        colors.background = color(*root, scope, "surface-window", QPalette::Window);
        colors.panel = color(*root, scope, "surface-panel", QPalette::Base);
        colors.text = color(*root, scope, "text-primary", QPalette::WindowText);
        colors.border = color(*root, scope, "border-normal", QPalette::Mid);
        colors.accent = color(*root, scope, "accent-primary", QPalette::Highlight);
        applyPalette(root, colors);
        root->setAutoFillBackground(true);
        for (auto *widget : root->findChildren<QWidget *>())
        {
            applyPalette(widget, colors);
            widget->update();
        }
    };
    if (auto *core = ThemeManager::installedThemeCore())
        QObject::connect(core, &ThemeCore::themeChanged, root,
                         [root, apply] { QTimer::singleShot(0, root, apply); });
    QTimer::singleShot(0, root, apply);
}
} // namespace WinJACKNexus::UI::Controls
