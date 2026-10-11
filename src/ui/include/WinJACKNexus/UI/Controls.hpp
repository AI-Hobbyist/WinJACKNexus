/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/Export.hpp>
#include <QColor>
#include <QBrush>
#include <QAbstractButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QToolButton>
#include <QWidget>
#include <QHash>
#include <functional>
#include <optional>
#include <span>
#include <vector>
class QPainter;
class QPainterPath;
class QFont;
class QLabel;
class QMimeData;

namespace WinJACKNexus::UI::Controls
{
inline constexpr int sidebarControlHeight = 28;
/* Shared palette entry for native effect controls. Each module may override
   individual colors while retaining the same control shapes and spacing. */
struct WINJACK_UI_API Palette
{
    QColor background{"#0b1929"}, panel{"#14283e"}, text{"#e9f2ff"};
    QColor accent{"#31c6ff"}, border{"#29435d"};
};

WINJACK_UI_API void applyPalette(QWidget *widget, const Palette &colors);
WINJACK_UI_API QColor themeColor(const QWidget &widget, const char *key, const QColor &fallback);
/* Looks up a registered v1 color in the active process theme. The fallback
   preserves the current skin until a v1 package is activated. */
WINJACK_UI_API QColor themeTokenColor(const QString &scope, const QString &role,
                                      const QColor &fallback);
WINJACK_UI_API void drawSurface(QPainter &painter, const QRectF &bounds, const QBrush &fill,
                                const QColor &border, qreal radius, qreal borderWidth = 1.0);
WINJACK_UI_API void drawText(QPainter &painter, const QRectF &bounds, const QString &value,
                             const QFont &font, const QColor &color,
                             Qt::Alignment alignment = Qt::AlignCenter);
/* Elides each explicit line using the supplied font. On a QWidget painter,
   clipped text exposes its complete source through a native tooltip in the
   transformed hit rectangle. Call during paintEvent; no source value changes. */
WINJACK_UI_API void drawElidedText(QPainter &painter, const QRectF &bounds, const QString &value,
                                   const QFont &font, const QColor &color,
                                   Qt::Alignment alignment = Qt::AlignCenter);
WINJACK_UI_API void drawMarker(QPainter &painter, const QPointF &center, qreal radius,
                               const QBrush &fill, const QColor &border = {},
                               qreal borderWidth = 1.0);
WINJACK_UI_API void drawToggle(QPainter &painter, const QRectF &bounds, bool on,
                               const QColor &accent, const QColor &offTrack);
class WINJACK_UI_API ToggleButton final : public QAbstractButton
{
  public:
    explicit ToggleButton(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;
};
WINJACK_UI_API Palette paletteFor(const QWidget &widget, const Palette &defaults = {});
struct WINJACK_UI_API ButtonStyle
{
    QColor inactiveFill{"#192d43"};
    QColor activeFill{"#123e61"};
    QColor inactiveBorder{"#45617e"};
    QColor activeBorder{"#26bfff"};
    QColor inactiveText{"#aec7eb"};
    QColor activeText{"#d7f7ff"};
    qreal cornerRadius = 5.0;
    int fontSize = 15;
    bool bold = false;
};
WINJACK_UI_API void drawStyledButton(QPainter &painter, const QRectF &bounds,
                                     const QString &caption, bool selected,
                                     const ButtonStyle &style = {});
WINJACK_UI_API void drawButton(QPainter &painter, const QRectF &bounds, const QString &caption,
                               bool selected, const Palette &colors, int fontSize = 16);
WINJACK_UI_API void drawKnob(QPainter &painter, const QPointF &center, qreal normalized, int radius,
                             const Palette &colors);
/* Bipolar pan arc grows from the centered position toward L or R. pan is -1..1. */
WINJACK_UI_API void drawPanKnob(QPainter &painter, const QPointF &center, qreal pan, qreal radius,
                                const Palette &colors);
WINJACK_UI_API void drawSlider(QPainter &painter, const QRectF &bounds, qreal normalized,
                               bool vertical, const Palette &colors);
struct WINJACK_UI_API FaderStyle
{
    QColor accent{"#33b6ff"};
    QColor track{"#0a0f15"};
    QColor grip{"#07131f"};
    qreal thumbWidth = 28.0;
    qreal thumbHeight = 40.0;
    qreal cornerRadius = 5.0;
};
/* normalizedFromTop is zero at the top and one at the bottom of track. */
WINJACK_UI_API void drawFader(QPainter &painter, const QRectF &track, qreal normalizedFromTop,
                              const FaderStyle &style = {});
WINJACK_UI_API void drawChart(QPainter &painter, const QRectF &bounds, int columns, int rows,
                              const Palette &colors);
struct WINJACK_UI_API ChartGridStyle
{
    QColor background;
    QColor grid{"#29435d"};
    qreal lineWidth = 1.0;
    Qt::PenStyle penStyle = Qt::SolidLine;
    int columns = 0;
    int rows = 0;
    std::vector<qreal> verticalFractions;
    std::vector<qreal> horizontalFractions;
};
/* Fractions are in [0,1]; explicit fractions override uniform divisions. */
WINJACK_UI_API void drawChartGrid(QPainter &painter, const QRectF &bounds,
                                  const ChartGridStyle &style);
WINJACK_UI_API void drawDropdown(QPainter &painter, const QRectF &bounds, const QString &caption,
                                 const Palette &colors, int fontSize = 16,
                                 bool manualFilter = false);
struct WINJACK_UI_API MeterColors
{
    QColor low{"#2bce75"}, middle{"#f4cd42"}, high{"#f35656"};
    QColor inactiveLow{"#163e2e"}, inactiveMiddle{"#4a3f1d"}, inactiveHigh{"#4c242a"};
    QColor border{"#426181"};
};
struct WINJACK_UI_API MeterZone
{
    double upperDb = 12.0;
    QColor active{"#2bce75"};
    QColor inactive{"#163e2e"};
};
struct WINJACK_UI_API MeterTick
{
    double valueDb = 0.0;
    QString label;
};
struct WINJACK_UI_API MeterStyle
{
    double minimumDb = -72.0;
    double maximumDb = 12.0;
    std::vector<MeterZone> zones{{-12.0, QColor("#2bce75"), QColor("#163e2e")},
                                 {0.0, QColor("#f4cd42"), QColor("#4a3f1d")},
                                 {12.0, QColor("#f35656"), QColor("#4c242a")}};
    std::vector<MeterTick> ticks;
    QColor border{"#426181"};
    QColor peakColor{"#f7fbff"};
    QColor labelColor{"#afc7e5"};
    QColor tickColor{"#526763"};
    QString label;
    std::optional<double> fillOriginDb;
    bool vertical = false;
    bool showPeak = true;
    bool ticksAbove = false;
    bool ticksLeft = false;
    bool showTickGrid = false;
    qreal tickBandPixels = 16.0;
    int tickFontPixelSize = 10;
};
WINJACK_UI_API void drawMeter(QPainter &painter, const QRectF &bounds, double levelDb,
                              std::optional<double> peakDb, const MeterStyle &style);
WINJACK_UI_API void drawMeterScale(QPainter &painter, const QRectF &bounds,
                                   const MeterStyle &style);
/* Call once per displayed frame with the current sample and elapsed seconds.
   The peak stays for holdSeconds, then decays independently of frame rate. */
class WINJACK_UI_API MeterBallistics final
{
  public:
    void advance(double inputDb, double elapsedSeconds, double holdSeconds) noexcept;
    void reset() noexcept;
    double levelDb() const noexcept
    {
        return levelDb_;
    }
    double peakDb() const noexcept
    {
        return peakDb_;
    }

