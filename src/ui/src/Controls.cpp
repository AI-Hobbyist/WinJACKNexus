/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QCompleter>
#include <QCheckBox>
#include <QLabel>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QToolTip>
#include <QtMath>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMainWindow>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QDoubleValidator>
#include <QDrag>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QStyledItemDelegate>
#include <QSplitterHandle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI::Controls {
namespace {
class PaintedTextTooltips final : public QObject {
public:
    struct Region { QRectF bounds; QString text; };
    explicit PaintedTextTooltips(QWidget* widget) : QObject(widget) {
        setObjectName(QStringLiteral("wjnPaintedTextTooltips"));
        widget->installEventFilter(this);
        if (auto* manager = I18nManager::installed()) manager->bind(this, [this, widget] {
            if (!shown_ || !QToolTip::isVisible() || QToolTip::text() != shownText_) return;
            widget->repaint();
            refreshTooltip(widget);
        });
    }
    std::vector<Region> regions;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Paint) {
            regions.clear();
        } else if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
                if (!it->bounds.contains(help->pos())) continue;
                shown_ = true; position_ = help->pos(); globalPosition_ = help->globalPos();
                shownText_ = Qt::convertFromPlainText(it->text);
                QToolTip::showText(help->globalPos(), shownText_,
                                  static_cast<QWidget*>(watched), it->bounds.toAlignedRect());
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
private:
    bool shown_ = false;
    QString shownText_;
    QPoint position_, globalPosition_;
    void refreshTooltip(QWidget* widget) {
        for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
            if (!it->bounds.contains(position_)) continue;
            shownText_ = Qt::convertFromPlainText(it->text);
            QToolTip::showText(globalPosition_, shownText_, widget, it->bounds.toAlignedRect());
            return;
        }
        shown_ = false;
        QToolTip::hideText();
    }
};

class NumericValueEditor final : public QLineEdit {
public:
    NumericValueEditor(const NumericControlSpec& spec, std::function<void(double)> apply,
                       QWidget* owner)
        : QLineEdit(owner), apply_(std::move(apply)) {
        setObjectName(QStringLiteral("numericValueEditor"));
        setValidator(new QDoubleValidator(spec.minimum, spec.maximum, spec.decimals, this));
        setAlignment(Qt::AlignCenter);
        const auto background = themeTokenColor("global", "control-input-surface-normal",
                                                QColor("#102033"));
        const auto foreground = themeTokenColor("global", "control-input-text-normal",
                                                QColor("#eff6ff"));
        const auto border = themeTokenColor("global", "control-input-border-focus",
                                            QColor("#31c6ff"));
        setStyleSheet(QStringLiteral("QLineEdit {background:%1;color:%2;"
                                     "border:1px solid %3;border-radius:4px;}")
                          .arg(background.name(), foreground.name(), border.name()));
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) { closeEditor(); return; }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (hasAcceptableInput()) {
                bool valid = false;
                const double value = locale().toDouble(text(), &valid);
                if (valid && std::isfinite(value)) {
                    auto apply = apply_;
                    closeEditor();
                    if (apply) apply(value);
                }
            }
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        closeEditor();
    }
private:
    void closeEditor() { hide(); deleteLater(); }
    std::function<void(double)> apply_;
};
}

QLineEdit* editNumericValue(QWidget* owner, const QRect& readout, double currentValue,
                            const NumericControlSpec& spec, std::function<void(double)> apply) {
    if (!owner || readout.isEmpty() || !std::isfinite(currentValue)
        || !std::isfinite(spec.minimum) || !std::isfinite(spec.maximum)
        || spec.maximum <= spec.minimum || spec.decimals < 0 || spec.decimals > 9) return nullptr;
    if (auto* existing = owner->findChild<QLineEdit*>(QStringLiteral("numericValueEditor"))) {
        existing->hide();
        existing->deleteLater();
    }
    auto* editor = new NumericValueEditor(spec, std::move(apply), owner);
    editor->setGeometry(readout);
    editor->setText(editor->locale().toString(currentValue, 'f', spec.decimals));
    editor->show();
    editor->raise();
    editor->setFocus(Qt::MouseFocusReason);
    editor->selectAll();
    return editor;
}

void resetNumericValue(const NumericControlSpec& spec,
                       const std::function<void(double)>& apply) {
    if (apply && std::isfinite(spec.defaultValue))
        apply(std::clamp(spec.defaultValue, spec.minimum, spec.maximum));
}

void applyPalette(QWidget* widget, const Palette& colors) {
    if (!widget) return;
    auto palette = widget->palette();
    palette.setColor(QPalette::Window, colors.background);
    palette.setColor(QPalette::Base, colors.panel);
    palette.setColor(QPalette::WindowText, colors.text);
    palette.setColor(QPalette::Text, colors.text);
    palette.setColor(QPalette::Highlight, colors.accent);
    widget->setPalette(palette);
    widget->setProperty("fxAccent", colors.accent);
    widget->setProperty("fxBorder", colors.border);
    widget->setProperty("fxBackground", colors.background);
    widget->setProperty("fxPanel", colors.panel);
    widget->setProperty("fxText", colors.text);
    if (widget->metaObject()->indexOfProperty("accentColor") >= 0)
        widget->setProperty("accentColor", colors.accent);
    if (widget->metaObject()->indexOfProperty("surfaceColor") >= 0)
        widget->setProperty("surfaceColor", colors.panel);
    if (widget->metaObject()->indexOfProperty("borderColor") >= 0)
        widget->setProperty("borderColor", colors.border);
    if (widget->metaObject()->indexOfProperty("textColor") >= 0)
        widget->setProperty("textColor", colors.text);
    widget->update();
}

QColor themeColor(const QWidget& widget, const char* key, const QColor& fallback) {
    const auto value = widget.property(key);
    const auto color = value.value<QColor>();
    return color.isValid() ? color : fallback;
}

QColor themeTokenColor(const QString& scope, const QString& role, const QColor& fallback) {
    const auto* core = ThemeManager::installedThemeCore();
    if (!core) return fallback;
    const auto snapshot = core->snapshot();
    if (!snapshot) return fallback;
    const auto token = QStringLiteral("--wjn-theme-") + scope + '-' + role;
    if (!snapshot->hasValue(token)) return fallback;
    const auto value = snapshot->value(token);
    if (value == QStringLiteral("transparent")) return QColor(0, 0, 0, 0);
    if (value.startsWith(QStringLiteral("rgb(")) || value.startsWith(QStringLiteral("rgba("))) {
        const bool alpha = value.startsWith(QStringLiteral("rgba("));
        const auto fields = value.mid(alpha ? 5 : 4, value.size() - (alpha ? 6 : 5)).split(',');
        if (fields.size() != (alpha ? 4 : 3)) return fallback;
        const auto opacity = alpha ? fields[3].trimmed().toDouble() : 1.0;
        const QColor parsed(fields[0].trimmed().toInt(), fields[1].trimmed().toInt(),
                            fields[2].trimmed().toInt(), qRound(opacity * 255.0));
        return parsed.isValid() ? parsed : fallback;
    }
    const QColor parsed(value);
    return parsed.isValid() ? parsed : fallback;
}

void drawSurface(QPainter& painter, const QRectF& bounds, const QBrush& fill,
                 const QColor& border, qreal radius, qreal borderWidth) {
    painter.setPen(border.isValid() && border.alpha() > 0
        ? QPen(border, borderWidth) : QPen(Qt::NoPen));
    painter.setBrush(fill);
    painter.drawRoundedRect(bounds, radius, radius);
}

void drawText(QPainter& painter, const QRectF& bounds, const QString& value,
              const QFont& font, const QColor& color, Qt::Alignment alignment) {
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(bounds, alignment, value);
}

void drawElidedText(QPainter& painter, const QRectF& bounds, const QString& value,
                    const QFont& font, const QColor& color, Qt::Alignment alignment) {
    const QFontMetricsF metrics(font, painter.device());
    auto lines = value.split(QLatin1Char('\n'));
    bool clipped = false;
    for (auto& line : lines) {
        const auto displayed = metrics.elidedText(line, Qt::ElideRight, std::max(0.0, bounds.width()));
        clipped |= displayed != line;
        line = displayed;
    }
    clipped |= metrics.boundingRect(bounds, int(alignment), value).height() > bounds.height();
    auto* widget = dynamic_cast<QWidget*>(painter.device());
    if (widget) {
        auto* holder = widget->findChild<QObject*>(QStringLiteral("wjnPaintedTextTooltips"), Qt::FindDirectChildrenOnly);
        auto* tooltips = dynamic_cast<PaintedTextTooltips*>(holder);
        if (!tooltips) tooltips = new PaintedTextTooltips(widget);
        if (clipped) {
            // combinedTransform includes logical viewport mapping, without the
            // device pixel scaling that would move a QWidget's hover hit area.
            const auto hit = painter.combinedTransform().mapRect(bounds).intersected(widget->rect());
            if (!hit.isEmpty()) tooltips->regions.push_back({hit, value});
        }
    }
    drawText(painter, bounds, lines.join(QLatin1Char('\n')), font, color, alignment);
}

