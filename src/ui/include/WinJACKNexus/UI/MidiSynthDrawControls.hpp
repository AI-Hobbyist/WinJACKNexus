// Layout adapters for the existing public knob, numeric editor and meter APIs.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <WinJACKNexus/UI/Controls.hpp>
#include <algorithm>
#include <array>

namespace WinJACKNexus::UI
{
// A layout surface owns the row rules so child surfaces cannot cover them.
class MidiSynthChannelPanel final : public QWidget
{
  public:
    std::array<QWidget *, 16> rows{};
    QWidget *firstColumn = nullptr;
    QWidget *lastColumn = nullptr;
    explicit MidiSynthChannelPanel(QWidget *parent) : QWidget(parent)
    {
        setProperty("midiToolsPassiveSurface", true);
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        if (!firstColumn || !lastColumn)
        {
            return;
        }
        QPainter painter(this);
        painter.setPen(Controls::themeTokenColor("module-midi-tools-sf2-loader", "border-normal",
                                                 palette().color(QPalette::Mid)));
        for (unsigned channel = 0; channel < 15; ++channel)
        {
            const int bottom = rows[channel]->geometry().bottom();
            const int next = rows[channel + 1]->geometry().top();
            painter.drawLine(firstColumn->geometry().left(), (bottom + next) / 2,
                             lastColumn->geometry().right(), (bottom + next) / 2);
        }
    }
};
class MidiSynthGain final : public QWidget
{
    double value_ = 0;
    double dragValue_ = 0;
    QPoint dragStart_;
    bool dragging_ = false;
    bool master_ = false;
    std::function<void(double)> changed_;
    const Controls::NumericControlSpec spec_{-60, 12, 0, 1};

    int diameter() const
    {
        return master_ ? (std::min)(width(), height() - fontMetrics().height() - 8) : height();
    }
    QRect readout() const
    {
        return master_ ? QRect(0, diameter(), width(), height() - diameter())
                       : QRect(height(), 0, width() - height(), height());
    }
    void apply(double value)
    {
        value = std::clamp(value, spec_.minimum, spec_.maximum);
        if (value != value_)
        {
            setValue(value);
            changed_(value);
        }
    }
    void edit()
    {
        Controls::editNumericValue(this, readout(), value_, spec_,
                                   [this](double value) { apply(value); });
    }

  public:
    explicit MidiSynthGain(QWidget *parent, bool master = false) : QWidget(parent), master_(master)
    {
        setProperty("midiToolsPassiveSurface", true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(sizeHint());
    }
    QSize sizeHint() const override
    {
        const int diameter = (std::max)(master_ ? 72 : 40, fontMetrics().height() + 24);
        if (master_)
        {
            return {diameter, diameter + fontMetrics().height() + 8};
        }
        return {diameter + fontMetrics().horizontalAdvance("-60.0 dB") + 12, diameter};
    }
    void onChanged(std::function<void(double)> changed)
    {
        changed_ = std::move(changed);
    }
    void setValue(double value)
    {
        value_ = value;
        update();
    }
    double value() const
    {
        return value_;
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        Controls::Palette colors;
        colors.background = Controls::themeTokenColor("module-midi-tools-sf2-loader",
                                                      "control-knob-background-base",
                                                      palette().color(QPalette::Base));
        colors.border = Controls::themeTokenColor(
            "module-midi-tools-sf2-loader", "control-knob-track", palette().color(QPalette::Mid));
        colors.accent = Controls::themeTokenColor("module-midi-tools-sf2-loader",
                                                  "control-knob-indicator-normal",
                                                  palette().color(QPalette::Highlight));
        colors.text = Controls::themeTokenColor("module-midi-tools-sf2-loader", "text-primary",
                                                palette().color(QPalette::WindowText));
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int radius = (diameter() - 8) / 2;
        Controls::drawKnob(painter, QPointF(diameter() / 2.0, diameter() / 2.0),
                           (value_ - spec_.minimum) / (spec_.maximum - spec_.minimum), radius,
                           colors);
        Controls::drawText(painter, readout(), QString::number(value_, 'f', 1) + " dB", font(),
                           colors.text);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }
        setFocus();
        if (readout().contains(event->position().toPoint()))
        {
            edit();
            return;
        }
        dragging_ = true;
        dragStart_ = event->position().toPoint();
        dragValue_ = value_;
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging_)
        {
            apply(dragValue_ + (dragStart_.y() - event->position().y()) * 0.5);
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        dragging_ = false;
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !readout().contains(event->position().toPoint()))
        {
            Controls::resetNumericValue(spec_, [this](double value) { apply(value); });
        }
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Right)
        {
            apply(value_ + 0.5);
        }
        else if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Left)
        {
            apply(value_ - 0.5);
        }
        else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            edit();
        }
        else
        {
            QWidget::keyPressEvent(event);
        }
    }
};

class MidiSynthMeter final : public QWidget
{
    double level_ = -72;
    double peak_ = -72;

  public:
    explicit MidiSynthMeter(QWidget *parent) : QWidget(parent)
    {
        setProperty("midiToolsPassiveSurface", true);
        setMinimumSize(fontMetrics().horizontalAdvance("Channel output level"),
                       fontMetrics().height());
    }
    void setLevels(double level, double peak)
    {
        level_ = level;
        peak_ = peak;
        update();
    }
    double levelDb() const
    {
        return level_;
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        Controls::MeterStyle style;
        const QString scope = "module-midi-tools-sf2-loader";
        const char *names[]{"low", "mid", "high"};
        for (unsigned index = 0; index < style.zones.size(); ++index)
        {
            const auto role = QStringLiteral("meter-level-zone-%1-").arg(names[index]);
            style.zones[index].active =
                Controls::themeTokenColor(scope, role + "active", style.zones[index].active);
            style.zones[index].inactive =
                Controls::themeTokenColor(scope, role + "inactive", style.zones[index].inactive);
        }
        style.border = Controls::themeTokenColor(scope, "meter-level-border", style.border);
        style.peakColor = Controls::themeTokenColor(scope, "meter-level-peak", style.peakColor);
        style.labelColor = Controls::themeTokenColor(scope, "meter-level-label", style.labelColor);
        style.tickColor = Controls::themeTokenColor(scope, "meter-level-tick", style.tickColor);
        style.ticksAbove = true;
        for (int db : {-72, -60, -48, -36, -24, -12, 0, 12})
        {
            style.ticks.push_back(
                {double(db), db > 0 ? "+" + QString::number(db) : QString::number(db)});
        }
        QPainter painter(this);
        const qreal barHeight = (height() - 4) / 2.0;
        const qreal boundsHeight = barHeight + style.tickBandPixels;
        const QRectF bounds(15, (height() - boundsHeight) / 2, width() - 31, boundsHeight);
        Controls::drawMeter(painter, bounds, level_, peak_, style);
    }
};
} // namespace WinJACKNexus::UI