  private:
    double levelDb_ = -72.0;
    double peakDb_ = -72.0;
    double heldSeconds_ = 0.0;
};
struct WINJACK_UI_API CurveStyle
{
    QColor stroke{"#28c9f3"};
    QColor fill{0, 0, 0, 0};
    std::optional<QBrush> fillBrush;
    qreal width = 2.0;
    Qt::PenStyle penStyle = Qt::SolidLine;
    double minimum = 0.0;
    double maximum = 1.0;
    QString label;
    bool showFill = false;
    bool showStroke = true;
};
WINJACK_UI_API void drawCurvePath(QPainter &painter, const QPainterPath &strokePath,
                                  const CurveStyle &style, const QPainterPath *fillPath = nullptr);
WINJACK_UI_API void drawPolarCurve(QPainter &painter, QPointF center, qreal radius,
                                   std::span<const float> values, const CurveStyle &style);
WINJACK_UI_API void drawLevelMeter(QPainter &painter, const QRectF &bounds, double levelDb,
                                   const MeterColors &colors = {});
WINJACK_UI_API void drawCardLabel(QPainter &painter, const QRectF &bounds, const QString &title,
                                  const QString &glyph, const Palette &colors);

struct WINJACK_UI_API NumericControlSpec
{
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    int decimals = 1;
};
/* Overlay a value readout with an editor. Enter applies an in-range value;
   Escape or focus loss restores the painted readout without changing it. */
WINJACK_UI_API QLineEdit *editNumericValue(QWidget *owner, const QRect &readout,
                                           double currentValue, const NumericControlSpec &spec,
                                           std::function<void(double)> apply);
WINJACK_UI_API void resetNumericValue(const NumericControlSpec &spec,
                                      const std::function<void(double)> &apply);

/* Keep this registry inside the owning plugin instance. A plugin may register
   a factory for any local key and supply its own QWidget implementation. */
class WINJACK_UI_API ControlRegistry final
{
  public:
    using Factory = std::function<QWidget *(QWidget *parent)>;
    bool registerFactory(const QString &key, Factory factory);
    QWidget *create(const QString &key, QWidget *parent) const;