void drawMarker(QPainter& painter, const QPointF& center, qreal radius,
                const QBrush& fill, const QColor& border, qreal borderWidth) {
    if (radius <= 0) return;
    painter.setPen(border.isValid() && border.alpha() > 0
        ? QPen(border, borderWidth) : QPen(Qt::NoPen));
    painter.setBrush(fill);
    painter.drawEllipse(center, radius, radius);
}

void drawToggle(QPainter& painter, const QRectF& bounds, bool on,
                const QColor& accent, const QColor& offTrack) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? accent : offTrack);
    painter.drawRoundedRect(bounds, bounds.height() / 2, bounds.height() / 2);
    painter.setBrush(Qt::white);
    const auto diameter = bounds.height() - 4;
    painter.drawEllipse(QRectF(on ? bounds.right() - diameter - 2 : bounds.left() + 2,
                               bounds.top() + 2, diameter, diameter));
}

ToggleButton::ToggleButton(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setFixedSize(44, 26);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);
}

void ToggleButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawToggle(painter, QRectF(rect()).adjusted(1, 2, -1, -2), isChecked(),
               themeColor(*this, "fxAccent", themeTokenColor("global",
                   "control-toggle-indicator-selected", QColor("#187ee8"))),
               themeTokenColor("global", "control-toggle-surface-normal", QColor("#38516d")));
}

Palette paletteFor(const QWidget& widget, const Palette& defaults) {
    Palette result = defaults;
    result.background = themeColor(widget, "fxBackground", defaults.background);
    result.panel = themeColor(widget, "fxPanel", defaults.panel);
    result.text = themeColor(widget, "fxText", defaults.text);
    result.accent = themeColor(widget, "fxAccent", defaults.accent);
    result.border = themeColor(widget, "fxBorder", defaults.border);
    return result;
}

void drawStyledButton(QPainter& painter, const QRectF& bounds, const QString& caption,
                      bool selected, const ButtonStyle& style) {
    painter.save();
    painter.setPen(selected ? style.activeBorder : style.inactiveBorder);
    painter.setBrush(selected ? style.activeFill : style.inactiveFill);
    painter.drawRoundedRect(bounds, style.cornerRadius, style.cornerRadius);
    auto font = painter.font();
    font.setPixelSize(style.fontSize);
    font.setBold(style.bold);
    painter.setFont(font);
    painter.setPen(selected ? style.activeText : style.inactiveText);
    painter.drawText(bounds, Qt::AlignCenter, caption);
    painter.restore();
}

void drawButton(QPainter& painter, const QRectF& bounds, const QString& caption,
                bool selected, const Palette& colors, int fontSize) {
    painter.setPen(QPen(selected ? colors.accent : colors.border, selected ? 1.5 : 1));
    painter.setBrush(selected ? colors.panel.lighter(125) : colors.panel);
    painter.drawRoundedRect(bounds, 6, 6);
    auto font = painter.font(); font.setPixelSize(fontSize); painter.setFont(font);
    painter.setPen(selected ? colors.accent : colors.text);
    painter.drawText(bounds, Qt::AlignCenter, caption);
}

void drawKnob(QPainter& painter, const QPointF& center, qreal normalized,
              int radius, const Palette& colors) {
    normalized = qBound<qreal>(0, normalized, 1);
    painter.setPen(QPen(colors.border, 4));
    painter.setBrush(colors.background.darker(135));
    painter.drawEllipse(center, radius, radius);
    const QRectF arc(center.x()-radius+5, center.y()-radius+5, 2*radius-10, 2*radius-10);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colors.accent, 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arc, 225*16, -qRound(270*normalized)*16);
    const qreal angle = (225.0 - 270.0*normalized)*3.141592653589793/180.0;
    painter.drawLine(center, center + QPointF(std::cos(angle)*(radius-9), -std::sin(angle)*(radius-9)));
}

void drawPanKnob(QPainter& painter, const QPointF& center, qreal pan,
                 qreal radius, const Palette& colors) {
    pan=qBound<qreal>(-1,pan,1);
    const QRectF ring(center.x()-radius,center.y()-radius,2*radius,2*radius);
    const qreal stroke=std::max<qreal>(5,radius*.06);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colors.border,stroke,Qt::SolidLine,Qt::RoundCap));
    painter.drawArc(ring,225*16,-270*16);
    if(std::abs(pan)>.0001){
        painter.setPen(QPen(colors.accent,stroke,Qt::SolidLine,Qt::RoundCap));
        painter.drawArc(ring,90*16,qRound(-pan*135*16));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.panel);
    painter.drawEllipse(center,radius*.78,radius*.78);
    const qreal angle=(270+pan*135)*3.14159265358979323846/180.0;
    const QPointF direction(std::cos(angle),std::sin(angle));
    painter.setPen(QPen(colors.text,std::max<qreal>(3,radius*.035),Qt::SolidLine,Qt::RoundCap));
    painter.drawLine(center+direction*(radius*.44),center+direction*(radius*.76));
}

void drawSlider(QPainter& painter, const QRectF& bounds, qreal normalized,
                bool vertical, const Palette& colors) {
    normalized = qBound<qreal>(0, normalized, 1);
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.panel);
    painter.drawRoundedRect(bounds, 4, 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.accent);
    if (vertical) {
        const qreal y = bounds.bottom() - bounds.height()*normalized;
        painter.drawRoundedRect(QRectF(bounds.left()+2, y, bounds.width()-4, bounds.bottom()-y-2), 3, 3);
        painter.setBrush(colors.text);
        painter.drawRoundedRect(QRectF(bounds.left()-3, y-3, bounds.width()+6, 6), 3, 3);
    } else {
        const qreal x = bounds.left() + bounds.width()*normalized;
        painter.drawRoundedRect(QRectF(bounds.left()+2, bounds.top()+2, x-bounds.left()-2, bounds.height()-4), 3, 3);
        painter.setBrush(colors.text);
        painter.drawRoundedRect(QRectF(x-3, bounds.top()-3, 6, bounds.height()+6), 3, 3);
    }
}

