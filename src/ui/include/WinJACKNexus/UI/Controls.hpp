/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/Export.hpp>
#include <QColor>
#include <QAbstractButton>
#include <QPointer>
#include <QWidget>
#include <QHash>
#include <functional>
class QPainter;
class QLineEdit;
class QLabel;

namespace WinJACKNexus::UI::Controls {
/* Shared palette entry for native effect controls. Each module may override
   individual colors while retaining the same control shapes and spacing. */
struct WINJACK_UI_API Palette {
    QColor background {"#0b1929"}, panel {"#14283e"}, text {"#e9f2ff"};
    QColor accent {"#31c6ff"}, border {"#29435d"};
};

WINJACK_UI_API void applyPalette(QWidget* widget, const Palette& colors);
WINJACK_UI_API QColor themeColor(const QWidget& widget, const char* key, const QColor& fallback);
WINJACK_UI_API void drawToggle(QPainter& painter, const QRectF& bounds, bool on,
                               const QColor& accent, const QColor& offTrack);
class WINJACK_UI_API ToggleButton final : public QAbstractButton {
public:
    explicit ToggleButton(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
};
WINJACK_UI_API Palette paletteFor(const QWidget& widget, const Palette& defaults = {});
WINJACK_UI_API void drawButton(QPainter& painter, const QRectF& bounds, const QString& caption,
                               bool selected, const Palette& colors, int fontSize = 16);
WINJACK_UI_API void drawKnob(QPainter& painter, const QPointF& center, qreal normalized,
                             int radius, const Palette& colors);
/* Bipolar pan arc grows from the centered position toward L or R. pan is -1..1. */
WINJACK_UI_API void drawPanKnob(QPainter& painter, const QPointF& center, qreal pan,
                                qreal radius, const Palette& colors);
WINJACK_UI_API void drawSlider(QPainter& painter, const QRectF& bounds, qreal normalized,
                               bool vertical, const Palette& colors);
WINJACK_UI_API void drawChart(QPainter& painter, const QRectF& bounds, int columns,
                              int rows, const Palette& colors);
WINJACK_UI_API void drawDropdown(QPainter& painter, const QRectF& bounds, const QString& caption,
                                 const Palette& colors, int fontSize = 16);
struct WINJACK_UI_API MeterColors {
    QColor low {"#2bce75"}, middle {"#f4cd42"}, high {"#f35656"};
    QColor inactiveLow {"#163e2e"}, inactiveMiddle {"#4a3f1d"}, inactiveHigh {"#4c242a"};
    QColor border {"#426181"};
};
WINJACK_UI_API void drawLevelMeter(QPainter& painter, const QRectF& bounds,
                                   double levelDb, const MeterColors& colors = {});
WINJACK_UI_API void drawCardLabel(QPainter& painter, const QRectF& bounds,
                                  const QString& title, const QString& glyph,
                                  const Palette& colors);

/* Keep this registry inside the owning plugin instance. A plugin may register
   a factory for any local key and supply its own QWidget implementation. */
class WINJACK_UI_API ControlRegistry final {
public:
    using Factory = std::function<QWidget*(QWidget* parent)>;
    bool registerFactory(const QString& key, Factory factory);
    QWidget* create(const QString& key, QWidget* parent) const;
private:
    QHash<QString, Factory> factories_;
};

class WINJACK_UI_API ClientNameWidget final : public QWidget {
public:
    using ApplyHandler = std::function<bool(const QString&)>;
    ClientNameWidget(QString clientName, ApplyHandler handler, QWidget* parent = nullptr);
    void setClientName(const QString& clientName);
private:
    void apply();
    QLineEdit* editor_ = nullptr;
    QLabel* status_ = nullptr;
    ApplyHandler handler_;
};
/* Effect editors keep their complete node layout. This host exposes the
   controls below the node-only channel strip for a mixer chain. */
class WINJACK_UI_API EmbeddedFxEditor final : public QWidget {
public:
    EmbeddedFxEditor(QWidget* editor, int stripHeight, QWidget* parent = nullptr);
    QWidget* editor() const noexcept { return editor_; }
private:
    QPointer<QWidget> editor_;
};
}

namespace WinJACKNexus::UI {
using GateClientNameWidget = Controls::ClientNameWidget;
}