  private:
    QHash<QString, Factory> factories_;
};

/* Generic drag source for a sidebar tree. The caller supplies item-specific
   MIME data; ownership passes to QDrag for the duration of the drag. */
class WINJACK_UI_API DragTreeWidget final : public QTreeWidget
{
  public:
    using MimeDataFactory = std::function<QMimeData *(const QTreeWidgetItem *)>;
    explicit DragTreeWidget(QWidget *parent = nullptr);
    void setMimeDataFactory(MimeDataFactory factory);

  protected:
    void startDrag(Qt::DropActions supportedActions) override;

  private:
    MimeDataFactory mimeDataFactory_;
};

class WINJACK_UI_API TextField : public QLineEdit
{
  public:
    explicit TextField(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;
};

class WINJACK_UI_API SearchField final : public TextField
{
  public:
    explicit SearchField(QWidget *parent = nullptr);
};

class WINJACK_UI_API ReadOnlyField final : public TextField
{
  public:
    explicit ReadOnlyField(QWidget *parent = nullptr);
};

class WINJACK_UI_API ChoiceField final : public QComboBox
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
    Q_PROPERTY(QColor textColor MEMBER textColor_)
  public:
    explicit ChoiceField(QWidget *parent = nullptr);
    void setManualFilterEnabled(bool enabled);
    bool manualFilterEnabled() const noexcept;

  protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    bool manualFilterEnabled_ = false;
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
    QColor textColor_{"#e7f0ec"};
};

class WINJACK_UI_API ActionButton final : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
    Q_PROPERTY(QColor textColor MEMBER textColor_)
  public:
    explicit ActionButton(const QString &text, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
    QColor textColor_{"#e7f0ec"};
};

class WINJACK_UI_API DialogButtonBar final : public QDialogButtonBox
{
  public:
    explicit DialogButtonBar(QWidget *parent = nullptr);
    DialogButtonBar(StandardButtons buttons, QWidget *parent = nullptr);
    QPushButton *addButton(const QString &text, ButtonRole role);
    QPushButton *addButton(StandardButton button);
    QPushButton *button(StandardButton standard) const;

  protected:
    bool event(QEvent *event) override;
};

class WINJACK_UI_API IconActionButton final : public QToolButton
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
    Q_PROPERTY(QColor textColor MEMBER textColor_)
  public:
    explicit IconActionButton(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
    QColor textColor_{"#e7f0ec"};
};

class WINJACK_UI_API ZoomControl final : public QWidget
{
    Q_OBJECT
  public:
    explicit ZoomControl(QWidget *parent = nullptr);
    void setZoomFactor(qreal factor);
    qreal zoomFactor() const noexcept;
    QSize sizeHint() const override;
  Q_SIGNALS:
    void zoomInRequested();
    void zoomOutRequested();
    void zoomResetRequested();

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