void drawFader(QPainter& painter, const QRectF& track,
               qreal normalizedFromTop, const FaderStyle& style) {
    if (track.isEmpty() || style.thumbWidth <= 0 || style.thumbHeight <= 0) return;
    const qreal thumbY = track.top() + track.height() * std::clamp(normalizedFromTop, 0.0, 1.0);
    const QRectF thumb(track.center().x() - style.thumbWidth / 2.0,
                       thumbY - style.thumbHeight / 2.0,
                       style.thumbWidth, style.thumbHeight);
    QLinearGradient gradient(thumb.topLeft(), thumb.bottomRight());
    gradient.setColorAt(0, style.accent.lighter(130));
    gradient.setColorAt(1, style.accent.darker(125));
    painter.save();
    painter.setPen(QPen(style.track, 7, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(track.center().x(), track.top(), track.center().x(), track.bottom());
    painter.setPen(QPen(style.accent, 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(track.center().x(), track.bottom(), track.center().x(), thumbY);
    painter.setPen(QPen(style.accent.lighter(135), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(thumb, style.cornerRadius, style.cornerRadius);
    QColor grip = style.grip;
    grip.setAlpha(130);
    painter.setPen(QPen(grip, 1));
    painter.drawLine(thumb.left() + 3, thumb.center().y(),
                     thumb.right() - 3, thumb.center().y());
    painter.restore();
}

void drawChart(QPainter& painter, const QRectF& bounds, int columns,
               int rows, const Palette& colors) {
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.background);
    painter.drawRoundedRect(bounds, 5, 5);
    painter.save(); painter.setClipRect(bounds.adjusted(1, 1, -1, -1));
    painter.setPen(QPen(colors.border.darker(125), 1));
    for (int i=1; i<columns; ++i) {
        const qreal x=bounds.left()+bounds.width()*i/columns;
        painter.drawLine(QPointF(x,bounds.top()),QPointF(x,bounds.bottom()));
    }
    for (int i=1; i<rows; ++i) {
        const qreal y=bounds.top()+bounds.height()*i/rows;
        painter.drawLine(QPointF(bounds.left(),y),QPointF(bounds.right(),y));
    }
    painter.restore();
}

void drawChartGrid(QPainter& painter, const QRectF& bounds, const ChartGridStyle& style) {
    if (bounds.isEmpty()) return;
    painter.save();
    painter.setClipRect(bounds);
    if (style.background.isValid()) painter.fillRect(bounds, style.background);
    painter.setPen(QPen(style.grid, style.lineWidth, style.penStyle));
    painter.setBrush(Qt::NoBrush);
    const auto vertical = [&](qreal fraction) {
        if (fraction < 0.0 || fraction > 1.0) return;
        const auto x = bounds.left() + fraction * bounds.width();
        painter.drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
    };
    const auto horizontal = [&](qreal fraction) {
        if (fraction < 0.0 || fraction > 1.0) return;
        const auto y = bounds.top() + fraction * bounds.height();
        painter.drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
    };
    if (!style.verticalFractions.empty()) {
        for (const auto fraction : style.verticalFractions) vertical(fraction);
    } else if (style.columns > 0) {
        for (int index = 0; index <= style.columns; ++index)
            vertical(static_cast<qreal>(index) / style.columns);
    }
    if (!style.horizontalFractions.empty()) {
        for (const auto fraction : style.horizontalFractions) horizontal(fraction);
    } else if (style.rows > 0) {
        for (int index = 0; index <= style.rows; ++index)
            horizontal(static_cast<qreal>(index) / style.rows);
    }
    painter.restore();
}

void drawDropdown(QPainter& painter, const QRectF& bounds, const QString& caption,
                  const Palette& colors, int fontSize, bool manualFilter) {
    painter.setPen(QPen(colors.border, 1)); painter.setBrush(colors.panel);
    painter.drawRoundedRect(bounds, 5, 5);
    auto font=painter.font(); font.setPixelSize(fontSize); painter.setFont(font);
    painter.setPen(colors.text);
    const auto textBounds = bounds.adjusted(manualFilter ? 28 : 12,0,-29,0);
    painter.drawText(textBounds, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(font).elidedText(caption,Qt::ElideRight,std::max(0,qFloor(textBounds.width()))));
    if (manualFilter) {
        painter.setPen(QPen(colors.accent, 1.5));
        const QPointF search(bounds.left()+15, bounds.center().y()-2);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(search, 4, 4);
        painter.drawLine(search+QPointF(3,3), search+QPointF(7,7));
    }
    const QPointF tip(bounds.right()-15,bounds.center().y()+3);
    painter.setPen(QPen(colors.accent, 1.8));
    painter.drawLine(tip+QPointF(-5,-5),tip);
    painter.drawLine(tip,tip+QPointF(5,-5));
}

void drawLevelMeter(QPainter& painter, const QRectF& bounds,
                    double levelDb, const MeterColors& colors) {
    MeterStyle style;
    style.minimumDb = -60.0;
    style.maximumDb = 12.0;
    style.zones = {
        {-12.0, colors.low, colors.inactiveLow},
        {0.0, colors.middle, colors.inactiveMiddle},
        {12.0, colors.high, colors.inactiveHigh}
    };
    style.border = colors.border;
    drawMeter(painter, bounds, levelDb, levelDb, style);
}

namespace {
void drawMeterTicks(QPainter& painter, const QRectF& bounds, const QRectF& track,
                    const MeterStyle& style)
{
    const qreal verticalTickBand = style.vertical && !style.ticksLeft
        ? std::max<qreal>(31.0, style.tickBandPixels) : style.tickBandPixels;
    const auto coordinate = [&](double value) {
        const auto fraction = (std::clamp(value, style.minimumDb, style.maximumDb) - style.minimumDb)
            / (style.maximumDb - style.minimumDb);
        return style.vertical ? track.bottom() - fraction * track.height()
                              : track.left() + fraction * track.width();
    };
    painter.save();
    auto tickFont = painter.font();
    tickFont.setPixelSize(std::max(1, style.tickFontPixelSize));
    tickFont.setBold(false);
    painter.setFont(tickFont);
    for (const auto& tick : style.ticks) {
        const auto position = coordinate(tick.valueDb);
        if (style.vertical) {
            painter.setPen(style.tickColor);
            if (style.ticksLeft)
                painter.drawLine(QPointF(track.left() - 4, position), QPointF(track.left() - 1, position));
            else
                painter.drawLine(QPointF(track.right() + 1, position), QPointF(track.right() + 4, position));
            painter.setPen(style.labelColor);
            const QRectF labelRect = style.ticksLeft
                ? QRectF(bounds.left(), position - 7, std::max(0.0, style.tickBandPixels - 6), 14)
                : QRectF(track.right() + 5, position - 7, std::max(0.0, verticalTickBand - 5), 14);
            painter.drawText(labelRect,
                             (style.ticksLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                             tick.label);
        } else {
            painter.setPen(style.tickColor);
            const qreal labelLeft = position - 15.0;
            if (style.ticksAbove) {
                painter.drawLine(QPointF(position, track.top() - 7), QPointF(position, track.top() - 4));
                painter.setPen(style.labelColor);
                painter.drawText(QRectF(labelLeft, track.top() - style.tickBandPixels, 30, 16),
                                 Qt::AlignCenter, tick.label);
            } else {
                painter.drawLine(QPointF(position, track.bottom() + 3), QPointF(position, track.bottom() + 6));
                painter.setPen(style.labelColor);
                painter.drawText(QRectF(labelLeft, track.bottom() + 8, 30, 14),
                                 Qt::AlignCenter, tick.label);
            }
        }
    }
    painter.restore();
}
}

void drawMeterScale(QPainter& painter, const QRectF& bounds, const MeterStyle& style)
{
    if (!std::isfinite(style.minimumDb) || !std::isfinite(style.maximumDb)
        || style.maximumDb <= style.minimumDb || bounds.isEmpty()) return;
    const QRectF track = style.vertical
        ? QRectF(style.ticksLeft ? bounds.right() : bounds.left(), bounds.top(), 0, bounds.height())
        : QRectF(bounds.left(), style.ticksAbove ? bounds.bottom() : bounds.top(), bounds.width(), 0);
    drawMeterTicks(painter, bounds, track, style);
}

void drawMeter(QPainter& painter, const QRectF& bounds, double levelDb,
               std::optional<double> peakDb, const MeterStyle& style) {
    if (!std::isfinite(style.minimumDb) || !std::isfinite(style.maximumDb)
        || style.maximumDb <= style.minimumDb || bounds.isEmpty()) return;
    painter.save();
    QRectF track = bounds;
    const qreal verticalTickBand = style.vertical && !style.ticksLeft
        ? std::max<qreal>(31.0, style.tickBandPixels) : style.tickBandPixels;
    if (!style.label.isEmpty()) {
        const QRectF caption = style.vertical
            ? QRectF(bounds.left(), bounds.top(), bounds.width(), 18.0)
            : QRectF(bounds.left(), bounds.top(), 32.0, bounds.height());
        painter.setPen(style.labelColor);
        painter.drawText(caption, Qt::AlignCenter, style.label);
        if (style.vertical) track.adjust(0, 18, 0, 0);
        else track.adjust(32, 0, 0, 0);
    }
    if (!style.ticks.empty()) {
        if (style.vertical) {
            if (style.ticksLeft) track.adjust(style.tickBandPixels, 0, 0, 0);
            else track.adjust(0, 0, -verticalTickBand, 0);
        }
        else if (style.ticksAbove) track.adjust(0, style.tickBandPixels, 0, 0);
        else track.adjust(0, 0, 0, -style.tickBandPixels);
    }
    if (track.isEmpty()) { painter.restore(); return; }
    const auto fraction = [&style](double value) {
        return (std::clamp(value, style.minimumDb, style.maximumDb) - style.minimumDb)
            / (style.maximumDb - style.minimumDb);
    };
    const auto coordinate = [&](double value) {
        return style.vertical ? track.bottom() - fraction(value) * track.height()
                              : track.left() + fraction(value) * track.width();
    };
    const double origin = std::clamp(style.fillOriginDb.value_or(style.minimumDb),
                                     style.minimumDb, style.maximumDb);
    const double activeLower = std::min(origin, levelDb);
    const double activeUpper = std::max(origin, levelDb);
    double lower = style.minimumDb;
    for (const auto& zone : style.zones) {
        const double upper = std::clamp(zone.upperDb, lower, style.maximumDb);
        const double zoneActiveLower = std::clamp(activeLower, lower, upper);
        const double zoneActiveUpper = std::clamp(activeUpper, lower, upper);
        const auto zoneRect = style.vertical
            ? QRectF(track.left(), coordinate(upper), track.width(), coordinate(lower) - coordinate(upper))
            : QRectF(coordinate(lower), track.top(), coordinate(upper) - coordinate(lower), track.height());
        painter.fillRect(zoneRect, zone.inactive);
        const auto activeRect = style.vertical
            ? QRectF(track.left(), coordinate(zoneActiveUpper), track.width(),
                     coordinate(zoneActiveLower) - coordinate(zoneActiveUpper))
            : QRectF(coordinate(zoneActiveLower), track.top(),
                     coordinate(zoneActiveUpper) - coordinate(zoneActiveLower), track.height());
        painter.fillRect(activeRect, zone.active);
        lower = upper;
        if (lower >= style.maximumDb) break;
    }
    if (style.vertical && style.showTickGrid) {
        painter.setPen(QPen(style.tickColor, 1));
        for (const auto& tick : style.ticks) {
            const auto position = coordinate(tick.valueDb);
            painter.drawLine(QPointF(track.left() - 4, position), QPointF(track.right(), position));
        }
    }
    painter.setPen(QPen(style.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(track);
    if (style.showPeak && peakDb.has_value() && std::isfinite(*peakDb)) {
        const auto position = coordinate(*peakDb);
        painter.setPen(QPen(style.peakColor, 2));
        if (style.vertical)
            painter.drawLine(QPointF(track.left() + 1, position), QPointF(track.right() - 1, position));
        else
            painter.drawLine(QPointF(position, track.top() + 1), QPointF(position, track.bottom() - 1));
    }
    drawMeterTicks(painter, bounds, track, style);
    painter.restore();
}

void MeterBallistics::advance(double inputDb, double elapsedSeconds,
                              double holdSeconds) noexcept {
    if (!std::isfinite(inputDb) || !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0)
        return;
    const double target = std::clamp(inputDb, -72.0, 12.0);
    const double tau = target > levelDb_ ? .025 : .18;
    levelDb_ += (target-levelDb_)*(1.0-std::exp(-elapsedSeconds/tau));
    if (target >= peakDb_) {
        peakDb_ = target;
        heldSeconds_ = std::max(0.0, holdSeconds);
    } else if (heldSeconds_ > 0.0) {
        heldSeconds_ = std::max(0.0, heldSeconds_-elapsedSeconds);
    } else {
        peakDb_ = std::max(target, peakDb_-24.0*elapsedSeconds);
    }
}

void MeterBallistics::reset() noexcept {
    levelDb_ = peakDb_ = -72.0;
    heldSeconds_ = 0.0;
}

void drawCurvePath(QPainter& painter, const QPainterPath& strokePath,
                   const CurveStyle& style, const QPainterPath* fillPath) {
    painter.save();
    if (style.showFill && fillPath != nullptr) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(style.fillBrush.value_or(QBrush(style.fill)));
        painter.drawPath(*fillPath);
    }
    if (style.showStroke) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(style.stroke, style.width, style.penStyle,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(strokePath);
    }
    painter.restore();
}

void drawPolarCurve(QPainter& painter, QPointF center, qreal radius,
                    std::span<const float> values, const CurveStyle& style) {
    if (values.size() < 3 || !std::isfinite(style.minimum) || !std::isfinite(style.maximum)
        || style.maximum <= style.minimum || radius <= 0) return;
    constexpr double pi = 3.14159265358979323846;
    QPainterPath path;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto angle = 2.0 * pi * static_cast<double>(index) / values.size();
        const auto normalized = std::clamp((static_cast<double>(values[index]) - style.minimum)
            / (style.maximum - style.minimum), 0.0, 1.0);
        const auto point = center + QPointF(std::sin(angle), -std::cos(angle)) * radius * normalized;
        if (index == 0) path.moveTo(point);
        else path.lineTo(point);
    }
    path.closeSubpath();
    drawCurvePath(painter, path, style, &path);
}

void drawCardLabel(QPainter& painter, const QRectF& bounds,
                   const QString& title, const QString& glyph,
                   const Palette& colors) {
    painter.setPen(QPen(colors.border, 1)); painter.setBrush(colors.panel);
    painter.drawRoundedRect(bounds, 7, 7);
    const QRectF icon(bounds.left()+8, bounds.top()+6, bounds.height()-12, bounds.height()-12);
    painter.setPen(Qt::NoPen); painter.setBrush(colors.accent.darker(145));
    painter.drawRoundedRect(icon, 5, 5);
    painter.setPen(colors.text);
    painter.drawText(icon, Qt::AlignCenter, glyph);
    painter.drawText(bounds.adjusted(icon.right()-bounds.left()+12, 0, -8, 0),
                     Qt::AlignVCenter | Qt::AlignLeft, title);
}

bool ControlRegistry::registerFactory(const QString& key, Factory factory) {
    if (key.isEmpty() || !factory) return false;
    factories_.insert(key, std::move(factory));
    return true;
}

QWidget* ControlRegistry::create(const QString& key, QWidget* parent) const {
    if (const auto it=factories_.constFind(key); it != factories_.cend()) return (*it)(parent);
    if (key == QStringLiteral("button")) return new ActionButton({}, parent);
    if (key == QStringLiteral("slider")) return new RangeSlider(Qt::Horizontal, parent);
    if (key == QStringLiteral("dropdown")) return new ChoiceField(parent);
    if (key == QStringLiteral("toggle")) return new ToggleButton(parent);
    if (key == QStringLiteral("label")) return new TextLabel(parent);
    if (key == QStringLiteral("card")) return new QFrame(parent);
    return nullptr;
}

namespace {
class TreeRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), sidebarControlHeight));
        return size;
    }
};
}

DragTreeWidget::DragTreeWidget(QWidget* parent) : QTreeWidget(parent) {
    setProperty("wjnRole", QStringLiteral("tree"));
    setDragEnabled(true);
    setItemDelegate(new TreeRowDelegate(this));
    setVerticalScrollBar(new ScrollBarField(Qt::Vertical, this));
    setHorizontalScrollBar(new ScrollBarField(Qt::Horizontal, this));
    setViewportMargins(8, 0, 0, 0);
}

void DragTreeWidget::setMimeDataFactory(MimeDataFactory factory) {
    mimeDataFactory_ = std::move(factory);
}

void DragTreeWidget::startDrag(Qt::DropActions supportedActions) {
    const auto* item = currentItem();
    if (!item || !mimeDataFactory_ || !(supportedActions & Qt::CopyAction)) return;
    auto* mimeData = mimeDataFactory_(item);
    if (!mimeData) return;
    QDrag drag(this);
    drag.setMimeData(mimeData);
    drag.exec(Qt::CopyAction);
}

namespace { Palette inheritedPalette(const QWidget& widget); }

TextField::TextField(QWidget* parent) : QLineEdit(parent) {
    setProperty("wjnRole", QStringLiteral("field"));
    setMinimumHeight(sidebarControlHeight);
    setFrame(false);
    setTextMargins(7, 0, 7, 0);
    setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0;"));
}

void TextField::paintEvent(QPaintEvent* event) {
    if (!property("wjnEmbedded").toBool()) {
        QPainter painter(this);
        const auto colors = inheritedPalette(*this);
        drawSurface(painter, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                    colors.panel, hasFocus() ? colors.accent : colors.border, 4);
    }
    QLineEdit::paintEvent(event);
}

SearchField::SearchField(QWidget* parent) : TextField(parent) {
    setFixedHeight(sidebarControlHeight);
    setClearButtonEnabled(true);
    setInputMethodHints(Qt::ImhNoPredictiveText);
}

ReadOnlyField::ReadOnlyField(QWidget* parent) : TextField(parent) {
    setReadOnly(true);
}

ChoiceField::ChoiceField(QWidget* parent) : QComboBox(parent) {
    setProperty("wjnRole", QStringLiteral("choice"));
    setMinimumHeight(sidebarControlHeight);
}

void ChoiceField::setManualFilterEnabled(bool enabled) {
    if (manualFilterEnabled_ == enabled) return;
    manualFilterEnabled_ = enabled;
    setEditable(enabled);
    if (enabled) {
        setInsertPolicy(QComboBox::NoInsert);
        auto* search = new TextField(this);
        search->setProperty("wjnEmbedded", true);
        setLineEdit(search);
        search->setPlaceholderText(QStringLiteral("输入筛选"));
        search->setTextMargins(28, 0, 25, 0);
        auto* filter = new QCompleter(model(), search);
        filter->setCaseSensitivity(Qt::CaseInsensitive);
        filter->setFilterMode(Qt::MatchContains);
        filter->setCompletionMode(QCompleter::PopupCompletion);
        setCompleter(filter);
    }
    update();
}

bool ChoiceField::manualFilterEnabled() const noexcept {
    return manualFilterEnabled_;
}

bool ChoiceField::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip && !isEditable()) {
        auto displayFont = font();
        displayFont.setPixelSize(std::max(11,font().pixelSize() > 0 ? font().pixelSize() : 14));
        if (QFontMetrics(displayFont).horizontalAdvance(currentText()) > width()-43) {
            const auto* help = static_cast<QHelpEvent*>(event);
            auto fullText = currentText();
            if (!toolTip().isEmpty() && toolTip() != fullText) fullText += '\n'+toolTip();
            QToolTip::showText(help->globalPos(),Qt::mightBeRichText(fullText) ? Qt::convertFromPlainText(fullText) : fullText,this);
            event->accept(); return true;
        }
    }
    return QComboBox::event(event);
}

