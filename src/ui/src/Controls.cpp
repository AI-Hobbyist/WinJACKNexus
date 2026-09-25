/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/Controls.hpp>
#include <QPalette>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <cmath>

namespace WinJACKNexus::UI::Controls {
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
    widget->update();
}

QColor themeColor(const QWidget& widget, const char* key, const QColor& fallback) {
    const auto value = widget.property(key);
    const auto color = value.value<QColor>();
    return color.isValid() ? color : fallback;
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
               themeColor(*this, "fxAccent", QColor("#187ee8")), QColor("#38516d"));
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

void drawDropdown(QPainter& painter, const QRectF& bounds, const QString& caption,
                  const Palette& colors, int fontSize) {
    painter.setPen(QPen(colors.border, 1)); painter.setBrush(colors.panel);
    painter.drawRoundedRect(bounds, 5, 5);
    auto font=painter.font(); font.setPixelSize(fontSize); painter.setFont(font);
    painter.setPen(colors.text);
    painter.drawText(bounds.adjusted(12,0,-29,0), Qt::AlignVCenter | Qt::AlignLeft, caption);
    const QPointF tip(bounds.right()-15,bounds.center().y()+3);
    painter.setPen(QPen(colors.accent, 1.8));
    painter.drawLine(tip+QPointF(-5,-5),tip);
    painter.drawLine(tip,tip+QPointF(5,-5));
}

void drawLevelMeter(QPainter& painter, const QRectF& bounds,
                    double levelDb, const MeterColors& colors) {
    constexpr double minimum = -60.0, maximum = 12.0;
    const auto xFor = [&bounds, minimum, maximum](double db) {
        return bounds.left() + (qBound(minimum, db, maximum)-minimum)/(maximum-minimum)*bounds.width();
    };
    const struct Zone { double from, to; QColor active, inactive; } zones[] {
        {-60, -12, colors.low, colors.inactiveLow},
        {-12, 0, colors.middle, colors.inactiveMiddle},
        {0, 12, colors.high, colors.inactiveHigh}
    };
    for (const auto& zone : zones) {
        const auto left = xFor(zone.from), right = xFor(zone.to);
        painter.fillRect(QRectF(left, bounds.top(), right-left, bounds.height()), zone.inactive);
        const auto activeRight = xFor(qBound(zone.from, levelDb, zone.to));
        if (activeRight > left)
            painter.fillRect(QRectF(left, bounds.top(), activeRight-left, bounds.height()), zone.active);
    }
    painter.setPen(QPen(colors.border, 1)); painter.setBrush(Qt::NoBrush);
    painter.drawRect(bounds);
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
    if (key == QStringLiteral("button")) return new QPushButton(parent);
    if (key == QStringLiteral("slider")) return new QSlider(parent);
    if (key == QStringLiteral("dropdown")) return new QComboBox(parent);
    if (key == QStringLiteral("toggle")) return new ToggleButton(parent);
    if (key == QStringLiteral("label")) return new QLabel(parent);
    if (key == QStringLiteral("card")) return new QFrame(parent);
    return nullptr;
}

ClientNameWidget::ClientNameWidget(QString clientName, ApplyHandler handler, QWidget* parent)
    : QWidget(parent), handler_(std::move(handler)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* form = new QFormLayout;
    editor_ = new QLineEdit(std::move(clientName), this);
    editor_->setObjectName(QStringLiteral("gate.clientName"));
    editor_->setMaxLength(32);
    form->addRow(QStringLiteral("客户端名称"), editor_);
    layout->addLayout(form);
    auto* actions = new QHBoxLayout;
    status_ = new QLabel(this);
    auto* applyButton = new QPushButton(QStringLiteral("应用"), this);
    applyButton->setObjectName(QStringLiteral("gate.applyClientName"));
    actions->addWidget(status_, 1);
    actions->addWidget(applyButton);
    layout->addLayout(actions);
    connect(applyButton, &QPushButton::clicked, this, [this] { apply(); });
    connect(editor_, &QLineEdit::returnPressed, this, [this] { apply(); });
    connect(editor_, &QLineEdit::textChanged, this, [this] { status_->clear(); });
}

void ClientNameWidget::setClientName(const QString& clientName) {
    editor_->setText(clientName);
    status_->clear();
}

void ClientNameWidget::apply() {
    const auto value = editor_->text().trimmed();
    if (value.isEmpty()) { status_->setText(QStringLiteral("名称不能为空")); return; }
    const bool success = handler_ && handler_(value);
    status_->setText(success ? QStringLiteral("已应用") : QStringLiteral("名称无法应用"));
    if (success) editor_->setText(value);
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