  private:
    qreal factor_ = 1.0;
    int hoveredPart_ = -1;
};

class WINJACK_UI_API IntegerField final : public QSpinBox
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
  public:
    explicit IntegerField(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
};

class WINJACK_UI_API DecimalField final : public QDoubleSpinBox
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
  public:
    explicit DecimalField(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
};

class WINJACK_UI_API ToggleField final : public QCheckBox
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
    Q_PROPERTY(QColor textColor MEMBER textColor_)
  public:
    explicit ToggleField(const QString &text = {}, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
    QColor textColor_{"#e7f0ec"};
};

class WINJACK_UI_API RangeSlider final : public QSlider
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
  public:
    explicit RangeSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
};

class WINJACK_UI_API ProgressField final : public QProgressBar
{
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER accentColor_)
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor_)
    Q_PROPERTY(QColor borderColor MEMBER borderColor_)
    Q_PROPERTY(QColor textColor MEMBER textColor_)
  public:
    explicit ProgressField(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QColor accentColor_{"#43c7a8"};
    QColor surfaceColor_{"#263437"};
    QColor borderColor_{"#3b4d4d"};
    QColor textColor_{"#e7f0ec"};
};

class WINJACK_UI_API ScrollBarField final : public QScrollBar
{
  public:
    explicit ScrollBarField(Qt::Orientation orientation, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;
};

class WINJACK_UI_API ScrollField final : public QScrollArea
{
  public:
    explicit ScrollField(QWidget *parent = nullptr);
};

class WINJACK_UI_API ListField final : public QListWidget
{
  public:
    explicit ListField(QWidget *parent = nullptr);
};

class WINJACK_UI_API TableField final : public QTableWidget
{
  public:
    explicit TableField(QWidget *parent = nullptr);
};

class WINJACK_UI_API GroupPanel final : public QGroupBox
{
  public:
    explicit GroupPanel(const QString &title, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;
};

class WINJACK_UI_API TextLabel final : public QLabel
{
  public:
    explicit TextLabel(QWidget *parent = nullptr);
    TextLabel(const QString &value, QWidget *parent = nullptr);
    QSize minimumSizeHint() const override;

  protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
};

class WINJACK_UI_API TabField final : public QTabWidget
{
  public:
    explicit TabField(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;
};

class WINJACK_UI_API LogField final : public QPlainTextEdit
{
  public:
    explicit LogField(QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *event) override;
};

class WINJACK_UI_API SplitField final : public QSplitter
{
  public:
    explicit SplitField(Qt::Orientation orientation, QWidget *parent = nullptr);

  protected:
    QSplitterHandle *createHandle() override;
};

class WINJACK_UI_API DockPanel final : public QDockWidget
{
  public:
    DockPanel(const QString &title, Qt::DockWidgetArea area, QWidget *parent = nullptr);
    bool isCollapsed() const noexcept
    {
        return collapsed_;
    }
    void setCollapsed(bool collapsed);

  protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void updateGripGeometry();
    Qt::DockWidgetArea area_;
    QWidget *titleBar_ = nullptr;
    QWidget *resizeGrip_ = nullptr;
    bool collapsed_ = false;
    int expandedWidth_ = 270;
};

class WINJACK_UI_API ClientNameWidget final : public QWidget
{
  public:
    using ApplyHandler = std::function<bool(const QString &)>;
    ClientNameWidget(QString clientName, ApplyHandler handler, QWidget *parent = nullptr);
    void setClientName(const QString &clientName);

  private:
    void apply();
    void refreshStatus();
    enum class ApplyStatus
    {
        none,
        empty,
        applied,
        rejected
    };
    ApplyStatus applyStatus_ = ApplyStatus::none;
    QLineEdit *editor_ = nullptr;
    QLabel *status_ = nullptr;
    ApplyHandler handler_;
};
/* Effect editors keep their complete node layout. This host exposes the
   controls below the node-only channel strip for a mixer chain. */
class WINJACK_UI_API EmbeddedFxEditor final : public QWidget
{
  public:
    EmbeddedFxEditor(QWidget *editor, int stripHeight, QWidget *parent = nullptr);
    QWidget *editor() const noexcept
    {
        return editor_;
    }

  private:
    QPointer<QWidget> editor_;
};
}

namespace WinJACKNexus::UI
{
using GateClientNameWidget = Controls::ClientNameWidget;
}