void ChoiceField::paintEvent(QPaintEvent*) {
    if (isEditable() && lineEdit() && !lineEdit()->property("wjnChoiceEditor").toBool()) {
        lineEdit()->setProperty("wjnChoiceEditor", true);
        lineEdit()->setFrame(false);
        lineEdit()->setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0;"));
        if (!manualFilterEnabled_) lineEdit()->setTextMargins(7, 0, 24, 0);
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Palette colors;
    colors.panel = surfaceColor_;
    colors.border = borderColor_;
    colors.accent = accentColor_;
    colors.text = textColor_;
    if (!isEnabled()) painter.setOpacity(0.45);
    drawDropdown(painter, QRectF(rect()).adjusted(1, 1, -1, -1),
                 isEditable() ? QString() : currentText(), colors,
                 std::max(11, font().pixelSize() > 0 ? font().pixelSize() : 14),
                 manualFilterEnabled_);
}

ActionButton::ActionButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
    setProperty("wjnRole", QStringLiteral("action"));
    setMinimumHeight(sidebarControlHeight);
    setCursor(Qt::PointingHandCursor);
}

void ActionButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Palette colors;
    colors.panel = surfaceColor_;
    colors.border = borderColor_;
    colors.accent = accentColor_;
    colors.text = textColor_;
    if (!isEnabled()) painter.setOpacity(0.45);
    const auto buttonRect = QRectF(rect()).adjusted(1, 1, -1, -1);
    const bool hasIcon = !icon().isNull();
    drawButton(painter, buttonRect, hasIcon ? QString() : text(),
               isDown() || isChecked() || underMouse(), colors,
               std::max(11, font().pixelSize() > 0 ? font().pixelSize() : 14));
    if (hasIcon) {
        const int side = std::min({iconSize().width(), iconSize().height(),
                                   std::max(0, height() - 14)});
        const int gap = text().isEmpty() ? 0 : 4;
        const qreal textWidth = std::min<qreal>(fontMetrics().horizontalAdvance(text()),
                                                std::max(0, width() - side - gap - 14));
        const qreal left = std::max<qreal>(7, (width() - side - gap - textWidth) / 2.0);
        const QRect iconRect(qRound(left), (height() - side) / 2, side, side);
        icon().paint(&painter, iconRect, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
        drawText(painter, QRectF(left + side + gap, 0, textWidth, height()),
                 text(), font(), textColor_, Qt::AlignCenter);
    }
}

ZoomControl::ZoomControl(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("patchbayZoomControl"));
    setFixedHeight(32);
    setMinimumWidth(146);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    if (auto* core = ThemeManager::installedThemeCore())
        connect(core, &ThemeCore::themeChanged, this, [this] { update(); });
}

void ZoomControl::setZoomFactor(qreal factor) {
    if (qFuzzyCompare(factor_, factor)) return;
    factor_ = factor;
    update();
}

qreal ZoomControl::zoomFactor() const noexcept { return factor_; }
QSize ZoomControl::sizeHint() const { return {150, 32}; }

void ZoomControl::paintEvent(QPaintEvent*) {
    const auto color = [](const QString& role, const QColor& fallback) {
        return themeTokenColor(QStringLiteral("patchbay"), role, fallback);
    };
    const QColor surface = color(QStringLiteral("control-zoom-surface"), QColor("#1e2b34"));
    const QColor border = color(QStringLiteral("control-zoom-border"), QColor("#4b5f73"));
    const QColor icon = color(QStringLiteral("control-zoom-icon"), QColor("#43c7a8"));
    const QColor caption = color(QStringLiteral("control-zoom-text"), QColor("#e6eef4"));
    const QColor hover = color(QStringLiteral("control-zoom-hover"), QColor("#33495b"));
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawSurface(painter, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), surface, border, 6.0);
    if (hoveredPart_ == 0 || hoveredPart_ == 2) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(hover);
        painter.drawRoundedRect(QRectF(hoveredPart_ == 0 ? 2.0 : width() - 31.0, 2.0, 29.0, height() - 4.0), 4, 4);
    }
    painter.setPen(QPen(border, 1.0));
    painter.drawLine(QPointF(32.0, 5.0), QPointF(32.0, height() - 5.0));
    painter.drawLine(QPointF(width() - 33.0, 5.0), QPointF(width() - 33.0, height() - 5.0));
    painter.setPen(QPen(icon, 1.7, Qt::SolidLine, Qt::RoundCap));
    const auto magnifier = [&painter, this, surface, icon](qreal x, bool plus) {
        const QPointF center(x - 2.0, height() / 2.0 - 3.0);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, 6.0, 6.0);
        painter.drawLine(center + QPointF(4.5, 4.5), center + QPointF(10.0, 10.0));
        const QPointF sign = center + QPointF(10.0, 10.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(surface);
        painter.drawEllipse(sign, 4.5, 4.5);
        painter.setPen(QPen(icon, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(sign + QPointF(-2.5, 0.0), sign + QPointF(2.5, 0.0));
        if (plus) painter.drawLine(sign + QPointF(0.0, -2.5), sign + QPointF(0.0, 2.5));
    };
    magnifier(14.0, true);
    magnifier(width() - 19.0, false);
    painter.setPen(caption);
    painter.drawText(QRectF(34.0, 0.0, width() - 68.0, height()), Qt::AlignCenter,
                     QString::number(qRound(factor_ * 100.0)) + QLatin1Char('%'));
}

void ZoomControl::mouseMoveEvent(QMouseEvent* event) {
    const int part = event->position().x() < 33.0 ? 0
        : event->position().x() >= width() - 33.0 ? 2 : 1;
    if (part != hoveredPart_) { hoveredPart_ = part; update(); }
    QWidget::mouseMoveEvent(event);
}

void ZoomControl::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        if (event->position().x() < 33.0) emit zoomInRequested();
        else if (event->position().x() >= width() - 33.0) emit zoomOutRequested();
        else emit zoomResetRequested();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ZoomControl::leaveEvent(QEvent* event) {
    hoveredPart_ = -1;
    update();
    QWidget::leaveEvent(event);
}

DialogButtonBar::DialogButtonBar(QWidget* parent) : QDialogButtonBox(parent) {
    setProperty("wjnRole", QStringLiteral("dialogActions"));
}

DialogButtonBar::DialogButtonBar(StandardButtons buttons, QWidget* parent)
    : DialogButtonBar(parent) {
    for (const auto button : {Ok, Open, Save, Cancel, Close, Discard, Apply,
                              Reset, RestoreDefaults, Help, SaveAll, Yes,
                              YesToAll, No, NoToAll, Abort, Retry, Ignore})
        if (buttons.testFlag(button)) addButton(button);
}

QPushButton* DialogButtonBar::addButton(const QString& text, ButtonRole role) {
    auto* button = new ActionButton(text, this);
    QDialogButtonBox::addButton(button, role);
    return button;
}

QPushButton* DialogButtonBar::addButton(StandardButton standard) {
    auto* placeholder = QDialogButtonBox::addButton(standard);
    if (!placeholder) return nullptr;
    const auto label = placeholder->text();
    const auto role = buttonRole(placeholder);
    removeButton(placeholder);
    delete placeholder;
    auto* result=addButton(label, role);
    result->setProperty("wjn.standardButton",static_cast<int>(standard));
    return result;
}
QPushButton* DialogButtonBar::button(StandardButton standard) const {
    for (auto* item:findChildren<QPushButton*>(QString{},Qt::FindDirectChildrenOnly))
        if (item->property("wjn.standardButton").isValid()
            && item->property("wjn.standardButton").toInt()==static_cast<int>(standard)) return item;
    return QDialogButtonBox::button(standard);
}
bool DialogButtonBar::event(QEvent* event) {
    const bool handled=QDialogButtonBox::event(event);
    if (event->type()==QEvent::LanguageChange) {
        for (auto* item:findChildren<QPushButton*>(QString{},Qt::FindDirectChildrenOnly)) {
            if (!item->property("wjn.standardButton").isValid()) continue;
            const auto standard=static_cast<StandardButton>(item->property("wjn.standardButton").toInt());
            QDialogButtonBox probe(standard);
            if (auto* native=probe.button(standard)) item->setText(native->text());
        }
    }
    return handled;
}

IconActionButton::IconActionButton(QWidget* parent) : QToolButton(parent) {
    setProperty("wjnRole", QStringLiteral("iconAction"));
    setCursor(Qt::PointingHandCursor);
}

void IconActionButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Palette colors;
    colors.panel = surfaceColor_;
    colors.border = borderColor_;
    colors.accent = accentColor_;
    colors.text = textColor_;
    if (!isEnabled()) painter.setOpacity(0.45);
    drawButton(painter, QRectF(rect()).adjusted(1, 1, -1, -1), {},
               isDown() || isChecked() || underMouse(), colors);
    const auto iconSide = std::min({iconSize().width(), iconSize().height(),
                                    std::max(0, height() - 14)});
    const bool hasIcon = !icon().isNull() && iconSide > 0;
    const int gap = hasIcon && !text().isEmpty() ? 4 : 0;
    QRectF textRect = rect().adjusted(5, 3, -5, -3);
    if (hasIcon) {
        QRectF iconRect;
        if (toolButtonStyle() == Qt::ToolButtonTextUnderIcon) {
            iconRect = QRectF((width() - iconSide) / 2.0, 6, iconSide, iconSide);
            textRect.setTop(iconRect.bottom() + gap);
        } else if (toolButtonStyle() == Qt::ToolButtonTextBesideIcon) {
            const qreal textWidth = std::min<qreal>(fontMetrics().horizontalAdvance(text()),
                                                    std::max(0, width() - iconSide - gap - 14));
            const qreal left = std::max<qreal>(7, (width() - iconSide - gap - textWidth) / 2.0);
            iconRect = QRectF(left, (height() - iconSide) / 2.0, iconSide, iconSide);
            textRect = QRectF(iconRect.right() + gap, 0, textWidth, height());
        } else {
            iconRect = QRectF((width() - iconSide) / 2.0,
                              (height() - iconSide) / 2.0, iconSide, iconSide);
        }
        icon().paint(&painter, iconRect.toRect(), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
    if (toolButtonStyle() != Qt::ToolButtonIconOnly || !hasIcon) {
        const auto visible=fontMetrics().elidedText(text(),Qt::ElideRight,std::max(0,qRound(textRect.width())));
        if (visible!=text() && !toolTip().contains(text()))
            setToolTip(toolTip().isEmpty()?text():text()+QLatin1Char('\n')+toolTip());
        drawText(painter, textRect, visible, font(), textColor_, Qt::AlignCenter);
    }
}

IntegerField::IntegerField(QWidget* parent) : QSpinBox(parent) {
    setProperty("wjnRole", QStringLiteral("number"));
    setMinimumHeight(sidebarControlHeight);
    setFrame(false);
    lineEdit()->setFrame(false);
    lineEdit()->setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0;"));
}

DecimalField::DecimalField(QWidget* parent) : QDoubleSpinBox(parent) {
    setProperty("wjnRole", QStringLiteral("number"));
    setMinimumHeight(sidebarControlHeight);
    setFrame(false);
    lineEdit()->setFrame(false);
    lineEdit()->setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0;"));
}

ToggleField::ToggleField(const QString& text, QWidget* parent) : QCheckBox(text, parent) {
    setProperty("wjnRole", QStringLiteral("toggle"));
    setMinimumHeight(sidebarControlHeight);
    setCursor(Qt::PointingHandCursor);
}

void ToggleField::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF indicator(1, (height() - 18) / 2.0, 18, 18);
    const auto accent = isEnabled() ? accentColor_ : borderColor_;
    drawSurface(painter, indicator, isChecked() ? accent : surfaceColor_, borderColor_, 4);
    if (isChecked()) {
        painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const auto origin = indicator.topLeft();
        painter.drawLine(origin + QPointF(4, 9), origin + QPointF(8, 13));
        painter.drawLine(origin + QPointF(8, 13), origin + QPointF(14, 5));
    }
    auto labelColor = textColor_;
    if (!isEnabled()) labelColor.setAlpha(110);
    drawText(painter, QRectF(27, 0, width() - 27, height()), text(), font(), labelColor,
             Qt::AlignLeft | Qt::AlignVCenter);
}

RangeSlider::RangeSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent) {
    setProperty("wjnRole", QStringLiteral("range"));
    setCursor(Qt::PointingHandCursor);
}

void RangeSlider::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal fraction = maximum() > minimum()
        ? qreal(value() - minimum()) / qreal(maximum() - minimum()) : 0.0;
    const auto accent = isEnabled() ? accentColor_ : borderColor_;
    if (orientation() == Qt::Horizontal) {
        const qreal left = 9, right = width() - 9, y = height() / 2.0;
        drawSurface(painter, {left, y - 3, right - left, 6}, surfaceColor_, borderColor_, 3);
        if (fraction > 0)
            drawSurface(painter, {left, y - 3, (right - left) * fraction, 6}, accent, {}, 3);
        drawMarker(painter, {left + (right - left) * fraction, y}, 8, accent, borderColor_);
    } else {
        const qreal top = 9, bottom = height() - 9, x = width() / 2.0;
        drawSurface(painter, {x - 3, top, 6, bottom - top}, surfaceColor_, borderColor_, 3);
        if (fraction > 0)
            drawSurface(painter, {x - 3, bottom - (bottom - top) * fraction, 6,
                                  (bottom - top) * fraction}, accent, {}, 3);
        drawMarker(painter, {x, bottom - (bottom - top) * fraction}, 8, accent, borderColor_);
    }
}

ProgressField::ProgressField(QWidget* parent) : QProgressBar(parent) {
    setProperty("wjnRole", QStringLiteral("progress"));
}

void ProgressField::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    drawSurface(painter, track, surfaceColor_, borderColor_, 4);
    const qreal fraction = maximum() > minimum()
        ? std::clamp(qreal(value() - minimum()) / qreal(maximum() - minimum()), 0.0, 1.0)
        : 0.0;
    if (fraction > 0.0) {
        QRectF fill = track.adjusted(2, 2, -2, -2);
        if (orientation() == Qt::Horizontal) {
            fill.setWidth(fill.width() * fraction);
            if (invertedAppearance()) fill.moveRight(track.right() - 2);
        } else {
            fill.setHeight(fill.height() * fraction);
            if (!invertedAppearance()) fill.moveBottom(track.bottom() - 2);
        }
        drawSurface(painter, fill, isEnabled() ? accentColor_ : borderColor_, {}, 3);
    }
    if (isTextVisible())
        drawText(painter, track, text(), font(), textColor_, Qt::AlignCenter);
}

namespace {
Palette inheritedPalette(const QWidget& widget) {
    Palette defaults;
    const auto native = widget.palette();
    defaults.background = themeTokenColor("global", "surface-window", native.color(QPalette::Window));
    defaults.panel = themeTokenColor("global", "surface-panel", native.color(QPalette::Base));
    defaults.text = themeTokenColor("global", "text-primary", native.color(QPalette::Text));
    defaults.accent = themeTokenColor("global", "accent-primary", native.color(QPalette::Highlight));
    defaults.border = themeTokenColor("global", "border-normal", native.color(QPalette::Mid));
    return paletteFor(widget, defaults);
}

void drawSpinField(QPainter& painter, const QWidget& widget,
                   const QRect& up, const QRect& down) {
    const auto colors = inheritedPalette(widget);
    const auto accent = themeColor(widget, "accentColor", colors.accent);
    const auto surface = themeColor(widget, "surfaceColor", colors.panel);
    const auto border = themeColor(widget, "borderColor", colors.border);
    drawSurface(painter, QRectF(widget.rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                surface, widget.hasFocus() ? accent : border, 4);
    painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const auto chevron = [&painter](const QRect& bounds, bool upward) {
        if (bounds.isEmpty()) return;
        const auto center = bounds.center();
        const qreal direction = upward ? 1.0 : -1.0;
        painter.drawLine(QPointF(center.x() - 4, center.y() + direction * 2),
                         QPointF(center.x(), center.y() - direction * 2));
        painter.drawLine(QPointF(center.x(), center.y() - direction * 2),
                         QPointF(center.x() + 4, center.y() + direction * 2));
    };
    chevron(up, true);
    chevron(down, false);
}

class FieldItemDelegate final : public QStyledItemDelegate {
public:
    explicit FieldItemDelegate(QWidget* parent) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto* host = qobject_cast<const QWidget*>(parent());
        const auto colors = host ? inheritedPalette(*host) : Palette {};
        const bool selected = option.state & QStyle::State_Selected;
        QColor fill = selected ? colors.accent : colors.panel;
        if (selected) fill.setAlpha(95);
        painter->save();
        painter->fillRect(option.rect, fill);
        painter->setPen(colors.border);
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        const auto value = index.data(Qt::DisplayRole).toString();
        const auto content = option.rect.adjusted(8, 0, -6, 0);
        drawText(*painter, content,
                 option.fontMetrics.elidedText(value, Qt::ElideRight, content.width()),
                 option.font, colors.text, Qt::AlignLeft | Qt::AlignVCenter);
        painter->restore();
    }
};

class FieldHeader final : public QHeaderView {
public:
    FieldHeader(Qt::Orientation orientation, QWidget* parent)
        : QHeaderView(orientation, parent) {}
protected:
    void paintSection(QPainter* painter, const QRect& bounds,
                      int logicalIndex) const override {
        const auto colors = parentWidget() ? inheritedPalette(*parentWidget()) : Palette {};
        painter->save();
        drawSurface(*painter, bounds, colors.panel, colors.border, 0);
        const auto label = model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString();
        drawText(*painter, bounds.adjusted(7, 0, -5, 0), label, font(),
                 colors.text, Qt::AlignLeft | Qt::AlignVCenter);
        painter->restore();
    }
};

class FieldTabBar final : public QTabBar {
public:
    explicit FieldTabBar(QWidget* parent) : QTabBar(parent) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto colors = inheritedPalette(*this);
        for (int index = 0; index < count(); ++index) {
            const auto bounds = tabRect(index).adjusted(1, 1, -1, -1);
            const bool hasClose = tabButton(index, QTabBar::RightSide) != nullptr;
            drawButton(painter, bounds, QString(), index == currentIndex(), colors,
                       std::max(11, font().pixelSize() > 0 ? font().pixelSize() : 14));
            const QRectF labelBounds = bounds.adjusted(8, 0, hasClose ? -28 : -8, 0);
            const auto caption = fontMetrics().elidedText(tabText(index), Qt::ElideRight,
                std::max(0, qRound(labelBounds.width())));
            drawText(painter, labelBounds, caption, font(), colors.text,
                     Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    void tabInserted(int index) override {
        QTabBar::tabInserted(index);
        QTimer::singleShot(0, this, [this] {
            for (int tab = 0; tab < count(); ++tab) {
                auto* current = tabButton(tab, QTabBar::RightSide);
                if (!current || qobject_cast<IconActionButton*>(current)) continue;
                auto* close = new IconActionButton(this);
                close->setText(QStringLiteral("×"));
                close->setToolButtonStyle(Qt::ToolButtonTextOnly);
                close->setFixedSize(18, 18);
                setTabButton(tab, QTabBar::RightSide, close);
                QObject::connect(close, &QToolButton::clicked, this, [this, close] {
                    for (int candidate = 0; candidate < count(); ++candidate)
                        if (tabButton(candidate, QTabBar::RightSide) == close) {
                            Q_EMIT tabCloseRequested(candidate);
                            break;
                        }
                });
            }
        });
    }
};

class FieldSplitHandle final : public QSplitterHandle {
public:
    FieldSplitHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto colors = parentWidget() ? inheritedPalette(*parentWidget()) : Palette {};
        painter.fillRect(rect(), colors.background);
        const auto center = QRectF(rect()).center();
        for (int index = -1; index <= 1; ++index)
            drawMarker(painter, orientation() == Qt::Horizontal
                ? center + QPointF(0, index * 5)
                : center + QPointF(index * 5, 0), 1.3, colors.border);
    }
};

class FieldDockTitle final : public QWidget {
public:
    FieldDockTitle(QDockWidget* dock, Qt::DockWidgetArea area)
        : QWidget(dock), dock_(dock), area_(area) {
        setFixedHeight(sidebarControlHeight);
    }

    void updateCollapsed(bool collapsed) {
        collapsed_ = collapsed;
        updateGeometry();
        update();
    }
    QSize sizeHint() const override {
        return { collapsed_ ? 34 : 160, sidebarControlHeight };
    }
    QSize minimumSizeHint() const override {
        return { 0, sidebarControlHeight };
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto colors = inheritedPalette(*this);
        drawSurface(painter, QRectF(rect()), colors.panel, colors.border, 0);
        if (!collapsed_)
            drawText(painter, QRectF(rect()).adjusted(
                         area_ == Qt::RightDockWidgetArea ? 30 : 8, 0, -8, 0),
                     dock_->windowTitle(), font(), colors.text,
                     Qt::AlignLeft | Qt::AlignVCenter);
    }
private:
    QDockWidget* dock_;
    Qt::DockWidgetArea area_;
    bool collapsed_ = false;
};

class FieldDockGrip final : public QWidget {
public:
    FieldDockGrip(DockPanel* dock, Qt::DockWidgetArea area)
        : QWidget(dock), dock_(dock), area_(area) {
        setObjectName(QStringLiteral("dockResizeGrip"));
        // Settings may contain native child windows. Keep the overlapping grip
        // in the native stacking order so it remains clickable after selection.
        setAttribute(Qt::WA_NativeWindow);
        setCursor(Qt::SplitHCursor);
        arrow_ = new IconActionButton(this);
        arrow_->setObjectName(QStringLiteral("dockCollapseButton"));
        arrow_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        arrow_->setFixedSize(22, sidebarControlHeight);
        QObject::connect(arrow_, &QToolButton::clicked, dock_, [dock] {
            dock->setCollapsed(!dock->isCollapsed());
        });
        updateCollapsed(false);
    }

    void updateCollapsed(bool collapsed) {
        arrow_->setText(area_ == Qt::LeftDockWidgetArea
            ? (collapsed ? QStringLiteral("▶") : QStringLiteral("◀"))
            : (collapsed ? QStringLiteral("◀") : QStringLiteral("▶")));
        arrow_->setToolTip(collapsed ? QStringLiteral("展开侧边栏")
                                      : QStringLiteral("收缩侧边栏"));
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        arrow_->move((width() - arrow_->width()) / 2,
                     (height() - arrow_->height()) / 2);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto colors = inheritedPalette(*this);
        painter.fillRect(rect(), colors.background);
        painter.setPen(colors.border);
        const qreal center = width() / 2.0;
        painter.drawLine(QPointF(center, 0), QPointF(center, height()));
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        dragStart_ = event->globalPosition().x();
        widthAtDragStart_ = dock_->width();
        dragging_ = true;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_) return;
        auto* main = qobject_cast<QMainWindow*>(dock_->parentWidget());
        if (!main) return;
        const int delta = qRound(event->globalPosition().x() - dragStart_);
        const int direction = area_ == Qt::LeftDockWidgetArea ? 1 : -1;
        const int target = std::clamp(widthAtDragStart_ + direction * delta,
                                      dock_->minimumWidth(), std::max(160, main->width() - 180));
        main->resizeDocks({ dock_ }, { target }, Qt::Horizontal);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) dragging_ = false;
        event->accept();
    }
private:
    DockPanel* dock_;
    Qt::DockWidgetArea area_;
    IconActionButton* arrow_ = nullptr;
    qreal dragStart_ = 0;
    int widthAtDragStart_ = 0;
    bool dragging_ = false;
};
}

void IntegerField::paintEvent(QPaintEvent*) {
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    const auto part = [this, &option](QStyle::SubControl control) {
        return style()->subControlRect(QStyle::CC_SpinBox, &option, control, this);
    };
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawSpinField(painter, *this, part(QStyle::SC_SpinBoxUp), part(QStyle::SC_SpinBoxDown));
}

void DecimalField::paintEvent(QPaintEvent*) {
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    const auto part = [this, &option](QStyle::SubControl control) {
        return style()->subControlRect(QStyle::CC_SpinBox, &option, control, this);
    };
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawSpinField(painter, *this, part(QStyle::SC_SpinBoxUp), part(QStyle::SC_SpinBoxDown));
}

ScrollBarField::ScrollBarField(Qt::Orientation orientation, QWidget* parent)
    : QScrollBar(orientation, parent) {
    setProperty("wjnRole", QStringLiteral("scrollbar"));
}

void ScrollBarField::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto colors = inheritedPalette(*this);
    QStyleOptionSlider option;
    initStyleOption(&option);
    const auto part = [this, &option](QStyle::SubControl control) {
        return style()->subControlRect(QStyle::CC_ScrollBar, &option, control, this);
    };
    drawSurface(painter, part(QStyle::SC_ScrollBarGroove), colors.panel,
                colors.border, 3);
    if (maximum() > minimum())
        drawSurface(painter, part(QStyle::SC_ScrollBarSlider), colors.accent,
                    colors.border, 3);
    const auto before = part(QStyle::SC_ScrollBarSubLine);
    const auto after = part(QStyle::SC_ScrollBarAddLine);
    if (!before.isEmpty())
        drawText(painter, before, orientation() == Qt::Horizontal
            ? QStringLiteral("‹") : QStringLiteral("↑"), font(), colors.text);
    if (!after.isEmpty())
        drawText(painter, after, orientation() == Qt::Horizontal
            ? QStringLiteral("›") : QStringLiteral("↓"), font(), colors.text);
}

ScrollField::ScrollField(QWidget* parent) : QScrollArea(parent) {
    setProperty("wjnRole", QStringLiteral("scrollArea"));
    setVerticalScrollBar(new ScrollBarField(Qt::Vertical, this));
    setHorizontalScrollBar(new ScrollBarField(Qt::Horizontal, this));
}

ListField::ListField(QWidget* parent) : QListWidget(parent) {
    setProperty("wjnRole", QStringLiteral("list"));
    setItemDelegate(new FieldItemDelegate(this));
    setVerticalScrollBar(new ScrollBarField(Qt::Vertical, this));
    setHorizontalScrollBar(new ScrollBarField(Qt::Horizontal, this));
}

TableField::TableField(QWidget* parent) : QTableWidget(parent) {
    setProperty("wjnRole", QStringLiteral("table"));
    setItemDelegate(new FieldItemDelegate(this));
    setHorizontalHeader(new FieldHeader(Qt::Horizontal, this));
    setVerticalScrollBar(new ScrollBarField(Qt::Vertical, this));
    setHorizontalScrollBar(new ScrollBarField(Qt::Horizontal, this));
}

GroupPanel::GroupPanel(const QString& title, QWidget* parent) : QGroupBox(title, parent) {
    setProperty("wjnRole", QStringLiteral("group"));
}

void GroupPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto colors = inheritedPalette(*this);
    drawSurface(painter, QRectF(rect()).adjusted(1, 8, -1, -1),
                colors.panel, colors.border, 6);
    const QRectF titleBounds(10, 0, std::max(0, width() - 20), 22);
    painter.fillRect(titleBounds.adjusted(0, 1, 0, -1), colors.background);
    auto titleFont = font();
    titleFont.setBold(true);
    drawText(painter, titleBounds, title(), titleFont, colors.text,
             Qt::AlignLeft | Qt::AlignVCenter);
}

TextLabel::TextLabel(QWidget* parent) : QLabel(parent) {
    setProperty("wjnRole", QStringLiteral("label"));
}

TextLabel::TextLabel(const QString& value, QWidget* parent)
    : TextLabel(parent) {
    setText(value);
}

QSize TextLabel::minimumSizeHint() const {
    auto result = QLabel::minimumSizeHint();
    if (!wordWrap() && !(textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::TextEditable))
        && pixmap().isNull() && textFormat() != Qt::RichText)
        result.setWidth(0);
    return result;
}

bool TextLabel::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip && !wordWrap() && textFormat() != Qt::RichText
        && QFontMetrics(font()).horizontalAdvance(text()) > contentsRect().width()-2*margin()) {
        const auto* help = static_cast<QHelpEvent*>(event);
        auto fullText = text();
        if (!toolTip().isEmpty() && toolTip() != fullText) fullText += '\n'+toolTip();
        QToolTip::showText(help->globalPos(),Qt::mightBeRichText(fullText) ? Qt::convertFromPlainText(fullText) : fullText,this);
        event->accept(); return true;
    }
    return QLabel::event(event);
}

void TextLabel::paintEvent(QPaintEvent* event) {
    if ((textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::TextEditable)) || !pixmap().isNull()
        || textFormat() == Qt::RichText) {
        QLabel::paintEvent(event);
        return;
    }
    QPainter painter(this);
    auto colors = inheritedPalette(*this);
    colors.text = themeColor(*this, "wjnText", palette().color(QPalette::WindowText));
    const auto flags = static_cast<Qt::Alignment>(int(alignment())
        | (wordWrap() ? int(Qt::TextWordWrap) : 0));
    const auto bounds = QRectF(contentsRect()).adjusted(margin(),margin(),-margin(),-margin());
    drawText(painter,bounds,wordWrap() ? text() : QFontMetrics(font()).elidedText(text(),Qt::ElideRight,std::max(0,qFloor(bounds.width()))),font(),colors.text,flags);
}

TabField::TabField(QWidget* parent) : QTabWidget(parent) {
    setProperty("wjnRole", QStringLiteral("tabs"));
    setTabBar(new FieldTabBar(this));
}

void TabField::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const auto colors = inheritedPalette(*this);
    drawSurface(painter, QRectF(rect()).adjusted(1, 1, -1, -1),
                colors.panel, colors.border, 4);
}

LogField::LogField(QWidget* parent) : QPlainTextEdit(parent) {
    setProperty("wjnRole", QStringLiteral("log"));
    setFrameStyle(QFrame::NoFrame);
    setVerticalScrollBar(new ScrollBarField(Qt::Vertical, this));
    setHorizontalScrollBar(new ScrollBarField(Qt::Horizontal, this));
}

void LogField::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const auto colors = inheritedPalette(*this);
    drawSurface(painter, QRectF(viewport()->rect()).adjusted(1, 1, -1, -1),
                colors.panel, colors.border, 4);
    painter.setClipRect(viewport()->rect().adjusted(3, 3, -3, -3));
    painter.setPen(colors.text);
    const auto selection = textCursor();
    for (auto block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const auto top = blockBoundingGeometry(block).translated(contentOffset()).top();
        const auto bottom = top + blockBoundingRect(block).height();
        if (top > viewport()->height()) break;
        if (!block.isVisible() || bottom < 0 || !block.layout()) continue;
        QList<QTextLayout::FormatRange> formats;
        if (selection.hasSelection()) {
            const int start = std::max(selection.selectionStart(), block.position());
            const int end = std::min(selection.selectionEnd(), block.position() + block.length());
            if (start < end) {
                QTextLayout::FormatRange highlight;
                highlight.start = start - block.position();
                highlight.length = end - start;
                highlight.format.setBackground(palette().highlight());
                highlight.format.setForeground(palette().highlightedText());
                formats.push_back(highlight);
            }
        }
        block.layout()->draw(&painter, QPointF(contentOffset().x(), top), formats);
    }
}

SplitField::SplitField(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
    setProperty("wjnRole", QStringLiteral("splitter"));
}

QSplitterHandle* SplitField::createHandle() {
    return new FieldSplitHandle(orientation(), this);
}

DockPanel::DockPanel(const QString& title, Qt::DockWidgetArea area, QWidget* parent)
    : QDockWidget(title, parent), area_(area) {
    setProperty("wjnRole", QStringLiteral("dock"));
    setMinimumWidth(160);
    auto* bar = new FieldDockTitle(this, area);
    setTitleBarWidget(bar);
    titleBar_ = bar;
    resizeGrip_ = new FieldDockGrip(this, area);
    resizeGrip_->show();
}

void DockPanel::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) return;
    if (collapsed) expandedWidth_ = std::max(width(), 160);
    const int expandedHeight = height();
    const int targetWidth = collapsed ? 34 : expandedWidth_;
    collapsed_ = collapsed;
    static_cast<FieldDockTitle*>(titleBar_)->updateCollapsed(collapsed);
    static_cast<FieldDockGrip*>(resizeGrip_)->updateCollapsed(collapsed);
    if (widget()) widget()->setVisible(!collapsed);
    setMinimumHeight(collapsed ? expandedHeight : 0);
    setMinimumWidth(collapsed ? 34 : 160);
    setMaximumWidth(collapsed ? 34 : QWIDGETSIZE_MAX);
    resizeGrip_->setVisible(!isFloating());
    if (auto* main = qobject_cast<QMainWindow*>(parentWidget()))
        main->resizeDocks({ this }, { targetWidth }, Qt::Horizontal);
    updateGripGeometry();
    QTimer::singleShot(0, this, [this] { updateGripGeometry(); });
}

bool DockPanel::event(QEvent* event) {
    const bool result = QDockWidget::event(event);
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show)
        updateGripGeometry();
    return result;
}

void DockPanel::resizeEvent(QResizeEvent* event) {
    QDockWidget::resizeEvent(event);
    if (!collapsed_ && !isFloating()) expandedWidth_ = width();
    updateGripGeometry();
}

void DockPanel::updateGripGeometry() {
    if (resizeGrip_) {
        resizeGrip_->setGeometry(area_ == Qt::LeftDockWidgetArea ? width() - 24 : 0,
                                 0, 24, height());
        resizeGrip_->raise();
    }
}

ClientNameWidget::ClientNameWidget(QString clientName, ApplyHandler handler, QWidget* parent)
    : QWidget(parent), handler_(std::move(handler)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* form = new QFormLayout;
    editor_ = new TextField(this);
    editor_->setText(std::move(clientName));
    editor_->setObjectName(QStringLiteral("gate.clientName"));
    editor_->setMaxLength(32);
    editor_->setFixedHeight(sidebarControlHeight);
    auto* nameLabel = new TextLabel(this);
    bindLocalizedText(nameLabel, "text", "ui", "clientName.label", "Client name");
    form->addRow(nameLabel, editor_);
    layout->addLayout(form);
    auto* actions = new QHBoxLayout;
    status_ = new TextLabel(this);
    auto* applyButton = new ActionButton(QString{}, this);
    bindLocalizedText(applyButton, "text", "ui", "clientName.apply", "Apply");
    applyButton->setFixedHeight(sidebarControlHeight);
    applyButton->setObjectName(QStringLiteral("gate.applyClientName"));
    actions->addWidget(status_, 1);
    actions->addWidget(applyButton);
    layout->addLayout(actions);
    connect(applyButton, &QPushButton::clicked, this, [this] { apply(); });
    connect(editor_, &QLineEdit::returnPressed, this, [this] { apply(); });
    connect(editor_, &QLineEdit::textChanged, this, [this] { applyStatus_ = ApplyStatus::none; refreshStatus(); });
    if (auto* manager = I18nManager::installed()) manager->bind(this, [this] { refreshStatus(); });
}

void ClientNameWidget::setClientName(const QString& clientName) {
    editor_->setText(clientName);
    applyStatus_ = ApplyStatus::none;
    refreshStatus();
}

void ClientNameWidget::apply() {
    const auto value = editor_->text().trimmed();
    if (value.isEmpty()) { applyStatus_ = ApplyStatus::empty; refreshStatus(); return; }
    const bool success = handler_ && handler_(value);
    if (success) editor_->setText(value);
    applyStatus_ = success ? ApplyStatus::applied : ApplyStatus::rejected;
    refreshStatus();
}

void ClientNameWidget::refreshStatus() {
    switch (applyStatus_) {
    case ApplyStatus::none: status_->clear(); break;
    case ApplyStatus::empty: status_->setText(localizedText("ui", "clientName.empty", "Name cannot be empty")); break;
    case ApplyStatus::applied: status_->setText(localizedText("ui", "clientName.applied", "Applied")); break;
    case ApplyStatus::rejected: status_->setText(localizedText("ui", "clientName.rejected", "Unable to apply name")); break;
    }
}

EmbeddedFxEditor::EmbeddedFxEditor(QWidget* editor, int stripHeight, QWidget* parent)
    : QWidget(parent), editor_(editor) {
    if (!editor) return;
    editor->setParent(this);
    setFixedSize(editor->width(), qMax(1, editor->height() - stripHeight));
    editor->move(0, -stripHeight);
    editor->show();
}
}
