#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <WinJACKNexus/UI/MixerPolarPlot.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeBackground.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/Common/Logger.hpp>

#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QColorDialog>
#include <QComboBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QFontMetricsF>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTimer>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>
#include <utility>

namespace WinJACKNexus::UI {
namespace {

struct Channel {
    QString name;
    QString source;
    QString number;
    QColor accent;
    qreal fader;
    qreal level;
    qreal target;
    bool aux = false;
    bool subMix = false;
    bool fx = false;
    bool mute = false;
    bool solo = false;
    Common::MixerListenMode listenMode { Common::MixerListenMode::off };
    bool sipMuted = false;
    bool postFaderMeter = false;
    std::uint32_t auxId {};
    std::uint32_t id {};
    Common::AuxChannelLayout layout { Common::AuxChannelLayout::mono };
    Common::AuxBusState selState;
    bool customColor {};
    qreal inputGainDb {};
    bool mixToMain = true;
    std::uint64_t modelId {};
    bool realMeter {};
    std::array<qreal, 8> channelLevels {};
    std::array<qreal, 8> channelPeaks {};
    std::array<qreal, 8> channelInputLevels {};
    std::array<qreal, 8> channelInputPeaks {};
    Controls::MeterBallistics meterBallistics;
    Controls::MeterBallistics inputMeterBallistics;
    Controls::MeterBallistics outputMeterBallistics;
    std::array<Controls::MeterBallistics, 8> channelBallistics;
    std::array<Controls::MeterBallistics, 8> channelInputBallistics;
    qreal inputMeterReadoutDb {-72.0};
    qreal outputMeterReadoutDb {-72.0};
};

QString auxSourceNumber(const Channel& channel)
{
    if(channel.auxId)return QStringLiteral("AUX %1").arg(channel.auxId);
    if(channel.number.startsWith(QStringLiteral("CH ")))return channel.number;
    return QStringLiteral("CH %1").arg(channel.number);
}

qreal faderDbValue(qreal position);

qreal selContentScale(const QSize& size)
{
    return std::min(size.width()/1394.0,size.height()/813.0);
}

qreal selDetailsLeft(const QSize& size)
{
    const qreal scale=selContentScale(size);
    const qreal rightRegionStart=313.0*scale;
    return rightRegionStart+(size.width()-rightRegionStart-1064.0*scale)/2.0;
}

class MonitorSelPage final : public QWidget {
public:
    explicit MonitorSelPage(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("monitorSelPage"));
        setMinimumSize(760, 520);
        levels.fill(-72.0);
        peaks.fill(-72.0);
        layoutButton = new Controls::ActionButton({}, this);
        layoutButton->setObjectName(QStringLiteral("monitorLayoutButton"));
        connect(layoutButton, &QPushButton::clicked, this, [this] {
            QMenu menu(layoutButton);
            const std::array layouts {Common::MixerLayout::mono, Common::MixerLayout::stereo,
                                      Common::MixerLayout::surround51, Common::MixerLayout::surround71};
            for (const auto candidate : layouts) {
                const auto name = candidate == Common::MixerLayout::mono ? QStringLiteral("单声道")
                    : candidate == Common::MixerLayout::stereo ? QStringLiteral("立体声")
                    : candidate == Common::MixerLayout::surround51 ? QStringLiteral("5.1")
                    : QStringLiteral("7.1");
                auto* action = menu.addAction(name);
                action->setCheckable(true);
                action->setChecked(candidate == layout);
                connect(action, &QAction::triggered, this, [this, candidate] {
                    if (layoutChanged) layoutChanged(candidate);
                });
            }
            menu.exec(layoutButton->mapToGlobal(QPoint(0, layoutButton->height())));
        });
        setLayoutMode(Common::MixerLayout::stereo);
    }

    void setLayoutMode(Common::MixerLayout next)
    {
        if (layout != next) {
            for (auto& meter : meterBallistics) meter.reset();
            levels.fill(-72.0);
            peaks.fill(-72.0);
        }
        layout = next;
        layoutButton->setText(next == Common::MixerLayout::mono ? QStringLiteral("单声道")
            : next == Common::MixerLayout::stereo ? QStringLiteral("立体声")
            : next == Common::MixerLayout::surround51 ? QStringLiteral("5.1")
            : QStringLiteral("7.1"));
        update();
    }
    void setContour(const Common::MixerPolarContour& next) { contour = next; }
    void advanceMeters(const std::array<qreal, 8>& samples, double frameSeconds,
                       double peakHoldSeconds)
    {
        for (std::size_t index = 0; index < meterBallistics.size(); ++index) {
            meterBallistics[index].advance(samples[index], frameSeconds, peakHoldSeconds);
            levels[index] = meterBallistics[index].levelDb();
            peaks[index] = meterBallistics[index].peakDb();
        }
    }
    std::function<void(Common::MixerLayout)> layoutChanged;

protected:
    void resizeEvent(QResizeEvent*) override
    {
        const QRectF sidebar(width() * .72, 70, width() * .25, height() - 95);
        layoutButton->setGeometry(QRectF(sidebar.left() + 20, sidebar.top() + 94,
                                          sidebar.width() - 40, 38).toAlignedRect());
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto themed = [](const char* role, const QColor& fallback) {
            return Controls::themeTokenColor(QStringLiteral("sel-main"),
                QString::fromLatin1(role), fallback);
        };
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), themed("surface-empty", QColor("#0b1a2b")));
        const QRectF header(14, 12, width() - 28, 48);
        const QRectF plot(14, 70, width() * .69, height() - 95);
        const QRectF sidebar(width() * .72, 70, width() * .25, height() - 95);
        const auto border = themed("monitor-border", QColor("#2b4662"));
        Controls::drawSurface(painter, header,
            themed("monitor-header-surface", QColor("#123a65")), border, 7);
        Controls::drawSurface(painter, plot,
            themed("monitor-plot-surface", QColor("#102235")), border, 7);
        Controls::drawSurface(painter, sidebar,
            themed("monitor-sidebar-surface", QColor("#102235")), border, 7);
        auto font = painter.font(); font.setPixelSize(20); font.setBold(true);
        Controls::drawText(painter, header, QStringLiteral("监听 SEL · MAIN"), font,
                           themed("text-primary", QColor("#e8f2ff")), Qt::AlignCenter);
        font.setPixelSize(16);
        Controls::drawText(painter, QRectF(sidebar.left() + 18, sidebar.top() + 16,
            sidebar.width() - 36, 30), QStringLiteral("通用"), font,
            themed("text-primary", QColor("#e8f2ff")));
        font.setBold(false); font.setPixelSize(14);
        Controls::drawText(painter, QRectF(sidebar.left() + 18, sidebar.top() + 63,
            sidebar.width() - 36, 24), QStringLiteral("监听通道模式"), font,
            themed("text-muted", QColor("#aec7eb")));
        const auto center = QPointF(plot.center().x(), plot.center().y() + 8);
        const auto radius = std::min(plot.width(), plot.height()) * .36;
        drawMixerPolarGrid(painter, center, radius);
        drawMixerPolarCurves(painter, plot, center, radius, contour, true, true);
        const auto referenceLayout = layout == Common::MixerLayout::mono ? Common::AuxChannelLayout::mono
            : layout == Common::MixerLayout::stereo ? Common::AuxChannelLayout::stereo
            : layout == Common::MixerLayout::surround51 ? Common::AuxChannelLayout::surround51
            : Common::AuxChannelLayout::surround71;
        const auto points = Common::auxLayoutReferencePoints(referenceLayout);
        const auto pointLabels = Common::mixerChannelLabels(layout);
        for (int index = 0; index < static_cast<int>(points.size()); ++index) {
            const auto point = center + points[index] * radius * .83;
            drawMixerPolarReferencePoint(painter, point, 1.0);
            Controls::drawText(painter, QRectF(point.x() - 20, point.y() + 14, 40, 20),
                pointLabels[index], font, themed("text-muted", QColor("#aec7eb")), Qt::AlignCenter);
        }
        drawMixerPolarCenter(painter, center);
        Controls::drawText(painter, QRectF(plot.left() + 20, plot.top() + 14,
            plot.width() - 40, 24), QStringLiteral("空间声像 · 只读"), font,
            themed("text-muted", QColor("#aec7eb")));
        const auto labels = Common::mixerChannelLabels(Common::MixerLayout::surround71);
        const auto meterTop = sidebar.top() + 166;
        const auto meterWidth = std::min<qreal>(24, (sidebar.width() - 32) / labels.size() - 5);
        for (int index = 0; index < labels.size(); ++index) {
            const auto x = sidebar.left() + 16 + index * ((sidebar.width() - 32) / labels.size());
            const QRectF meter(x, meterTop, meterWidth, std::max<qreal>(80, sidebar.bottom() - meterTop - 38));
            const auto channel = layout == Common::MixerLayout::mono
                ? (index == 2 ? 0 : -1)
                : (index < static_cast<int>(layout) ? index : -1);
            if (channel < 0) {
                Controls::drawSurface(painter, meter,
                    themed("monitor-meter-disabled-surface", QColor("#172538")),
                    themed("monitor-meter-disabled-border", QColor("#34475b")), 0);
                Controls::drawText(painter, QRectF(x - 4, meter.bottom() + 4, meterWidth + 8, 24),
                    labels[index], font, themed("monitor-meter-disabled-label", QColor("#879ab0")), Qt::AlignCenter);
                continue;
            }
            Controls::MeterStyle style;
            style.vertical = true;
            style.border = themed("meter-level-border", QColor("#07131f"));
            style.peakColor = themed("monitor-meter-peak", QColor("#f7fbff"));
            style.zones = {{-12, themed("monitor-meter-low-active", QColor("#2bce75")),
                                  themed("monitor-meter-low-inactive", QColor("#163e2e"))},
                           {0, themed("monitor-meter-mid-active", QColor("#f4cd42")),
                               themed("monitor-meter-mid-inactive", QColor("#4a3f1d"))},
                           {12, themed("monitor-meter-high-active", QColor("#f35656")),
                                themed("monitor-meter-high-inactive", QColor("#4c242a"))}};
            Controls::drawMeter(painter, meter, levels[channel], peaks[channel], style);
            Controls::drawText(painter, QRectF(x - 4, meter.bottom() + 4, meterWidth + 8, 24),
                labels[index], font, themed("text-muted", QColor("#aec7eb")), Qt::AlignCenter);
        }
    }
private:
    Controls::ActionButton* layoutButton {};
    Common::MixerLayout layout { Common::MixerLayout::stereo };
    Common::MixerPolarContour contour;
    std::array<qreal, 8> levels {};
    std::array<qreal, 8> peaks {};
    std::array<Controls::MeterBallistics, 8> meterBallistics;
};

class SelMainPage final : public QWidget {
public:
    explicit SelMainPage(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("selMainPage"));
        setMinimumSize(950, 650);
        name = new Controls::TextField(this);
        name->setObjectName(QStringLiteral("selChannelName"));
        comment = new Controls::TextField(this);
        comment->setObjectName(QStringLiteral("selChannelComment"));
        modeButton = new Controls::ActionButton({}, this);
        modeButton->setObjectName(QStringLiteral("selChannelMode"));
        modeArrow = new Controls::TextLabel(QStringLiteral("▾"), modeButton);
        modeArrow->setObjectName(QStringLiteral("selChannelModeArrow"));
        modeArrow->setAlignment(Qt::AlignCenter);
        modeArrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(modeButton, &QPushButton::clicked, this, [this] {
            if (!channel || channel->number == QStringLiteral("MAIN")) return;
            QMenu menu(modeButton);
            const std::array layouts {Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                      Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71};
            for (const auto layout : layouts) {
                auto* action = menu.addAction(Common::auxLayoutName(layout));
                action->setCheckable(true);
                action->setChecked(channel->layout == layout);
                connect(action, &QAction::triggered, this, [this, layout] {
                    if (layoutChanged) layoutChanged(layout);
                });
            }
            menu.exec(modeButton->mapToGlobal(QPoint(0, modeButton->height())));
        });
        colorButton = new Controls::ActionButton(QStringLiteral("..."), this);
        colorButton->setObjectName(QStringLiteral("selCustomColor"));
        for (auto* edit : { name, comment }) {
            connect(edit, &QLineEdit::editingFinished, this, [this] {
                if (!channel) return;
                channel->name = name->text().trimmed();
                channel->source = comment->text();
                if (changed) changed();
                if (detailsChanged) detailsChanged();
            });
        }
        connect(colorButton, &QPushButton::clicked, this, [this] {
            if (!channel) return;
            QColorDialog dialog(channel->accent, this);
            dialog.setWindowTitle(QStringLiteral("自定义通道颜色 · RGB / HEX"));
            dialog.setOption(QColorDialog::DontUseNativeDialog);
            dialog.setOption(QColorDialog::ShowAlphaChannel, false);
            if (dialog.exec() == QDialog::Accepted) {
                channel->accent = dialog.selectedColor();
                channel->customColor = true;
                update();
                if (changed) changed();
            }
        });
        applyTheme();
        if (auto* core = ThemeManager::installedThemeCore())
            connect(core, &ThemeCore::themeChanged, this, [this] { applyTheme(); update(); });
        new ThemeBackgroundOverlay(QStringLiteral("sel-main"), this);
    }

    static std::array<QColor, 8> colorOptions()
    {
        constexpr std::array<const char*, 8> roles {
            "control-color-option-1", "control-color-option-2",
            "control-color-option-3", "control-color-option-4",
            "control-color-option-5", "control-color-option-6",
            "control-color-option-7", "control-color-option-8"
        };
        constexpr std::array<const char*, 8> defaults {
            "#24b8ff", "#20c5dd", "#45d877", "#ffd246",
            "#ffae55", "#f34d70", "#e359da", "#9963ec"
        };
        std::array<QColor, 8> colors;
        for (std::size_t i = 0; i < colors.size(); ++i)
            colors[i] = Controls::themeTokenColor(QStringLiteral("sel-main"),
                QString::fromLatin1(roles[i]), QColor(QString::fromLatin1(defaults[i])));
        return colors;
    }

    void applyTheme()
    {
        const auto opacity = ThemeBackground::componentOpacity(QStringLiteral("sel-main"));
        for (auto* child : {static_cast<QWidget*>(name), static_cast<QWidget*>(comment),
                            static_cast<QWidget*>(modeButton), static_cast<QWidget*>(colorButton)}) {
            if (opacity < 1.0) {
                auto* effect = qobject_cast<QGraphicsOpacityEffect*>(child->graphicsEffect());
                if (!effect) { effect = new QGraphicsOpacityEffect(child); child->setGraphicsEffect(effect); }
                effect->setOpacity(opacity);
            } else if (child->graphicsEffect()) {
                child->setGraphicsEffect(nullptr);
            }
        }
        const auto css = [](const char* role, const char* fallback) {
            return Controls::themeTokenColor(QStringLiteral("sel-main"),
                QString::fromLatin1(role), QColor(QString::fromLatin1(fallback))).name();
        };
        QString modeStyle = QStringLiteral("QPushButton {background:%surface%;color:%text%;border:1px solid %border%;border-radius:5px;font-size:15px;text-align:left;padding:4px 30px 4px 12px;} QPushButton:disabled {color:%disabled-text%;border-color:%border%;}");
        modeStyle.replace(QStringLiteral("%surface%"), css("control-mode-surface", "#102033"));
        modeStyle.replace(QStringLiteral("%text%"), css("control-mode-text", "#eff6ff"));
        modeStyle.replace(QStringLiteral("%border%"), css("control-mode-border", "#36516d"));
        modeStyle.replace(QStringLiteral("%disabled-text%"), css("control-mode-text-disabled", "#879ab0"));
        modeButton->setStyleSheet(modeStyle);
        modeArrow->setStyleSheet(QStringLiteral("color:%1;background:transparent;font-size:15px;")
            .arg(css("control-mode-arrow", "#d6eaff")));
        QString editStyle = QStringLiteral("QLineEdit {background:%surface%;color:%text%;border:1px solid %border%;border-radius:5px;padding:4px 12px;font-size:15px;}");
        editStyle.replace(QStringLiteral("%surface%"), css("control-edit-surface", "#102033"));
        editStyle.replace(QStringLiteral("%text%"), css("control-edit-text", "#eff6ff"));
        editStyle.replace(QStringLiteral("%border%"), css("control-edit-border", "#36516d"));
        name->setStyleSheet(editStyle);
        comment->setStyleSheet(editStyle);
        QString colorStyle = QStringLiteral("QPushButton {background:%surface%;color:%text%;border:1px solid %border%;border-radius:5px;font-size:15px;}");
        colorStyle.replace(QStringLiteral("%surface%"), css("control-color-button-surface", "#172a40"));
        colorStyle.replace(QStringLiteral("%text%"), css("control-color-button-text", "#d6eaff"));
        colorStyle.replace(QStringLiteral("%border%"), css("control-color-button-border", "#50708c"));
        colorButton->setStyleSheet(colorStyle);
    }

    void setChannel(Channel* next)
    {
        channel = next;
        if (channel) {
            const bool switched=displayedId!=channel->id;
            if(switched){displayedId=channel->id;selectedPoint=0;polarContour={};}
            if (switched || !name->hasFocus()) name->setText(channel->name);
            if (switched || !comment->hasFocus()) comment->setText(channel->source);
            modeButton->setText(Common::auxLayoutName(channel->layout));
            modeButton->setEnabled(channel->number != QStringLiteral("MAIN"));
            modeArrow->setVisible(channel->number != QStringLiteral("MAIN"));
            auto& state=channel->selState;
            const bool fresh=state.sends.size()!=8;
            const bool wasEligible=Common::auxSupportsClassicPan(state.sourceLayout,state.targetLayout);
            const bool eligible=Common::auxSupportsClassicPan(channel->layout,
                channel->realMeter && !channel->subMix ? Common::AuxChannelLayout::stereo : channel->layout);
            if(fresh || wasEligible!=eligible)
                state.panMode=eligible?Common::AuxPanMode::classic:Common::AuxPanMode::spatial;
            selectedPoint=std::clamp(selectedPoint,0,std::max(0,static_cast<int>(Common::auxLayoutChannelLabels(channel->layout).size())-1));
            if(!Common::auxHasSpatialPoint(channel->layout,selectedPoint))selectedPoint=0;
            if(state.adjustablePoints.size()!=static_cast<std::size_t>(Common::auxLayoutChannelLabels(channel->layout).size()))
                state.adjustablePoints=Common::auxLayoutReferencePoints(channel->layout);
            if(state.pointInfluenceRadii.size()!=state.adjustablePoints.size())
                state.pointInfluenceRadii.resize(state.adjustablePoints.size(),0.1);
            state.sourceLayout=channel->layout;
            if (!channel->realMeter) state.targetLayout=channel->layout;
            setProperty("selPanMode",static_cast<int>(state.panMode));
            setProperty("selClassicPan",state.classicPan);
            setProperty("selChannelLayout",static_cast<int>(channel->layout));
            setProperty("selActiveSendCount",Common::auxLayoutChannelLabels(channel->layout).size());
            setProperty("selInputGainDb",channel->inputGainDb);
            if(state.sends.size()!=8) {
                const auto labels = WinJACKNexus::Common::standardChannelLabels(8);
                state.sends.resize(8);
                for(std::size_t i=0;i<labels.size();++i) {
                    state.sends[i].label=labels[i];state.sends[i].name=labels[i];
                    state.sends[i].levelDb=0.0;state.sends[i].meterDb=-12.0;
                }
            }
            linked=state.sendsLinked;
            publishLevels();
            publishPanner();
        }
        update();
    }
    void setPolarContour(const Common::MixerPolarContour& value) {
        polarContour = value;
        setProperty("selPolarDataValid", value.valid);
        if (isVisible()) update();
    }
    std::function<void()> changed;
    std::function<void()> dragFinished;
    std::function<void()> detailsChanged;
    std::function<void(Common::AuxChannelLayout)> layoutChanged;
    bool draggingControl() const
    {
        return draggedFader >= 0 || draggedPoint >= 0 || draggedInfluencePoint >= 0
            || draggedKnob >= 0 || draggedInputGain || draggedClassicPan;
    }

protected:
    void resizeEvent(QResizeEvent*) override
    {
        const qreal scale=contentScale();const QPointF origin=contentOrigin();
        const auto place = [scale,origin](QWidget* widget, int x, int y, int w, int h) {
            widget->setGeometry(qRound(origin.x()+x*scale),qRound(origin.y()+y*scale),qRound(w*scale),qRound(h*scale));
        };
        place(name, 29, 49, 254, 34);
        place(comment, 29, 129, 254, 34);
        place(modeButton, 29, 209, 254, 34);
        modeArrow->setGeometry(modeButton->width()-qRound(24*scale), 0,
                               qRound(18*scale), modeButton->height());
        place(colorButton, 252, 289, 31, 34);
    }

    void paintEvent(QPaintEvent*) override
    {
        const auto themed = [](const char* role, const QColor& fallback) {
            return Controls::themeTokenColor(QStringLiteral("sel-main"), QString::fromLatin1(role), fallback);
        };
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), themed("surface-empty", QColor("#0b1a2b")));
        ThemeBackground::paint(p, rect(), QStringLiteral("sel-main"));
        p.setOpacity(ThemeBackground::componentOpacity(QStringLiteral("sel-main")));
        p.translate(contentOrigin());
        p.scale(contentScale(), contentScale());
        p.fillRect(QRect(0, 0, 1394, 813), themed("surface-empty", QColor("#0b1a2b")));
        const QColor border=channel&&channel->customColor?channel->accent
            : themed("border-panel",QColor("#2b4662"));
        const QColor ink=themed("text-primary",QColor("#e8f2ff"));
        const QColor muted=themed("text-muted",QColor("#aec7eb"));
        const QColor blue=themed("control-knob-indicator-normal",QColor("#26bfff"));
        const auto knobColors=Controls::paletteFor(*this,
            {themed("control-knob-background-base",QColor("#0b1a2b")),
             themed("control-knob-panel",QColor("#1b3b57")),
             themed("control-knob-text",QColor("#d9f4ff")),blue,
             themed("control-knob-track",QColor("#254762"))});
        auto panel = [&](QRectF r) { Controls::drawSurface(p,r,themed("surface-panel",QColor("#102235")),border,7); };
        auto text = [&](QRectF r, const QString& s, int size=15, QColor c=QColor("#e8f2ff"), bool bold=false, int align=Qt::AlignLeft|Qt::AlignVCenter) {
            QFont f=p.font(); f.setPixelSize(size); f.setBold(bold);
            Controls::drawText(p,r,s,f,c,Qt::Alignment(align));
        };
        auto button = [&](QRectF r, const QString& label, bool active=false, QColor accent=QColor("#26bfff")) {
            Controls::ButtonStyle style;
            style.activeBorder = accent;
            style.inactiveText = muted;
            Controls::drawStyledButton(p, r, label, active, style);
        };
        auto toggle = [&](QRectF r, bool checked) {
            Controls::drawToggle(p, r, checked,
                themed("control-toggle-indicator-on",QColor("#168dff")),
                themed("control-toggle-indicator-off",QColor("#263e53")));
        };
        auto modeToggle = [&](QRectF r,const QString& label,bool checked,QColor accent) {
            Controls::ButtonStyle style;
            style.inactiveBorder = style.activeBorder = accent;
            style.inactiveFill = style.activeFill = accent;
            style.inactiveFill.setAlpha(25);
            style.activeFill.setAlpha(85);
            style.inactiveText = style.activeText =
                themed("control-mute-solo-text",QColor("#f7f8ff"));
            style.fontSize = 16;
            style.bold = true;
            Controls::drawStyledButton(p, r, label, checked, style);
        };
        panel({14, 11, 287, 790});
        text({29, 22, 240, 21}, QStringLiteral("通道名"), 14, ink, true);
        text({29, 102, 240, 21}, QStringLiteral("注释"), 14, ink, true);
        text({29, 182, 240, 21}, QStringLiteral("通道模式"), 14, ink, true);
        text({29, 262, 240, 21}, QStringLiteral("通道颜色"), 14, ink, true);
        const auto colors = colorOptions();
        Controls::drawSurface(p,QRectF(29,289,216,34),
            themed("control-color-picker-surface",QColor("#15263a")),border,5);
        for (int i=0;i<8;++i) { p.setPen(Qt::NoPen); p.setBrush(colors[i]); p.drawEllipse(QPointF(47+i*26,306),9,9); }
        text({29, 327, 254, 22}, QStringLiteral("输入增益  -12 ~ +12 dB"), 14, muted, false, Qt::AlignCenter);
        const qreal gain=channel?std::clamp(channel->inputGainDb,-12.0,12.0):0.0;
        const qreal gainFraction=(gain+12.0)/24.0;
        Controls::drawKnob(p,{93,378},gainFraction,23,knobColors);
        text({126, 362, 126, 28}, QStringLiteral("%1%2 dB").arg(gain>=0?QStringLiteral("+"):QString()).arg(gain,0,'f',1),
             17, ink, true, Qt::AlignCenter);
        const bool mainOutput=channel && channel->number==QStringLiteral("MAIN");
        modeToggle({29,407,mainOutput?254.0:118.0,34},QStringLiteral("M"),channel && (channel->mute || channel->sipMuted),
            themed("control-mute-accent",QColor("#ff4b62")));
        if(!mainOutput)modeToggle({165,407,118,34},QStringLiteral("S"),channel && channel->solo,
            themed("control-solo-accent",QColor("#ffae2d")));
        if(channel && channel->subMix) {
            modeToggle({29,446,118,28},QStringLiteral("PFL"),channel->listenMode==Common::MixerListenMode::pfl,
                themed("control-pfl-accent",QColor("#ffe15a")));
            modeToggle({165,446,118,28},QStringLiteral("AFL"),channel->listenMode==Common::MixerListenMode::afl,
                themed("control-afl-accent",QColor("#ffe15a")));
        } else text({29, 449, 200, 26}, QStringLiteral("电平表"), 14, ink, true);
        for(int m=0;m<2;++m) {
            const int x=24+m*138; panel({qreal(x),480,125,318});
            text({qreal(x),488,125,31},m?QStringLiteral("输出"):QStringLiteral("输入"),16,ink,false,Qt::AlignCenter);
            constexpr qreal meterTop=528, meterBottom=745;
            const qreal current=channel ? (m ? channel->outputMeterBallistics.levelDb()
                                                 : channel->inputMeterBallistics.levelDb()) : -72.0;
            const qreal readout=channel
                ? (m ? channel->outputMeterReadoutDb : channel->inputMeterReadoutDb) : -72.0;
            const qreal peak=channel ? (m ? channel->outputMeterBallistics.peakDb()
                                               : channel->inputMeterBallistics.peakDb()) : -72.0;
            Controls::MeterStyle meterStyle;
            meterStyle.vertical=true;
            meterStyle.border=themed("meter-level-border",QColor("#07141f"));
            meterStyle.zones={{-12,themed("meter-level-zone-low-active",QColor("#17d675")),
                                   themed("meter-level-zone-low-inactive",QColor("#123e2d"))},
                              {0,themed("meter-level-zone-mid-active",QColor("#f2df48")),
                                 themed("meter-level-zone-mid-inactive",QColor("#483d1e"))},
                              {12,themed("meter-level-zone-high-active",QColor("#f2484d")),
                                  themed("meter-level-zone-high-inactive",QColor("#4a2429"))}};
            auto scaleStyle=meterStyle;
            scaleStyle.ticks={{12,QStringLiteral("12")},{0,QStringLiteral("0")},
                              {-12,QStringLiteral("-12")},{-24,QStringLiteral("-24")},
                              {-48,QStringLiteral("-48")},{-72,QStringLiteral("-72")}};
            scaleStyle.ticksLeft=true;
            scaleStyle.tickBandPixels=46;
            scaleStyle.tickFontPixelSize=12;
            scaleStyle.labelColor=muted;
            Controls::drawMeterScale(p,{qreal(x+14),meterTop,50,meterBottom-meterTop},scaleStyle);
            Controls::drawMeter(p,{qreal(x+67),meterTop,28,meterBottom-meterTop},
                                current,peak,meterStyle);
            text({qreal(x+22),757,96,31},QStringLiteral("%1 dB").arg(readout,0,'f',1),15,ink,false,Qt::AlignCenter);
        }
        p.translate(selDetailsLeft(size())/contentScale()-313.0,0);
        panel({313, 11, 650, 544});
        panel({313, 566, 650, 235});
        panel({974, 11, 403, 790});
        text({330,20,250,29},QStringLiteral("声像器"),17,ink,true);
        const auto* pannerBus=selectedBus();
        const bool classic=pannerBus&&pannerBus->panMode==Common::AuxPanMode::classic;
        const bool canUseClassic=channel&&Common::auxSupportsClassicPan(channel->layout,channel->layout);
        button({330,52,137,34},QStringLiteral("空间声像器"),!classic);
        p.save();if(!canUseClassic)p.setOpacity(.32);
        button({477,52,137,34},QStringLiteral("经典 Pan"),classic);
        p.restore();
        if(!classic){
            text({638,52,118,34},QStringLiteral("显示极坐标曲线"),14,muted);
            toggle({761,56,42,25},pannerBus&&pannerBus->showPolarCurves);
            if(pannerBus&&pannerBus->showPolarCurves){
                button({809,52,58,34},QStringLiteral("输入"),pannerBus->showInputCurve,
                       themed("chart-series-polar-input-stroke",QColor("#28c9f3")));
                button({872,52,58,34},QStringLiteral("输出"),pannerBus->showOutputCurve,
                       themed("chart-series-polar-output-stroke",QColor("#ffa44a")));
            }
        }
        panel({330,95,616,448});
        const QPointF center(638,319); const qreal radius=190;
        const qreal pointRadius=159;
        if(!classic){
        drawMixerPolarGrid(p,center,radius,QStringLiteral("sel-main"));
        const auto outputGains=pannerBus&&selectedPoint<static_cast<int>(pannerBus->adjustablePoints.size())
            ? Common::auxSpatialOutputGains(*pannerBus,selectedPoint):std::vector<double>{};
        const auto referenceLayout=channel?channel->layout:Common::AuxChannelLayout::stereo;
        const auto referencePoints=Common::auxLayoutReferencePoints(referenceLayout);
        for(int i=0;i<static_cast<int>(referencePoints.size());++i){
            if(!Common::auxHasSpatialPoint(referenceLayout,i))continue;
            const auto gain=i<static_cast<int>(outputGains.size())?outputGains[i]:0.0;
            drawMixerPolarReferencePoint(p,center+referencePoints[i]*pointRadius,gain,QStringLiteral("sel-main"));
        }
        if(pannerBus&&pannerBus->showPolarCurves&&channel) {
            drawMixerPolarCurves(p,{330,95,616,448},center,radius,polarContour,
                                 pannerBus->showInputCurve,pannerBus->showOutputCurve);
            if(!polarContour.valid) text({center.x()-62,center.y()-12,124,24},
                                         QStringLiteral("无信号"),13,muted,false,Qt::AlignCenter);
        }
        if(pannerBus){
            for(int i=0;i<static_cast<int>(pannerBus->adjustablePoints.size());++i){
                if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                const auto& point=pannerBus->adjustablePoints[i];
                const QPointF screen=center+point*pointRadius;
                const bool selected=i==selectedPoint;
                const auto influence=pannerBus->pointInfluenceRadii.size()>static_cast<std::size_t>(i)
                    ? pannerBus->pointInfluenceRadii[i]:0.1;
                const auto outerRadius=Common::auxInfluencePixelRadius(influence);
                drawMixerPolarHandle(p,screen,outerRadius,selected,QStringLiteral("sel-main"));
                const auto labels=Common::auxLayoutChannelLabels(channel->layout);
                if(i<labels.size())text({screen.x()-17,screen.y()+14,34,19},labels[i],12,ink,true,Qt::AlignCenter);
            }
        }
        drawMixerPolarCenter(p,center,QStringLiteral("sel-main"));
        text({center.x()-22,105,44,20},QStringLiteral("0°"),12,muted,false,Qt::AlignCenter);
        text({center.x()+radius+8,309,45,20},QStringLiteral("90°"),12,muted);
        text({center.x()-radius-53,309,45,20},QStringLiteral("270°"),12,muted);
        text({center.x()-24,516,48,20},QStringLiteral("180°"),12,muted,false,Qt::AlignCenter);
        }else{
            Controls::Palette panColors;
            panColors.border=themed("control-pan-border",QColor("#254762"));panColors.accent=blue;
            panColors.panel=themed("control-knob-panel",QColor("#1b3b57"));
            panColors.text=themed("control-knob-text",QColor("#d9f4ff"));
            Controls::drawPanKnob(p,center,pannerBus?pannerBus->classicPan:0.0,170,panColors);
            text({center.x()-185,center.y()+186,85,26},QStringLiteral("L100%"),15,muted,true,Qt::AlignCenter);
            text({center.x()-25,center.y()+186,50,26},QStringLiteral("C"),17,muted,true,Qt::AlignCenter);
            text({center.x()+100,center.y()+186,85,26},QStringLiteral("R100%"),15,muted,true,Qt::AlignCenter);
            text({center.x()-115,center.y()-205,230,30},QStringLiteral("声像"),19,ink,true,Qt::AlignCenter);
            text({center.x()-110,center.y()+20,220,35},QStringLiteral("%1%2%").arg(pannerBus&&pannerBus->classicPan>0?QStringLiteral("R "):pannerBus&&pannerBus->classicPan<0?QStringLiteral("L "):QStringLiteral("C ")).arg(qRound(std::abs(pannerBus?pannerBus->classicPan:0.0)*100)),16,ink,true,Qt::AlignCenter);
        }
        text({330,574,350,27},QStringLiteral("空间声像参数"),17,ink,true);
        p.save();if(classic)p.setOpacity(.32);
        const std::array<QString,3> parameters {QStringLiteral("宽度"),QStringLiteral("前后"),QStringLiteral("角度")};
        for(int i=0;i<3;++i){
            const qreal x=330+i*206;
            panel({x,609,194,178});
            text({x+12,616,170,25},parameters[i],15,ink,true,Qt::AlignCenter);
            const qreal value=i==0?(pannerBus?pannerBus->width:1.0):i==1?(pannerBus&&!pannerBus->adjustablePoints.empty()?(pannerBus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(pannerBus->adjustablePoints.size())-1)].y()+1)/2:.5):(pannerBus?(pannerBus->angleDegrees+180)/360:.5);
            Controls::drawKnob(p,{x+97,694},value,35,knobColors);
            const QString readout=i==0?QStringLiteral("%1%").arg(qRound(pannerBus?pannerBus->width*100:100)):
                i==1?QStringLiteral("%1%").arg(qRound(pannerBus&&!pannerBus->adjustablePoints.empty()?pannerBus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(pannerBus->adjustablePoints.size())-1)].y()*100:0)):
                     QStringLiteral("%1°").arg(qRound(pannerBus?pannerBus->angleDegrees:0));
            text({x+18,748,158,26},readout,15,ink,true,Qt::AlignCenter);
        }
        p.restore();
        text({991,20,190,29},QStringLiteral("分控旋钮"),17,ink,true);
        text({992,54,81,28},QStringLiteral("全通道链接"),13,muted);
        toggle({1077,56,42,24},linked);
        button({1256,51,105,34},QStringLiteral("重置"));
        const auto* bus=pannerBus;
        const auto labels = WinJACKNexus::Common::standardChannelLabels(8);
        const int activeCount=channel?Common::auxLayoutChannelLabels(channel->layout).size():0;
        for(int i=0;i<activeCount;++i) {
            p.save();
            const qreal x=990, y=100+i*86;
            panel({x,y,370,80});
            text({x+10,y+23,35,28},i==0&&channel&&channel->layout==Common::AuxChannelLayout::mono
                     ? QStringLiteral("M") : labels[i],15,ink,true,Qt::AlignCenter);
            if(i==3&&i<activeCount)text({x+7,y+48,41,16},QStringLiteral("强度"),11,muted,false,Qt::AlignCenter);
            const qreal sendLevel=bus&&i<static_cast<int>(bus->sends.size())?bus->sends[i].levelDb:0.0;
            const qreal sourceDb = channel && channel->realMeter
                ? channel->channelInputLevels[static_cast<std::size_t>(i)]
                : channel ? channel->level + channel->inputGainDb : -72.0;
            const qreal meterDb=std::clamp(sourceDb+sendLevel,-72.0,12.0);
            Controls::drawKnob(p,{x+83,y+32},(sendLevel+72)/84.0,23,knobColors);
            text({x+42,y+57,82,18},QStringLiteral("%1 dB").arg(sendLevel,0,'f',1),12,ink,true,Qt::AlignCenter);
            Controls::MeterStyle sendMeter;
            sendMeter.border=themed("meter-aux-send-border",QColor("#06131f"));
            sendMeter.zones={{-12,themed("meter-aux-send-zone-low-active",QColor("#17d675")),
                                  themed("meter-aux-send-zone-low-inactive",QColor("#102234"))},
                             {0,themed("meter-aux-send-zone-mid-active",QColor("#f2df48")),
                                themed("meter-aux-send-zone-mid-inactive",QColor("#102234"))},
                             {12,themed("meter-aux-send-zone-high-active",QColor("#f2484d")),
                                 themed("meter-aux-send-zone-high-inactive",QColor("#102234"))}};
            sendMeter.ticks={{-72,QStringLiteral("-72")},{-48,QStringLiteral("-48")},
                             {-24,QStringLiteral("-24")},{-12,QStringLiteral("-12")},
                             {0,QStringLiteral("0")},{12,QStringLiteral("+12")}};
            sendMeter.labelColor=muted;
            sendMeter.ticksAbove=true;
            sendMeter.tickBandPixels=24;
            const qreal sendPeak=channel&&channel->realMeter
                ? std::clamp(channel->channelInputPeaks[static_cast<std::size_t>(i)]+sendLevel,-72.0,12.0)
                : meterDb;
            Controls::drawMeter(p,{x+126,y+3,226,33},meterDb,sendPeak,sendMeter);
            modeToggle({x+215,y+45,62,29},QStringLiteral("M"),bus&&i<static_cast<int>(bus->sends.size())&&bus->sends[i].mute,
                themed("control-mute-accent",QColor("#ff4b62")));
            modeToggle({x+286,y+45,62,29},QStringLiteral("S"),bus&&i<static_cast<int>(bus->sends.size())&&bus->sends[i].solo,
                themed("control-solo-accent",QColor("#ffae2d")));
            p.restore();
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!channel || event->button()!=Qt::LeftButton) return;
        const QPointF pt=designPoint(event->position());
        const auto channelId = channel->id;
        if (QRectF(126,362,126,28).contains(pt)) {
            Controls::editNumericValue(this, readoutRect({126,362,126,28}), channel->inputGainDb,
                {-12,12,0,1}, [this,channelId](double value) {
                    if (!channel || channel->id != channelId) return;
                    channel->inputGainDb=value;
                    setProperty("selInputGainDb", value);
                    update(); if(changed) changed();
                });
            return;
        }
        if (auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::spatial) {
            for (int knob=0;knob<3;++knob) {
                const qreal x=330+knob*206;
                const QRectF readout(x+18,748,158,26);
                if (!readout.contains(pt)) continue;
                const auto point=std::clamp(selectedPoint,0,static_cast<int>(bus->adjustablePoints.size())-1);
                const double current=knob==0?bus->width*100
                    :knob==1&&!bus->adjustablePoints.empty()?bus->adjustablePoints[point].y()*100
                    :bus->angleDegrees;
                const Controls::NumericControlSpec spec=knob==0
                    ? Controls::NumericControlSpec{0,100,100,0}
                    :knob==1?Controls::NumericControlSpec{-100,100,0,0}
                    :Controls::NumericControlSpec{-180,180,0,0};
                Controls::editNumericValue(this,readoutRect(readout),current,spec,
                    [this,channelId,knob](double value) {
                        if (!channel || channel->id!=channelId) return;
                        auto* currentBus=selectedBus(); if(!currentBus) return;
                        setKnob(*currentBus,knob,knob==0?value/100
                            :knob==1?(value+100)/200:(value+180)/360);
                        update(); if(changed) changed();
                    });
                return;
            }
        }
        if (auto* bus=selectedBus();bus&&pt.x()>=1032&&pt.x()<1114
            &&pt.y()>=100&&pt.y()<788) {
            const int index=static_cast<int>((pt.y()-100)/86);
            const qreal y=100+index*86;
            if(index>=0&&index<static_cast<int>(bus->sends.size())
                &&index<Common::auxLayoutChannelLabels(channel->layout).size()
                &&QRectF(1032,y+57,82,18).contains(pt)) {
                Controls::editNumericValue(this,readoutRect({1032,y+57,82,18}),
                    bus->sends[index].levelDb,{-72,12,0,1},
                    [this,channelId,index](double value) {
                        if (!channel || channel->id!=channelId) return;
                        if(auto* currentBus=selectedBus()) {
                            setSendLevel(*currentBus,index,value);
                            update(); if(changed) changed();
                        }
                    });
                return;
            }
        }
        const auto colors = colorOptions();
        if (pt.y()>=289&&pt.y()<323&&pt.x()>=29&&pt.x()<245) {const int i=int((pt.x()-34)/26);if(i>=0&&i<8){channel->accent=colors[i];channel->customColor=true;}}
        else if(QRectF(65,351,200,51).contains(pt)) {
            draggedInputGain=true;
            inputGainStartY=pt.y();
            inputGainStartValue=channel->inputGainDb;
            return;
        }
        else if(QRectF(29,407,channel->number==QStringLiteral("MAIN")?254.0:118.0,34).contains(pt)) channel->mute=!channel->mute;
        else if(channel->number!=QStringLiteral("MAIN")&&QRectF(165,407,118,34).contains(pt)) channel->solo=!channel->solo;
        else if(channel->subMix&&QRectF(29,446,118,28).contains(pt))
            channel->listenMode=channel->listenMode==Common::MixerListenMode::pfl?Common::MixerListenMode::off:Common::MixerListenMode::pfl;
        else if(channel->subMix&&QRectF(165,446,118,28).contains(pt))
            channel->listenMode=channel->listenMode==Common::MixerListenMode::afl?Common::MixerListenMode::off:Common::MixerListenMode::afl;
        else if(QRectF(330,52,137,34).contains(pt)){if(auto* bus=selectedBus()){bus->panMode=Common::AuxPanMode::spatial;setProperty("selPanMode",static_cast<int>(bus->panMode));}}
        else if(QRectF(477,52,137,34).contains(pt)){if(auto* bus=selectedBus();bus&&Common::auxSupportsClassicPan(bus->sourceLayout,bus->targetLayout)){bus->panMode=Common::AuxPanMode::classic;setProperty("selPanMode",static_cast<int>(bus->panMode));}}
        else if(QRectF(638,52,165,34).contains(pt)){if(auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::spatial){bus->showPolarCurves=!bus->showPolarCurves;publishPanner();}}
        else if(QRectF(809,52,58,34).contains(pt)){if(auto* bus=selectedBus();bus&&bus->showPolarCurves){bus->showInputCurve=!bus->showInputCurve;publishPanner();}}
        else if(QRectF(872,52,58,34).contains(pt)){if(auto* bus=selectedBus();bus&&bus->showPolarCurves){bus->showOutputCurve=!bus->showOutputCurve;publishPanner();}}
        else if(QRectF(1256,51,105,34).contains(pt)) {if(auto* bus=selectedBus()){bus->adjustablePoints=Common::auxLayoutReferencePoints(channel->layout);bus->pointInfluenceRadii.assign(bus->adjustablePoints.size(),0.1);bus->width=1.0;bus->angleDegrees=0;bus->classicPan=0;setProperty("selClassicPan",0.0);const auto active=Common::auxLayoutChannelLabels(channel->layout).size();for(int i=0;i<active;++i)bus->sends[i].levelDb=0;publishLevels();publishPanner();}}
        else if(QRectF(992,51,127,34).contains(pt)) {linked=!linked;if(auto* bus=selectedBus())bus->sendsLinked=linked;}
        else if(pt.y()>=650&&pt.y()<739&&pt.x()>=330&&pt.x()<936){
            if(auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::spatial){
                const int knob=int((pt.x()-330)/206);
                if(knob>=0&&knob<3){
                    const qreal x=330+knob*206;
                    if(QRectF(x+56,653,82,82).contains(pt)){
                        draggedKnob=knob;
                        parameterStartY=pt.y();
                        parameterStartValue=knob==0?bus->width:knob==1&&!bus->adjustablePoints.empty()?
                            (bus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus->adjustablePoints.size())-1)].y()+1)/2:
                            (bus->angleDegrees+180)/360;
                    }
                }
            }
        }
        else if(QRectF(448,129,380,380).contains(pt)) {
            if(auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::classic){
                if(QRectF(463,144,350,350).contains(pt)){
                    draggedClassicPan=true;classicPanStartY=pt.y();classicPanStartValue=bus->classicPan;
                }
            }else if(bus&&!bus->adjustablePoints.empty()){
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                    if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                    const auto& point=bus->adjustablePoints[i];
                    if(std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159)<=12.0){
                        selectedPoint=i;draggedPoint=i;publishPanner();update();return;
                    }
                }
                int influenceIndex=-1;
                qreal closestInfluence=9.0;
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                    if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                    const auto& point=bus->adjustablePoints[i];
                    const auto distance=std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159);
                    const auto influence=bus->pointInfluenceRadii.size()>static_cast<std::size_t>(i)
                        ? bus->pointInfluenceRadii[i]:0.1;
                    const auto error=std::abs(distance-Common::auxInfluencePixelRadius(influence));
                    if(error<closestInfluence){closestInfluence=error;influenceIndex=i;}
                }
                if(influenceIndex>=0){selectedPoint=influenceIndex;draggedInfluencePoint=influenceIndex;
                    publishPanner();update();if(changed)changed();return;}
                qreal nearest=1e9;int nearestIndex=0;
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                    if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                    const auto& point=bus->adjustablePoints[i];const qreal dx=pt.x()-638-point.x()*159,dy=pt.y()-319-point.y()*159;const qreal distance=dx*dx+dy*dy;if(distance<nearest){nearest=distance;nearestIndex=i;}
                }
                selectedPoint=nearestIndex;draggedPoint=nearestIndex;
                bus->adjustablePoints[nearestIndex]=Common::auxClampPannerPoint(QPointF((pt.x()-638)/159.0,(pt.y()-319)/159.0));
                publishPanner();
            }
        }
        else if(pt.y()>=100&&pt.y()<782&&pt.x()>=990&&pt.x()<1360) {
            auto* bus=selectedBus();
            if(!bus)return;
            const int activeCount=Common::auxLayoutChannelLabels(channel->layout).size();
            const int i=int((pt.y()-100)/86);
            const qreal x=990,y=100+i*86;
            if(i<0||i>=activeCount||i>=static_cast<int>(bus->sends.size())||pt.y()>y+80)return;
            if(QRectF(x+215,y+45,62,29).contains(pt))bus->sends[i].mute=!bus->sends[i].mute;
            else if(QRectF(x+286,y+45,62,29).contains(pt))bus->sends[i].solo=!bus->sends[i].solo;
            else if(QRectF(x+57,y+7,52,50).contains(pt)){
                draggedFader=i;
                sendStartY=pt.y();
                sendStartValue=bus->sends[i].levelDb;
            }
        }
        update();if(changed)changed();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (!channel || event->button()!=Qt::LeftButton) return;
        const QPointF pt=designPoint(event->position());
        if (QRectF(65,351,200,51).contains(pt)) {
            draggedInputGain=false;
            Controls::resetNumericValue({-12,12,0,1},[this](double value) {
                channel->inputGainDb=value;
                setProperty("selInputGainDb",value);
                update(); if(changed) changed();
            });
            return;
        }
        if(auto* bus=selectedBus()) {
            if(bus->panMode==Common::AuxPanMode::spatial) {
                for(int knob=0;knob<3;++knob) {
                    const qreal x=330+knob*206;
                    if(!QRectF(x+56,653,82,82).contains(pt)) continue;
                    draggedKnob=-1;
                    const auto spec=knob==0?Controls::NumericControlSpec{0,100,100,0}
                        :knob==1?Controls::NumericControlSpec{-100,100,0,0}
                        :Controls::NumericControlSpec{-180,180,0,0};
                    Controls::resetNumericValue(spec,[this,bus,knob](double value) {
                        setKnob(*bus,knob,knob==0?value/100
                            :knob==1?(value+100)/200:(value+180)/360);
                        update(); if(changed) changed();
                    });
                    return;
                }
            } else if (QRectF(463,144,350,350).contains(pt)) {
                draggedClassicPan=false;
                Controls::resetNumericValue({-1,1,0,2},[this,bus](double value) {
                    bus->classicPan=value; setProperty("selClassicPan",value);
                    update(); if(changed) changed();
                });
                return;
            }
            if(pt.x()>=1047&&pt.x()<1099&&pt.y()>=100&&pt.y()<788) {
                const int index=static_cast<int>((pt.y()-100)/86);
                const qreal y=100+index*86;
                if(index>=0&&index<static_cast<int>(bus->sends.size())
                    &&index<Common::auxLayoutChannelLabels(channel->layout).size()
                    &&QRectF(1047,y+7,52,50).contains(pt)) {
                    draggedFader=-1;
                    Controls::resetNumericValue({-72,12,0,1},[this,bus,index](double value) {
                        setSendLevel(*bus,index,value); update(); if(changed) changed();
                    });
                }
            }
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(!channel||!(event->buttons()&Qt::LeftButton))return;
        if(draggedInputGain){
            const qreal delta=inputGainStartY-designPoint(event->position()).y();
            channel->inputGainDb=std::clamp(qRound((inputGainStartValue+delta*.2)*10)/10.0,-12.0,12.0);
            setProperty("selInputGainDb",channel->inputGainDb);
            if(changed)changed();return;
        }
        auto* bus=selectedBus();if(!bus||draggedFader>=static_cast<int>(bus->sends.size()))return;
        if(draggedClassicPan){
            bus->classicPan=std::clamp(classicPanStartValue+(classicPanStartY-designPoint(event->position()).y())*.01,-1.0,1.0);
            setProperty("selClassicPan",bus->classicPan);if(changed)changed();return;
        }
        if(draggedKnob>=0){
            const qreal fraction=parameterStartValue+(parameterStartY-designPoint(event->position()).y())*.005;
            setKnob(*bus,draggedKnob,fraction);if(changed)changed();return;
        }
        if(draggedInfluencePoint>=0&&draggedInfluencePoint<static_cast<int>(bus->adjustablePoints.size())){
            const auto pt=designPoint(event->position());
            const auto& point=bus->adjustablePoints[draggedInfluencePoint];
            const auto distance=std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159);
            Common::auxSetPointInfluenceRadius(*bus,draggedInfluencePoint,
                Common::auxInfluenceRadiusForPixels(distance));
            publishPanner();if(changed)changed();return;
        }
        if(draggedPoint>=0&&draggedPoint<static_cast<int>(bus->adjustablePoints.size())){
            const QPointF pt=designPoint(event->position());
            bus->adjustablePoints[draggedPoint]=Common::auxClampPannerPoint(QPointF((pt.x()-638)/159.0,(pt.y()-319)/159.0));publishPanner();if(changed)changed();return;
        }
        if(draggedFader<0)return;
        const qreal y=designPoint(event->position()).y();
        setSendLevel(*bus,draggedFader,qRound((sendStartValue+(sendStartY-y)*.2)*10)/10.0);
        if(changed)changed();
    }
    void mouseReleaseEvent(QMouseEvent*) override
    {
        const bool wasDragging = draggingControl();
        draggedFader=-1;draggedPoint=-1;draggedInfluencePoint=-1;draggedKnob=-1;
        draggedInputGain=false;draggedClassicPan=false;
        if (wasDragging && dragFinished) dragFinished();
    }
    void wheelEvent(QWheelEvent* event) override
    {
        auto* bus=selectedBus();
        if(bus&&bus->panMode==Common::AuxPanMode::spatial){
            const QPointF pt=designPoint(event->position());
            for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                const auto& point=bus->adjustablePoints[i];
                const auto distance=std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159);
                const auto influence=bus->pointInfluenceRadii.size()>static_cast<std::size_t>(i)
                    ? bus->pointInfluenceRadii[i]:0.1;
                if(distance<=20||std::abs(distance-Common::auxInfluencePixelRadius(influence))<=7){
                    selectedPoint=i;
                    Common::auxSetPointInfluenceRadius(*bus,i,influence+event->angleDelta().y()/120.0*0.02);
                    publishPanner();update();if(changed)changed();event->accept();return;
                }
            }
        }
        QWidget::wheelEvent(event);
    }
private:
    QRect readoutRect(const QRectF& design) const {
        const qreal scale=contentScale();
        const qreal rightOffset=design.left()>=313?selDetailsLeft(size())-313*scale:0;
        return QRect(qRound(design.left()*scale+rightOffset),
                     qRound(design.top()*scale+contentOrigin().y()),
                     qRound(design.width()*scale),qRound(design.height()*scale));
    }
    qreal contentScale() const { return selContentScale(size()); }
    QPointF contentOrigin() const { const auto scale=contentScale();return {0,(height()-813*scale)/2}; }
    QPointF designPoint(const QPointF& point) const
    {
        const qreal scale=contentScale();
        const qreal right=selDetailsLeft(size());
        if(point.x()>301*scale&&point.x()<right)return {-1,-1};
        const qreal rightOffset=point.x()>=right?right-313*scale:0;
        return {(point.x()-rightOffset)/scale,(point.y()-contentOrigin().y())/scale};
    }
    void setSendLevel(Common::AuxBusState& bus,int index,qreal level)
    {
        level=std::clamp(level,-72.0,12.0);
        if(bus.sendsLinked){
            qreal lower=-84.0,upper=84.0;
            const auto count=Common::auxLayoutChannelLabels(channel->layout).size();
            for(int i=0;i<count;++i){const auto& send=bus.sends[i];lower=std::max(lower,-72.0-send.levelDb);upper=std::min(upper,12.0-send.levelDb);}
            const qreal delta=std::clamp(level-bus.sends[index].levelDb,lower,upper);
            for(int i=0;i<count;++i)bus.sends[i].levelDb+=delta;
        } else bus.sends[index].levelDb=level;
        publishLevels();
    }
    void publishLevels()
    {
        QVariantList levels;
        if(const auto* bus=selectedBus())
            for(const auto& send:bus->sends)levels.push_back(send.levelDb);
        setProperty("selSendLevels",levels);
    }
    void setKnob(Common::AuxBusState& bus,int index,qreal fraction)
    {
        fraction=std::clamp(fraction,0.0,1.0);
        if(index==0)bus.width=fraction;
        else if(index==1&&!bus.adjustablePoints.empty())
            bus.adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus.adjustablePoints.size())-1)].setY(fraction*2-1);
        else if(index==2)Common::auxSetPannerAngle(bus,fraction*360-180);
        publishPanner();
    }
    void publishPanner()
    {
        if(const auto* bus=selectedBus()){
            setProperty("selPolarCurvesVisible",bus->showPolarCurves);
            setProperty("selInputCurveVisible",bus->showInputCurve);
            setProperty("selOutputCurveVisible",bus->showOutputCurve);
            setProperty("selPannerWidth",bus->width);
            setProperty("selPannerAngle",bus->angleDegrees);
            const auto influence=bus->pointInfluenceRadii.size()>static_cast<std::size_t>(selectedPoint)
                ? bus->pointInfluenceRadii[selectedPoint]:0.1;
            setProperty("selPannerInfluence",influence);
            if(!bus->adjustablePoints.empty())
                setProperty("selPannerFrontBack",bus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus->adjustablePoints.size())-1)].y());
        }
    }
    Common::AuxBusState* selectedBus() const
    {
        return channel ? &channel->selState : nullptr;
    }
    Channel* channel {};
    Common::MixerPolarContour polarContour;
    QLineEdit* name {};
    QLineEdit* comment {};
    QPushButton* modeButton {};
    QLabel* modeArrow {};
    QPushButton* colorButton {};
    bool linked {true};
    int draggedFader {-1};
    int draggedPoint {-1};
    int draggedInfluencePoint {-1};
    int draggedKnob {-1};
    bool draggedInputGain {};
    qreal inputGainStartY {};
    qreal inputGainStartValue {};
    qreal sendStartY {};
    qreal sendStartValue {};
    qreal parameterStartY {};
    qreal parameterStartValue {};
    bool draggedClassicPan {};
    qreal classicPanStartY {};
    qreal classicPanStartValue {};
    int selectedPoint {};
    std::uint32_t displayedId {};
};

class SelContentHost final : public QWidget {
public:
    explicit SelContentHost(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(950,650); }

    void setPages(SelMainPage* main, QStackedWidget* details)
    {
        mainPage=main;
        detailPages=details;
        placePages();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        placePages();
    }

private:
    void placePages()
    {
        if(!mainPage||!detailPages)return;
        mainPage->setGeometry(rect());
        const qreal scale=selContentScale(size());
        const int rightRegionStart=qRound(313.0*scale);
        const int margin=qRound(8.0*scale);
        detailPages->setGeometry(rightRegionStart+margin,0,
                                 width()-rightRegionStart-2*margin,height());
    }

    SelMainPage* mainPage {};
    QStackedWidget* detailPages {};
};

QColor withAlpha(const QColor& color, int alpha)
{
    auto result = color;
    result.setAlpha(alpha);
    return result;
}

void drawText(QPainter& painter, const QRectF& rect, const QString& text, const QColor& color,
              qreal size, int alignment = Qt::AlignCenter, bool bold = false)
{
    auto font = painter.font();
    font.setPixelSize(std::max(8, qRound(size)));
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    Controls::drawText(painter,rect,text,font,color,Qt::Alignment(alignment));
}

void drawAdaptiveText(QPainter& painter, const QRectF& rect, const QString& text,
                      const QColor& color, qreal preferredSize, qreal minimumSize,
                      int alignment = Qt::AlignCenter, bool bold = false)
{
    auto font = painter.font();
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    auto size = preferredSize;
    const auto availableWidth = std::max(1.0, rect.width() - 8.0);
    const auto availableHeight = std::max(1.0, rect.height() - 4.0);
    while (size > minimumSize) {
        font.setPixelSize(std::max(1, qRound(size)));
        const QFontMetricsF metrics(font);
        if (metrics.horizontalAdvance(text) <= availableWidth && metrics.height() <= availableHeight)
            break;
        size -= 1.0;
    }
    font.setPixelSize(std::max(1, qRound(std::max(size, minimumSize))));
    Controls::drawText(painter,rect,text,font,color,Qt::Alignment(alignment));
}

QString faderDbText(qreal position)
{
    if (position >= .995)
        return QStringLiteral("−∞");
    const auto db = 12.0 - std::clamp(position / .95, 0.0, 1.0) * 84.0;
    const auto prefix = db > 0.0 ? QStringLiteral("+") : QString();
    return prefix + QString::number(db, 'f', 1) + QStringLiteral(" dB");
}

qreal faderDbValue(qreal position)
{
    if (position >= .995)
        return -72.0;
    return 12.0 - std::clamp(position / .95, 0.0, 1.0) * 84.0;
}

qreal faderPositionForDb(qreal db)
{
    return std::clamp((12.0-db)/84.0*.95,0.0,.95);
}

qreal meterDbValue(float peak)
{
    return peak <= 0.0F ? -72.0 : std::clamp(20.0 * std::log10(peak), -72.0, 12.0);
}

}

class MixerWindow::Impl {
public:
    explicit Impl(MixerWindow& owner, Common::DigitalMixerState* model,
                  MixerWindow::TopologyCommit commit, MixerWindow::MeterRead meterRead,
                  MixerWindow::PolarRead polarRead, MixerWindow::ChannelMeterRead channelMeterRead,
                  MixerWindow::EffectAnalysisRead effectAnalysisRead)
        : owner(owner)
        , model(model)
        , commit(std::move(commit))
        , meterRead(std::move(meterRead))
        , polarRead(std::move(polarRead))
        , channelMeterRead(std::move(channelMeterRead))
        , effectAnalysisRead(std::move(effectAnalysisRead))
    {
        channels = {
            { "Mic 1", "Analog In 1", "1", "#45bfff", Common::mixerUnityFaderPosition, -5.0, -5.0 },
            { "Mic 2", "Analog In 2", "2", "#45bfff", Common::mixerUnityFaderPosition, -9.0, -9.0 },
            { "Line 3", "Line In 3", "3", "#8da5d8", Common::mixerUnityFaderPosition, -14.0, -14.0 },
            { "Line 4", "Line In 4", "4", "#8da5d8", Common::mixerUnityFaderPosition, -8.0, -8.0 },
            { "USB 5", "USB In 5", "5", "#70c9f4", Common::mixerUnityFaderPosition, -11.0, -11.0 },
            { "USB 6", "USB In 6", "6", "#70c9f4", Common::mixerUnityFaderPosition, -18.0, -18.0 },
            { "Bus 7", "Bus In 7", "7", "#8da5d8", Common::mixerUnityFaderPosition, -7.0, -7.0 },
            { "Bus 8", "Bus In 8", "8", "#8da5d8", Common::mixerUnityFaderPosition, -23.0, -23.0 }
        };
        for (auto& channel : channels)
        {
            channel.id = nextChannelId++;
            channel.layout = Common::AuxChannelLayout::stereo;
        }
        rebuildAuxChannels();
        if (model) initializeFromModel();
        bindRefreshTimer(timer, &owner);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] {
            const auto& settings=ApplicationSettings::instance();
            const auto elapsedMilliseconds=meterFrameClock.isValid()
                ? meterFrameClock.restart() : -1;
            if(elapsedMilliseconds<0) meterFrameClock.start();
            const double frameSeconds=elapsedMilliseconds>0
                ? elapsedMilliseconds/1000.0 : 1.0/settings.frameRate();
            const double peakHold=settings.peakHoldSeconds();
            const bool sampleMeterReadouts=!meterReadoutClock.isValid()
                || meterReadoutClock.elapsed()>=settings.peakValueRefreshMilliseconds();
            const bool sipActive=std::any_of(channels.cbegin(),channels.cend(),[](const Channel& item) {
                return item.number!=QStringLiteral("MAIN") && item.solo;
            });
            for (auto& channel : channels) {
                channel.sipMuted=sipActive && channel.number!=QStringLiteral("MAIN") && !channel.solo;
                qreal inputLevel = -72.0;
                qreal outputLevel = -72.0;
                if (this->model) {
                    channel.realMeter = true;
                    inputLevel = this->meterRead && channel.modelId
                        ? meterDbValue(this->meterRead(channel.modelId, false)) : -72.0;
                    outputLevel = this->meterRead && channel.modelId
                        ? meterDbValue(this->meterRead(channel.modelId, true)) : -72.0;
                    channel.level = this->meterRead && channel.modelId
                        ? (channel.postFaderMeter ? outputLevel : inputLevel) : -72.0;
                    for (std::size_t index = 0; index < channel.channelLevels.size(); ++index)
                        channel.channelLevels[index] = this->channelMeterRead && channel.modelId
                            ? meterDbValue(this->channelMeterRead(channel.modelId,
                                channel.postFaderMeter, index)) : channel.level;
                    for (std::size_t index = 0; index < channel.channelInputLevels.size(); ++index)
                        channel.channelInputLevels[index] = this->channelMeterRead && channel.modelId
                            ? meterDbValue(this->channelMeterRead(channel.modelId, false, index)) : inputLevel;
                } else {
                    if (QRandomGenerator::global()->bounded(12) == 0)
                        channel.target = QRandomGenerator::global()->bounded(-480, 111) / 10.0;
                    channel.level += (channel.target - channel.level) * .17;
                    channel.level = std::clamp(channel.level + (QRandomGenerator::global()->bounded(7) - 3) / 10.0, -72.0, 12.0);
                    inputLevel = channel.level + channel.inputGainDb;
                    outputLevel = inputLevel + faderDbValue(channel.fader);
                }
                channel.meterBallistics.advance(channel.level,frameSeconds,peakHold);
                channel.inputMeterBallistics.advance(inputLevel,frameSeconds,peakHold);
                channel.outputMeterBallistics.advance(outputLevel,frameSeconds,peakHold);
                if(sampleMeterReadouts) {
                    channel.inputMeterReadoutDb=channel.inputMeterBallistics.levelDb();
                    channel.outputMeterReadoutDb=channel.outputMeterBallistics.levelDb();
                }
                for(std::size_t index=0;index<channel.channelBallistics.size();++index) {
                    channel.channelBallistics[index].advance(channel.realMeter
                        ? channel.channelLevels[index]:channel.level,frameSeconds,peakHold);
                    channel.channelLevels[index]=channel.channelBallistics[index].levelDb();
                    channel.channelPeaks[index]=channel.channelBallistics[index].peakDb();
                    channel.channelInputBallistics[index].advance(channel.realMeter
                        ? channel.channelInputLevels[index] : inputLevel,frameSeconds,peakHold);
                    channel.channelInputLevels[index]=channel.channelInputBallistics[index].levelDb();
                    channel.channelInputPeaks[index]=channel.channelInputBallistics[index].peakDb();
                }
            }
            qreal monitorLevel = this->model && this->meterRead
                ? meterDbValue(this->meterRead(0, true)) : -72.0;
            if (!this->model || !this->meterRead)
                for (const auto& channel : channels) {
                    if (channel.listenMode == Common::MixerListenMode::off) continue;
                    const auto level = channel.listenMode == Common::MixerListenMode::pfl
                        ? channel.inputMeterBallistics.levelDb() : channel.meterBallistics.levelDb();
                    monitorLevel = std::max(monitorLevel, level);
                }
            monitorMeter.advance(monitorLevel, frameSeconds, peakHold);
            if (monitorSel) {
                std::array<qreal, 8> monitorLevels;
                monitorLevels.fill(-72.0);
                const auto count = static_cast<std::size_t>(static_cast<int>(monitorLayout));
                for (std::size_t index = 0; index < count; ++index)
                    monitorLevels[index] = this->channelMeterRead
                        ? meterDbValue(this->channelMeterRead(0, true, index)) : monitorLevel;
                monitorSel->advanceMeters(monitorLevels, frameSeconds, peakHold);
                if (this->polarRead) {
                    Common::MixerPolarSnapshot snapshot;
                    if (this->polarRead(0, 0, snapshot)) monitorPolarCurve.accept(snapshot);
                    monitorSel->setContour(monitorPolarCurve.contour());
                }
                monitorSel->update();
            }
            if(sampleMeterReadouts) meterReadoutClock.start();
            this->owner.update();
            if (selMain && selMain->isVisible()) {
                if(sampleMeterReadouts) {
                    const auto selected=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item) {
                        return item.id==selectedChannelId;
                    });
                    if(selected!=channels.cend()) {
                        selMain->setProperty("selInputMeterReadoutDb",selected->inputMeterReadoutDb);
                        selMain->setProperty("selOutputMeterReadoutDb",selected->outputMeterReadoutDb);
                    }
                }
                selMain->update();
            }
            if (selMain && this->model && this->polarRead) {
                const auto selected = std::find_if(channels.cbegin(), channels.cend(), [this](const Channel& item) {
                    return item.id == selectedChannelId;
                });
                if (selected != channels.cend() && selected->modelId
                    && selected->modelId != this->model->main().id) {
                    if (polarSourceId != selected->modelId) {
                        polarSourceId = selected->modelId;
                        selPolarCurve.clear();
                        polarStaleTicks = 0;
                    }
                    Common::MixerPolarSnapshot snapshot;
                    if (this->polarRead(selected->modelId, this->model->main().id, snapshot)
                        && selPolarCurve.accept(snapshot)) polarStaleTicks = 0;
                    else if (++polarStaleTicks > 10) selPolarCurve.clear();
                    selMain->setPolarContour(selPolarCurve.contour());
                } else {
                    selPolarCurve.clear();
                    selMain->setPolarContour({});
                }
            }
        });
        timer.start();
    }


    int auxChannelCount() const
    {
        return static_cast<int>(auxState->buses().size());
    }

    int subMixCount() const { return static_cast<int>(std::count_if(channels.begin(),channels.end(),[](const Channel& channel) {return channel.subMix;})); }
    int subMixStart() const { return inputChannelCount + auxChannelCount(); }

    int masterIndex() const
    {
        return static_cast<int>(channels.size()) - 1;
    }

    static Common::AuxChannelLayout asAuxLayout(Common::MixerLayout layout)
    {
        switch (layout) {
        case Common::MixerLayout::mono: return Common::AuxChannelLayout::mono;
        case Common::MixerLayout::stereo: return Common::AuxChannelLayout::stereo;
        case Common::MixerLayout::surround51: return Common::AuxChannelLayout::surround51;
        case Common::MixerLayout::surround71: return Common::AuxChannelLayout::surround71;
        }
        return Common::AuxChannelLayout::stereo;
    }

    static Common::MixerLayout asMixerLayout(Common::AuxChannelLayout layout)
    {
        switch (layout) {
        case Common::AuxChannelLayout::mono: return Common::MixerLayout::mono;
        case Common::AuxChannelLayout::stereo: return Common::MixerLayout::stereo;
        case Common::AuxChannelLayout::surround51: return Common::MixerLayout::surround51;
        case Common::AuxChannelLayout::surround71: return Common::MixerLayout::surround71;
        }
        return Common::MixerLayout::stereo;
    }

    static void loadMainPanner(Channel& channel, const Common::MixerStripState& strip)
    {
        const auto& source = strip.mainPanner;
        auto& target = channel.selState;
        target.panMode = source.mode == Common::MixerPanMode::classic
            ? Common::AuxPanMode::classic : Common::AuxPanMode::spatial;
        target.classicPan = source.classicPan;
        target.width = source.width;
        target.angleDegrees = source.angleDegrees;
        target.sendsLinked = source.linked;
        target.showPolarCurves = source.showPolarCurves;
        target.showInputCurve = source.showInputCurve;
        target.showOutputCurve = source.showOutputCurve;
        target.sourceLayout = channel.layout;
        target.targetLayout = channel.subMix ? channel.layout : Common::AuxChannelLayout::stereo;
        target.adjustablePoints.clear();
        target.pointInfluenceRadii.clear();
        target.sends.resize(8);
        const auto labels = WinJACKNexus::Common::standardChannelLabels(8);
        for (std::size_t i = 0; i < source.points.size(); ++i) {
            const auto& point = source.points[i];
            target.adjustablePoints.emplace_back(point.x, point.y);
            target.pointInfluenceRadii.push_back(point.influenceRadius);
            target.sends[i].levelDb = point.levelDb;
            target.sends[i].mute = point.mute;
            target.sends[i].solo = point.solo;
        }
        for (std::size_t i = 0; i < target.sends.size(); ++i)
            target.sends[i].label = target.sends[i].name = labels[i];
    }

    bool commitTopology(const Common::DigitalMixerState& candidate)
    {
        if (!model) return true;
        QString error;
        if (commit && commit(candidate, &error)) return true;
        WJN_LOG_ERROR("Mixer topology commit failed: %s", error.toUtf8().constData());
        QMessageBox::warning(&owner, QStringLiteral("拓扑更新失败"),
            error.isEmpty() ? QStringLiteral("无法注册调音台端口，原有状态已保留。") : error);
        return false;
    }

    bool moveChannel(int from, int to)
    {
        if (from == to || from < 0 || to < 0 || from >= masterIndex() || to >= masterIndex()
            || (from < inputChannelCount) != (to < inputChannelCount)
            || channels[from].subMix != channels[to].subMix) return false;
        if (model) {
            auto candidate = *model;
            const auto index = to < inputChannelCount ? to : channels[to].subMix ? to-subMixStart() : to-inputChannelCount;
            if (!candidate.move(channels[from].modelId, static_cast<std::size_t>(index))
                || !commitTopology(candidate)) return false;
        }
        const auto first = channels.begin() + std::min(from, to);
        const auto last = channels.begin() + std::max(from, to) + 1;
        if (from < to) std::rotate(first, first + 1, last);
        else std::rotate(first, last - 1, last);
        if (from >= inputChannelCount && !channels[to].subMix) {
            auto& buses = auxState->buses();
            const auto busFirst = buses.begin() + std::min(from, to) - inputChannelCount;
            const auto busLast = buses.begin() + std::max(from, to) - inputChannelCount + 1;
            if (from < to) std::rotate(busFirst, busFirst + 1, busLast);
            else std::rotate(busFirst, busLast - 1, busLast);
        }
        owner.update();
        return true;
    }

    bool commitChannel(Channel& channel)
    {
        if (!model || !channel.modelId) return true;
        Common::DigitalMixerState candidate = *model;
        auto* strip = candidate.strip(channel.modelId);
        if (!strip) return false;
        strip->name = channel.name;
        strip->comment = channel.source;
        strip->color = channel.accent;
        strip->inputGainDb = channel.inputGainDb;
        strip->faderPosition = channel.fader;
        strip->mute = channel.mute;
        strip->solo = channel.solo;
        strip->listenMode = channel.listenMode;
        strip->mixToMain = channel.mixToMain;
        auto& panner = strip->mainPanner;
        const auto& view = channel.selState;
        if (view.adjustablePoints.size() == panner.points.size()) {
            panner.mode = view.panMode == Common::AuxPanMode::classic
                ? Common::MixerPanMode::classic : Common::MixerPanMode::spatial;
            panner.classicPan = view.classicPan;
            panner.width = view.width;
            panner.angleDegrees = view.angleDegrees;
            panner.linked = view.sendsLinked;
            panner.showPolarCurves = view.showPolarCurves;
            panner.showInputCurve = view.showInputCurve;
            panner.showOutputCurve = view.showOutputCurve;
            for (std::size_t i = 0; i < panner.points.size(); ++i) {
                panner.points[i].x = view.adjustablePoints[i].x();
                panner.points[i].y = view.adjustablePoints[i].y();
                if (i < view.pointInfluenceRadii.size())
                    panner.points[i].influenceRadius = view.pointInfluenceRadii[i];
                if (i < view.sends.size()) {
                    panner.points[i].levelDb = view.sends[i].levelDb;
                    panner.points[i].mute = view.sends[i].mute;
                    panner.points[i].solo = view.sends[i].solo;
                }
            }
        }
        if (commitTopology(candidate)) {
            WJN_LOG_INFO("Mixer channel updated id=%llu name=%s gainDb=%.2f fader=%.4f mute=%d solo=%d mixToMain=%d",
                static_cast<unsigned long long>(channel.modelId), channel.name.toUtf8().constData(),
                channel.inputGainDb, channel.fader, channel.mute, channel.solo, channel.mixToMain);
            return true;
        }
        const auto* old = model->strip(channel.modelId);
        if (!old) return false;
        channel.name = old->name;
        channel.source = old->comment;
        channel.accent = old->color;
        channel.inputGainDb = old->inputGainDb;
        channel.fader = old->faderPosition;
        channel.mute = old->mute;
        channel.solo = old->solo;
        channel.mixToMain = old->mixToMain;
        loadMainPanner(channel, *old);
        return false;
    }

    bool commitAuxSettings(bool chainEnabled, bool subMode = false)
    {
        if (!model) return true;
        auto candidate = *model;
        auto& routingState = subMode ? subState : auxState;
        std::map<std::uint32_t, std::uint64_t> busIds;
        for (const auto& channel : channels)
            if (channel.modelId && (subMode ? channel.subMix : channel.auxId != 0))
                busIds[subMode ? channel.id : channel.auxId] = channel.modelId;
        for (const auto& bus : routingState->buses()) {
            auto id = busIds.find(bus.id);
            if (id == busIds.end()) {
                const auto source = busIds.find(bus.cloneSourceId);
                const auto created = source != busIds.end()
                    ? candidate.clone(source->second)
                    : candidate.add(subMode ? Common::MixerStripKind::subMix : Common::MixerStripKind::aux, asMixerLayout(bus.targetLayout));
                if (!created) return false;
                busIds[bus.id] = created;
            } else if (const auto* strip = candidate.strip(id->second);
                       strip && strip->layout != asMixerLayout(bus.targetLayout)) {
                if (!candidate.setLayout(id->second, asMixerLayout(bus.targetLayout))) return false;
            }
        }
        for (const auto& [busId, modelId] : busIds) {
            if (routingState->bus(busId)) continue;
            if (!candidate.remove(modelId)) return false;
        }
        const auto source = std::find_if(channels.cbegin(), channels.cend(), [this,subMode](const Channel& channel) {
            return channel.id == (subMode ? subWindowChannelId : auxWindowChannelId);
        });
        if (source != channels.cend() && candidate.strip(source->modelId)) {
            auto* sourceStrip = candidate.strip(source->modelId);
            if (!subMode) sourceStrip->auxSendsEnabled = chainEnabled;
            const auto sourceKey = subMode ? QStringLiteral("sub:%1").arg(source->id) : source->auxId ? QStringLiteral("aux:%1").arg(source->auxId)
                                                 : QStringLiteral("input:%1").arg(source->id);
            const auto& sourceBuses = routingState->busesForSource(sourceKey, subMode && source->subMix ? source->id : subMode ? 0 : source->auxId);
            for (const auto& bus : sourceBuses) {
                const auto target = busIds.find(bus.id);
                if (target == busIds.end() || target->second == source->modelId) continue;
                const auto existing = std::find_if(sourceStrip->sends.cbegin(), sourceStrip->sends.cend(),
                    [target](const Common::MixerAuxSend& send) { return send.targetId == target->second; });
                if (existing == sourceStrip->sends.cend() && !(chainEnabled && bus.enabled)) continue;
                Common::MixerAuxSend send;
                send.targetId = target->second;
                send.enabled = chainEnabled && bus.enabled;
                send.preFader = !subMode && bus.preFader;
                send.panner = Common::defaultMixerPanner(asMixerLayout(bus.sourceLayout),
                    asMixerLayout(bus.targetLayout));
                send.panner.mode = bus.panMode == Common::AuxPanMode::classic
                    ? Common::MixerPanMode::classic : Common::MixerPanMode::spatial;
                send.panner.classicPan = bus.classicPan;
                send.panner.width = bus.width;
                send.panner.angleDegrees = bus.angleDegrees;
                send.panner.linked = bus.sendsLinked;
                send.panner.showPolarCurves = bus.showPolarCurves;
                send.panner.showInputCurve = bus.showInputCurve;
                send.panner.showOutputCurve = bus.showOutputCurve;
                for (std::size_t i = 0; i < send.panner.points.size() && i < bus.adjustablePoints.size(); ++i) {
                    auto& point = send.panner.points[i];
                    point.x = bus.adjustablePoints[i].x();
                    point.y = bus.adjustablePoints[i].y();
                    if (i < bus.pointInfluenceRadii.size()) point.influenceRadius = bus.pointInfluenceRadii[i];
                    if (i < bus.sends.size()) {
                        point.levelDb = bus.sends[i].levelDb;
                        point.mute = bus.sends[i].mute;
                        point.solo = bus.sends[i].solo;
                    }
                }
                QString error;
                if (!candidate.setSend(source->modelId, send, &error)) {
                    QMessageBox::warning(&owner, QStringLiteral("AUX 路由无效"), error);
                    return false;
                }
                sourceStrip = candidate.strip(source->modelId);
            }
        }
        if (!commitTopology(candidate)) return false;
        if (subMode) return true;
        rebuildAuxChannels();
        for (auto& channel : channels) {
            if (!channel.auxId) continue;
            if (const auto found = busIds.find(channel.auxId); found != busIds.end()) {
                channel.modelId = found->second;
                if (const auto* strip = model->strip(channel.modelId))
                    channel.number = QStringLiteral("AUX %1").arg(strip->number);
            }
        }
        return true;
    }

    void initializeFromModel()
    {
        channels.clear();
        inputChannelCount = 0;
        auxState->clearBuses();
        for (const auto& strip : model->inputs()) {
            Channel channel {strip.name, strip.comment, QStringLiteral("CH %1").arg(strip.number),
                             strip.color, strip.faderPosition, -72.0, -72.0};
            channel.id = nextChannelId++;
            channel.modelId = strip.id;
            channel.realMeter = true;
            channel.layout = asAuxLayout(strip.layout);
            channel.inputGainDb = strip.inputGainDb;
            channel.mute = strip.mute;
            channel.solo = strip.solo;
            channel.listenMode = strip.listenMode;
            channel.mixToMain = strip.mixToMain;
            channel.fx = strip.fxChainEnabled;
            loadMainPanner(channel, strip);
            channels.push_back(std::move(channel));
            ++inputChannelCount;
        }
        for (const auto& strip : model->auxiliaries()) auxState->addBus(asAuxLayout(strip.layout));
        rebuildAuxChannels();
        for (std::size_t index = 0; index < model->auxiliaries().size(); ++index) {
            auto& channel = channels[static_cast<std::size_t>(inputChannelCount) + index];
            const auto& strip = model->auxiliaries()[index];
            channel.modelId = strip.id;
            channel.realMeter = true;
            channel.name = strip.name;
            channel.source = strip.comment;
            channel.number = QStringLiteral("AUX %1").arg(strip.number);
            channel.fader = strip.faderPosition;
            channel.inputGainDb = strip.inputGainDb;
            channel.mute = strip.mute;
            channel.solo = strip.solo;
            channel.listenMode = strip.listenMode;
            channel.mixToMain = strip.mixToMain;
            channel.fx = strip.fxChainEnabled;
            loadMainPanner(channel, strip);
        }
        for (const auto& strip : model->subMixes()) insertSubMix(strip);
        auto& master = channels[masterIndex()];
        master.modelId = model->main().id;
        master.realMeter = true;
        master.name = model->main().name;
        master.source = model->main().comment;
        master.fader = model->main().faderPosition;
        master.fx = model->main().fxChainEnabled;
        monitorFader = model->monitorFaderPosition();
        monitorLayout = model->monitorLayout();
        loadMainPanner(master, model->main());
        selectedChannelId = channels.front().id;
        owner.update();
    }

    qreal mainInputLevelDb() const
    {
        if (model) return meterRead ? meterDbValue(meterRead(model->main().id, false)) : -72.0;
        const bool anySolo = std::any_of(channels.cbegin(), channels.cbegin() + masterIndex(),
                                         [](const Channel& channel) { return channel.solo; });
        double power = 0.0;
        for (int index = 0; index < masterIndex(); ++index) {
            const auto& channel = channels[index];
            if (!channel.mixToMain || channel.mute || (anySolo && !channel.solo)) continue;
            const auto db = channel.level + channel.inputGainDb + faderDbValue(channel.fader);
            power += std::pow(10.0, db / 10.0);
        }
        return power <= 0.0 ? -72.0 : std::clamp(10.0 * std::log10(power), -72.0, 12.0);
    }

    qreal mainOutputLevelDb() const
    {
        if (model) return meterRead ? meterDbValue(meterRead(model->main().id, true)) : -72.0;
        const auto input = mainInputLevelDb();
        if (input <= -72.0 || channels[masterIndex()].mute) return -72.0;
        return std::clamp(input + faderDbValue(channels[masterIndex()].fader), -72.0, 12.0);
    }

    void rebuildAuxChannels()
    {
        std::vector<Channel> rebuilt;
        rebuilt.reserve(inputChannelCount + auxChannelCount() + 1);
        for (int index = 0; index < std::min(inputChannelCount, static_cast<int>(channels.size())); ++index)
            rebuilt.push_back(channels[index]);

        for (const auto& bus : auxState->buses()) {
            const auto existing = std::find_if(channels.cbegin(), channels.cend(), [&bus](const auto& channel) {
                return channel.auxId == bus.id;
            });
            Channel channel;
            if (existing != channels.cend())
                channel = *existing;
            else {
                channel.fader = Common::mixerUnityFaderPosition;
                channel.level = -12.0;
                channel.target = -12.0;
                channel.accent = QColor("#33b6ff");
            }
            if (existing == channels.cend())
                channel.name = QStringLiteral("AUX %1").arg(static_cast<int>(rebuilt.size()) - inputChannelCount + 1);
            if (existing == channels.cend())
                channel.source = bus.name;
            channel.number = channel.name;
            if (model && channel.modelId) {
                if (const auto* strip = model->strip(channel.modelId))
                    channel.number = QStringLiteral("AUX %1").arg(strip->number);
            }
            channel.auxId = bus.id;
            channel.id = 1000000 + bus.id;
            channel.layout = bus.targetLayout;
            rebuilt.push_back(std::move(channel));
        }

        for (const auto& channel : channels) if (channel.subMix) rebuilt.push_back(channel);

        const auto existingMaster = std::find_if(channels.cbegin(), channels.cend(), [](const auto& channel) {
            return channel.number == QStringLiteral("MAIN");
        });
        if (existingMaster != channels.cend())
            rebuilt.push_back(*existingMaster);
        else {
            Channel master;
            master.name = QStringLiteral("Main L/R");
            master.source = QStringLiteral("JACK Out 1/2");
            master.number = QStringLiteral("MAIN");
            master.accent = QColor(QStringLiteral("#50dfbd"));
            master.fader = Common::mixerUnityFaderPosition;
            master.level = master.target = 4.0;
            master.id = 2000000;
            master.layout = Common::AuxChannelLayout::stereo;
            rebuilt.push_back(std::move(master));
        }
        channels = std::move(rebuilt);
        renumberInputs();
        if (auxSettingsWindow && auxWindowChannelId != 0
            && std::none_of(channels.cbegin(), channels.cend(), [this](const Channel& item) {
                   return item.id == auxWindowChannelId;
               })) {
            auxSettingsWindow->close();
            auxWindowChannelId = 0;
        }
        for (auto it = fxSettingsWindows.begin(); it != fxSettingsWindows.end();) {
            const auto present = std::find_if(channels.cbegin(), channels.cend(), [id = it->first](const Channel& item) {
                return item.id == id;
            });
            if (present == channels.cend()) {
                if (it->second) { it->second->close(); it->second->deleteLater(); }
                it = fxSettingsWindows.erase(it);
            } else {
                if (it->second) it->second->setSourceChannels(fxChannels(present->layout));
                ++it;
            }
        }
        updateSel();
        auxScrollOffset = effectiveAuxScrollOffset();
    }

    void renumberInputs()
    {
        for (int index = 0; index < inputChannelCount; ++index) {
            auto number = index + 1;
            if (model && channels[index].modelId) {
                if (const auto* strip = model->strip(channels[index].modelId))
                    number = static_cast<int>(strip->number);
            }
            channels[index].number = QStringLiteral("CH %1").arg(number);
        }
    }

    FxSettingsWindow* fxSettingsFor(int index)
    {
        const auto id = channels[index].id;
        auto& window = fxSettingsWindows[id];
        if (window.isNull()) {
            window = new FxSettingsWindow(channels[index].name,
                fxChannels(channels[index].layout), &owner);
            if (model && channels[index].modelId) {
                const auto modelId = channels[index].modelId;
                if (const auto* strip = model->strip(modelId))
                    window->loadChain(strip->effects, strip->fxChainEnabled);
                window->setChainCommitHandler([this, id, modelId](const auto& effects, bool enabled) {
                    auto candidate = *model;
                    auto* strip = candidate.strip(modelId);
                    if (!strip) return false;
                    strip->effects = effects;
                    strip->fxChainEnabled = enabled;
                    if (!commitTopology(candidate)) {
                        QTimer::singleShot(0, &owner, [this, id, modelId] {
                            const auto found = fxSettingsWindows.find(id);
                            if (found == fxSettingsWindows.end() || !found->second || !model) return;
                            if (const auto* saved = model->strip(modelId))
                                found->second->loadChain(saved->effects, saved->fxChainEnabled);
                        });
                        return false;
                    }
                    const auto found = std::find_if(channels.begin(), channels.end(), [id](const Channel& item) {
                        return item.id == id;
                    });
                    if (found != channels.end()) found->fx = enabled;
                    WJN_LOG_INFO("Mixer FX chain updated id=%llu enabled=%d effects=%llu",
                        static_cast<unsigned long long>(modelId), enabled,
                        static_cast<unsigned long long>(effects.size()));
                    owner.update();
                    return true;
                });
                if (meterRead) window->setMeterReadHandler([this, modelId](bool post) {
                    return meterDbValue(meterRead(modelId, post));
                });
                if (channelMeterRead) window->setChannelMeterReadHandler(
                    [this, modelId](bool post, std::size_t channel) {
                        return meterDbValue(channelMeterRead(modelId, post, channel));
                    });
                if (effectAnalysisRead) window->setLiveAnalysisReadHandler(
                    [this, modelId](std::size_t index, float input[8], float output[8],
                                    float reduction[8], void* spectrum, std::uint32_t bytes) {
                        return effectAnalysisRead(modelId, index, input, output,
                                                  reduction, spectrum, bytes);
                    });
                if (index == masterIndex() && model->main().effects.empty())
                    window->addDefaultMasterEffects();
            } else {
                if (index == masterIndex()) window->addDefaultMasterEffects();
                window->setChainEnabledHandler([this, id](bool enabled) {
                const auto found = std::find_if(channels.begin(), channels.end(), [id](const Channel& item) {
                    return item.id == id;
                });
                if (found != channels.end()) { found->fx = enabled; owner.update(); }
                });
            }
        }
        const bool chainEnabled = model && channels[index].modelId
            ? model->strip(channels[index].modelId)->fxChainEnabled
            : channels[index].fx;
        channels[index].fx = chainEnabled;
        window->setChainEnabled(chainEnabled);
        return window;
    }

    static QStringList fxChannels(Common::AuxChannelLayout layout)
    {
        if (layout == Common::AuxChannelLayout::mono) return { QStringLiteral("M") };
        auto labels = Common::auxLayoutChannelLabels(layout);
        return labels;
    }

    void addInput(Common::AuxChannelLayout layout)
    {
        std::uint64_t modelId {};
        if (model) {
            auto candidate = *model;
            modelId = candidate.add(Common::MixerStripKind::input, asMixerLayout(layout));
            if (!modelId || !commitTopology(candidate)) return;
        }
        Channel channel { QStringLiteral("Input %1").arg(nextChannelId),
                          QStringLiteral("Input %1").arg(nextChannelId),
                          QString::number(nextChannelId), QColor("#45bfff"),
                          Common::mixerUnityFaderPosition, -12.0, -12.0 };
        channel.id = nextChannelId++;
        channel.modelId = modelId;
        channel.layout = layout;
        channels.insert(channels.begin() + inputChannelCount, channel);
        ++inputChannelCount;
        renumberInputs();
        updateSel();
        owner.update();
    }

    void insertSubMix(const Common::MixerStripState& strip)
    {
        Channel channel {strip.name, strip.comment, QStringLiteral("SUB %1").arg(strip.number),
            strip.color, strip.faderPosition, -72.0, -72.0};
        channel.id = nextChannelId++;
        channel.modelId = strip.id;
        channel.realMeter = model != nullptr;
        channel.subMix = true;
        channel.layout = asAuxLayout(strip.layout);
        channel.inputGainDb = strip.inputGainDb;
        channel.mute = strip.mute;
        channel.solo = strip.solo;
        channel.listenMode = strip.listenMode;
        channel.mixToMain = strip.mixToMain;
        channel.fx = strip.fxChainEnabled;
        loadMainPanner(channel, strip);
        channels.insert(channels.end()-1,std::move(channel));
    }

    void addSubMix(Common::AuxChannelLayout layout)
    {
        if (model) {
            auto candidate = *model;
            const auto id = candidate.add(Common::MixerStripKind::subMix, asMixerLayout(layout));
            if (!id || !commitTopology(candidate)) return;
            insertSubMix(*model->strip(id));
        } else {
            Common::MixerStripState strip;
            strip.id = nextChannelId;
            strip.number = subMixCount()+1;
            strip.name = QStringLiteral("SUB%1").arg(strip.number);
            strip.layout = asMixerLayout(layout);
            strip.color = QColor("#37d879");
            strip.mainPanner = Common::defaultMixerPanner(strip.layout,strip.layout);
            insertSubMix(strip);
        }
        updateSel(); owner.update();
    }

    void addAux(Common::AuxChannelLayout layout)
    {
        std::uint64_t modelId {};
        if (model) {
            auto candidate = *model;
            modelId = candidate.add(Common::MixerStripKind::aux, asMixerLayout(layout));
            if (!modelId || !commitTopology(candidate)) return;
        }
        auto& bus = auxState->addBus(layout);
        const auto busId = bus.id;
        rebuildAuxChannels();
        for (auto& channel : channels) {
            if (channel.auxId != busId) continue;
            channel.modelId = modelId;
            if (modelId) channel.number = QStringLiteral("AUX %1").arg(model->strip(modelId)->number);
            break;
        }
        owner.update();
    }

    void removeChannel(int index)
    {
        if (index == masterIndex() || (!model && ((index < inputChannelCount && inputChannelCount <= 1)
            || (index >= inputChannelCount && auxChannelCount() <= 1)))) return;
        const auto channel = channels[index];
        if (model) {
            auto candidate = *model;
            if (!candidate.remove(channel.modelId) || !commitTopology(candidate)) return;
        }
        if (auto found = fxSettingsWindows.find(channel.id); found != fxSettingsWindows.end()) {
            if (found->second) { found->second->close(); found->second->deleteLater(); }
            fxSettingsWindows.erase(found);
        }
        if (auxWindowChannelId == channel.id && auxSettingsWindow) {
            auxSettingsWindow->close();
            auxWindowChannelId = 0;
        }
        if (channel.subMix) {
            channels.erase(channels.begin()+index);
        } else if (channel.auxId) {
            auxState->removeBus(channel.auxId, model != nullptr);
            rebuildAuxChannels();
        } else {
            channels.erase(channels.begin() + index);
            --inputChannelCount;
            renumberInputs();
        }
        if (selectedChannelId == channel.id)
            selectedChannelId = channels.empty() ? 0 : channels.front().id;
        updateSel();
        owner.update();
    }

    void copyChannel(int index)
    {
        if (index == masterIndex()) return;
        const auto sourceId = channels[index].id;
        std::uint64_t copiedModelId {};
        if (model) {
            auto candidate = *model;
            copiedModelId = candidate.clone(channels[index].modelId);
            if (!copiedModelId || !commitTopology(candidate)) return;
        }
        std::uint32_t copiedId {};
        if (channels[index].subMix) {
            if (model) {
                insertSubMix(*model->strip(copiedModelId));
                updateSel();owner.update();return;
            }
            auto copied=channels[index];copied.id=nextChannelId++;copied.modelId=copied.id;
            copied.name+=QStringLiteral(" 副本");channels.insert(channels.end()-1,copied);
            updateSel();owner.update();return;
        }
        if (channels[index].auxId) {
            auto* bus = auxState->cloneBus(channels[index].auxId);
            if (!bus) return;
            const auto newBusId = bus->id;
            rebuildAuxChannels();
            const auto copied = std::find_if(channels.begin(), channels.end(), [newBusId](const Channel& item) {
                return item.auxId == newBusId;
            });
            if (copied != channels.end()) {
                const auto id = copied->id;
                *copied = channels[index];
                copied->id = id;
                copied->auxId = newBusId;
                copied->modelId = copiedModelId;
                copied->name = QStringLiteral("AUX %1").arg(auxChannelCount());
                copied->number = model ? QStringLiteral("AUX %1").arg(model->strip(copiedModelId)->number) : copied->name;
                copiedId = copied->id;
            }
        } else {
            Channel copied = channels[index];
            copied.id = nextChannelId++;
            copied.modelId = copiedModelId;
            copied.name += QStringLiteral(" 副本");
            copied.number = QString::number(copied.id);
            copiedId = copied.id;
            channels.insert(channels.begin() + index + 1, copied);
            ++inputChannelCount;
            renumberInputs();
        }
        if (copiedId && !model) {
            const auto sourceFx = fxSettingsWindows.find(sourceId);
            if (sourceFx != fxSettingsWindows.end() && sourceFx->second) {
                const auto copied = std::find_if(channels.begin(), channels.end(), [copiedId](const Channel& item) {
                    return item.id == copiedId;
                });
                if (copied != channels.end()) {
                    auto* target = fxSettingsFor(static_cast<int>(std::distance(channels.begin(), copied)));
                    target->cloneChainFrom(*sourceFx->second);
                }
            }
        }
        updateSel();
        owner.update();
    }

    void setChannelLayout(int index, Common::AuxChannelLayout layout)
    {
        if (index == masterIndex() || channels[index].layout == layout)
            return;
        const auto id = channels[index].id;
        if (model) {
            auto candidate = *model;
            if (!candidate.setLayout(channels[index].modelId, asMixerLayout(layout))
                || !commitTopology(candidate)) return;
        }
        if (channels[index].auxId) {
            auxState->setBusLayout(channels[index].auxId, layout);
            rebuildAuxChannels();
        } else {
            channels[index].layout = layout;
        }
        if (model) if (const auto* strip = model->strip(channels[index].modelId)) loadMainPanner(channels[index], *strip);
        if (auto found = fxSettingsWindows.find(id); found != fxSettingsWindows.end() && found->second)
            found->second->setSourceChannels(fxChannels(layout));
        if (auxSettingsWindow) {
            if (auxWindowChannelId == id) {
                const auto found = std::find_if(channels.cbegin(), channels.cend(), [id](const Channel& item) {
                    return item.id == id;
                });
                if (found != channels.cend()) {
                    const auto key = found->auxId ? QStringLiteral("aux:%1").arg(found->auxId)
                                                  : QStringLiteral("input:%1").arg(found->id);
                    auxSettingsWindow->setSource(key, found->name, found->auxId, layout,auxSourceNumber(*found));
                }
            } else {
                auxSettingsWindow->update();
            }
        }
        updateSel();
        owner.update();
    }

    QString subMixViewSignature() const
    {
        QString result;
        for (const auto& channel : channels) if (channel.subMix || channel.id == selectedChannelId)
            result += QStringLiteral("%1:%2:%3:%4;").arg(channel.id).arg(channel.name)
                .arg(static_cast<int>(channel.layout)).arg(channel.accent.name());
        return result;
    }

    void updateSel()
    {
        for (const auto& channel : channels)
            if (channel.auxId)
                if (auto* bus = auxState->bus(channel.auxId)) bus->name = channel.name;
        if (!selLabel) return;
        const auto found = std::find_if(channels.cbegin(), channels.cend(), [this](const Channel& item) {
            return item.id == selectedChannelId;
        });
        if (subSettingsWindow && selStack && selStack->currentIndex()==3
            && subMixViewSignature()!=subViewSignature) showSelAux(true);
        selLabel->setText(found == channels.cend() ? QStringLiteral("未选择通道") : found->name);
        if(selHeader){
            const QColor background=found!=channels.cend()&&found->customColor?found->accent
                : Controls::themeTokenColor(QStringLiteral("sel-main"),
                    QStringLiteral("control-header-surface-default"),QColor("#123a65"));
            const qreal brightness=.299*background.red()+.587*background.green()+.114*background.blue();
            const QColor foreground=brightness>145
                ? Controls::themeTokenColor(QStringLiteral("sel-main"),
                    QStringLiteral("control-header-text-dark"),QColor("#102033"))
                : Controls::themeTokenColor(QStringLiteral("sel-main"),
                    QStringLiteral("control-header-text-light"),QColor("#f5fbff"));
            if(background!=lastHeaderColor || foreground!=lastHeaderTextColor){
                lastHeaderColor=background;
                lastHeaderTextColor=foreground;
                selHeader->setStyleSheet(QStringLiteral("#selHeader {background:%1;border-radius:6px;} #selHeader QLabel {color:%2;background:transparent;}").arg(background.name(),foreground.name()));
            }
        }
        if(selSubTab)selSubTab->setVisible(found!=channels.cend()&&found->number!=QStringLiteral("MAIN"));
        if(selAuxTab)selAuxTab->setVisible(found!=channels.cend()&&found->number!=QStringLiteral("MAIN")&&!found->subMix);
        if(selStack&&found!=channels.cend()&&(found->number==QStringLiteral("MAIN")||found->subMix)&&(selStack->currentIndex()==1 || (found->number==QStringLiteral("MAIN")&&selStack->currentIndex()==3))){
            selStack->setCurrentIndex(0);
            selStack->hide();
            if(auto* mainTab=selWindow->findChild<QPushButton*>(QStringLiteral("selTabMain")))mainTab->setChecked(true);
            selAuxTab->setChecked(false);
        }
        if (selMain) {
            auto* current = found == channels.cend() ? nullptr : &channels[std::distance(channels.cbegin(), found)];
            selMain->setChannel(current);
            if (current && auxSettingsWindow && auxWindowChannelId == current->id
                && auxSettingsWindow->windowTitle() != QStringLiteral("WinJACKNexus - %1 AUX 设置").arg(current->name)) {
                const auto selectedBus = auxSettingsWindow->selectedBusId();
                const auto key = current->auxId ? QStringLiteral("aux:%1").arg(current->auxId)
                                               : QStringLiteral("input:%1").arg(current->id);
                auxSettingsWindow->setSource(key, current->name, current->auxId, current->layout, auxSourceNumber(*current));
                auxSettingsWindow->selectBus(selectedBus);
            }
            if (selMainChannelId != (current ? current->id : 0)) {
                selMainChannelId = current ? current->id : 0;
                if (current && subSettingsWindow && selStack && selStack->currentIndex()==3) showSelAux(true);
                if (current && auxSettingsWindow) {
                    if (selStack && selStack->currentIndex() == 3) showSelAux(true);
                    if (selStack && selStack->currentIndex() == 1) showSelAux();
                    else {
                        auxWindowChannelId = current->id;
                        const auto key = current->auxId ? QStringLiteral("aux:%1").arg(current->auxId)
                                                        : QStringLiteral("input:%1").arg(current->id);
                        auxSettingsWindow->setSource(key,current->name,current->auxId,current->layout,auxSourceNumber(*current));
                        auxSettingsWindow->setChainEnabled(model && current->modelId
                            ? model->strip(current->modelId)->auxSendsEnabled : current->aux);
                    }
                }
                if (selStack && selStack->currentIndex()==2) showSelFx();
            }
        }
    }

    void selectChannel(int index)
    {
        selectedChannelId = channels[index].id;
        WJN_LOG_INFO("Mixer channel selected id=%u name=%s",
            selectedChannelId, channels[index].name.toUtf8().constData());
        updateSel();
        owner.update();
    }

    void showLargeScreen()
    {
        if (!selWindow) {
            selWindow = new QWidget(&owner, Qt::Window);
            selWindow->setAttribute(Qt::WA_DeleteOnClose);
            selWindow->setObjectName(QStringLiteral("selLargeScreen"));
            selWindow->setWindowTitle(QStringLiteral("Channel SEL"));
            selWindow->resize(1394, 885);
            selWindow->setMinimumSize(1100, 760);
            selWindow->setStyleSheet(QStringLiteral("#selLargeScreen {background:#08192b;color:#e9f2ff;} #selHeader QPushButton {background:#1b3048;color:#d5e8ff;border:1px solid #3b5675;border-radius:6px;padding:6px 18px;font-size:16px;} #selHeader QPushButton:checked {background:#123d62;border:1px solid #2bc7ff;color:white;}"));
            auto* layout = new QVBoxLayout(selWindow);
            layout->setContentsMargins(5,8,5,5);
            layout->setSpacing(0);
            auto* header = new QWidget(selWindow);
            header->setObjectName(QStringLiteral("selHeader"));
            selHeader=header;
            header->setFixedHeight(64);
            auto* headerLayout = new QHBoxLayout(header);
            headerLayout->setContentsMargins(24,5,16,5);
            selLabel = new Controls::TextLabel(selWindow);
            selLabel->setObjectName(QStringLiteral("selSelectedChannel"));
            QFont font = selLabel->font(); font.setPixelSize(26); font.setBold(true); selLabel->setFont(font);
            auto* icon = new Controls::TextLabel(QStringLiteral("🎙"),header);
            QFont iconFont=icon->font();iconFont.setPixelSize(30);icon->setFont(iconFont);
            headerLayout->addWidget(icon);
            auto* prefix = new Controls::TextLabel(QStringLiteral("Channel SEL -"),header);
            QFont prefixFont=prefix->font();prefixFont.setPixelSize(24);prefixFont.setBold(true);prefix->setFont(prefixFont);
            headerLayout->addWidget(prefix);
            headerLayout->addWidget(selLabel,1);
            const std::array<QString,4> tabs { QStringLiteral("Main"),QStringLiteral("AUX"),QStringLiteral("FX"),QStringLiteral("Sub Mix") };
            auto* content = new SelContentHost(selWindow);
            selMain = new SelMainPage(content);
            selStack = new QStackedWidget(content);
            selStack->setObjectName(QStringLiteral("selPages"));
            selStack->setStyleSheet(QStringLiteral("#selPages {background:#0b1a2b;}"));
            selStack->setAutoFillBackground(true);
            content->setPages(selMain,selStack);
            selMain->changed = [this] {
                const auto found=std::find_if(channels.begin(),channels.end(),[this](const Channel& item){return item.id==selectedChannelId;});
                if (found != channels.end()) commitChannel(*found);
                if(found!=channels.cend()&&found->auxId&&found->customColor)
                    auxState->setBusAccent(found->auxId,found->accent);
                if (selMain->draggingControl()) return;
                updateSel();owner.update();if(auxSettingsWindow)auxSettingsWindow->update();
            };
            selMain->dragFinished = [this] {
                updateSel();owner.update();if(auxSettingsWindow)auxSettingsWindow->update();
            };
            selMain->detailsChanged = [this] {
                const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item){return item.id==selectedChannelId;});
                if(found==channels.cend())return;
                if(auxSettingsWindow&&auxWindowChannelId==found->id){
                    const auto key=found->auxId?QStringLiteral("aux:%1").arg(found->auxId):QStringLiteral("input:%1").arg(found->id);
                    auxSettingsWindow->setSource(key,found->name,found->auxId,found->layout,auxSourceNumber(*found));
                }
                if(auto fx=fxSettingsWindows.find(found->id);fx!=fxSettingsWindows.end()&&fx->second)
                    fx->second->setSourceName(found->name);
            };
            selMain->layoutChanged = [this](Common::AuxChannelLayout nextLayout) {
                const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item){return item.id==selectedChannelId;});
                if(found!=channels.cend()&&found->number!=QStringLiteral("MAIN"))
                    setChannelLayout(static_cast<int>(std::distance(channels.cbegin(),found)),nextLayout);
            };
            selStack->addWidget(new QWidget(selStack));
            selAuxPage = new QWidget(selStack);
            auto* auxLayout = new QVBoxLayout(selAuxPage);auxLayout->setContentsMargins(0,0,0,0);
            selStack->addWidget(selAuxPage);
            selFxPage = new QWidget(selStack);
            auto* fxLayout = new QVBoxLayout(selFxPage);fxLayout->setContentsMargins(0,0,0,0);
            selStack->addWidget(selFxPage);
            selSubPage = new QWidget(selStack);
            auto* subLayout = new QVBoxLayout(selSubPage); subLayout->setContentsMargins(0,0,0,0);
            selStack->addWidget(selSubPage);
            for(int i=0;i<4;++i) {
                auto* tab = new Controls::ActionButton(tabs[i],header);
                tab->setObjectName(QStringLiteral("selTab%1").arg(tabs[i]));
                tab->setCheckable(true);
                tab->setChecked(i==0);
                tab->setFixedWidth(106);
                if(i==1)selAuxTab=tab;
                if(i==3)selSubTab=tab;
                headerLayout->addWidget(tab);
                connect(tab,&QPushButton::clicked,selWindow,[this,i,header] {
                    if(i==1)showSelAux();
                    if(i==2)showSelFx();
                    if(i==3)showSelAux(true);
                    selStack->setCurrentIndex(i);
                    selStack->setVisible(i!=0);
                    for(int j=0;j<4;++j)if(auto* item=header->findChild<QPushButton*>(QStringLiteral("selTab%1").arg(std::array<QString,4>{QStringLiteral("Main"),QStringLiteral("AUX"),QStringLiteral("FX"),QStringLiteral("Sub Mix")}[j])))item->setChecked(j==i);
                    updateSel();
                });
            }
            auto* close = new Controls::ActionButton(QStringLiteral("×"),header);
            close->setFixedWidth(48);headerLayout->addWidget(close);
            connect(close,&QPushButton::clicked,selWindow,&QWidget::close);
            layout->addWidget(header);
            layout->addWidget(content,1);
            selStack->hide();
        }
        updateSel();
        selWindow->show();
        selWindow->raise();
        selWindow->activateWindow();
    }

    void showMonitorSel()
    {
        if (!monitorSel) {
            monitorSel = new MonitorSelPage(&owner);
            monitorSel->setWindowFlags(Qt::Window);
            monitorSel->setAttribute(Qt::WA_DeleteOnClose);
            monitorSel->setWindowTitle(QStringLiteral("监听 SEL"));
            monitorSel->resize(1100, 760);
            monitorSel->layoutChanged = [this](Common::MixerLayout layout) {
                if (model) {
                    auto candidate = *model;
                    candidate.setMonitorLayout(layout);
                    if (!commitTopology(candidate)) return;
                }
                monitorLayout = layout;
                monitorPolarCurve.clear();
                monitorSel->setLayoutMode(layout);
                owner.update();
            };
        }
        monitorSel->setLayoutMode(model ? model->monitorLayout() : monitorLayout);
        monitorSel->show();
        monitorSel->raise();
        monitorSel->activateWindow();
    }

    void showSelAux(bool subMode = false)
    {
        const auto selected=std::find_if(channels.begin(),channels.end(),[this](const Channel& channel){return channel.id==selectedChannelId;});
        if(selected!=channels.end()&&(selected->number==QStringLiteral("MAIN")||(!subMode&&selected->subMix))) return;
        if(!selStack)return;
        const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& c){return c.id==selectedChannelId;});
        if(found==channels.cend())return;
        auto& auxSettingsWindow = subMode ? subSettingsWindow : this->auxSettingsWindow;
        auto& auxState = subMode ? subState : this->auxState;
        auto& auxWindowChannelId = subMode ? subWindowChannelId : this->auxWindowChannelId;
        auto* selAuxPage = subMode ? selSubPage.data() : this->selAuxPage.data();
        if (subMode) {
            subViewSignature = subMixViewSignature();
            auxState->clearBuses();
            for (const auto& channel : channels) if (channel.subMix) {
                auto& bus = auxState->addBus(channel.layout);
                bus.id = channel.id; bus.name = channel.name; bus.accent = channel.accent;
            }
        }
        auxWindowChannelId=found->id;
        if(!auxSettingsWindow){
            auxSettingsWindow=new AuxSettingsWindow(auxState,&owner);
            auxSettingsWindow->setSubMixMode(subMode);
            if(subMode) auxSettingsWindow->setObjectName(QStringLiteral("selSubMixRoutes"));
            auxSettingsWindow->setAttribute(Qt::WA_DeleteOnClose);
            auxSettingsWindow->setStateChangedCallback([this,subMode]{if(!subMode)rebuildAuxChannels();updateSel();owner.update();});
            if (model) {
                auxSettingsWindow->setChannelMeterReadCallback([this,subMode](bool post, std::size_t index) {
                    const auto source = std::find_if(channels.cbegin(), channels.cend(), [this,subMode](const Channel& item) {
                        return item.id == (subMode ? subWindowChannelId : this->auxWindowChannelId);
                    });
                    return source != channels.cend() && channelMeterRead && source->modelId
                        ? channelMeterRead(source->modelId, post, index) : 0.0F;
                });
                if (polarRead) auxSettingsWindow->setPolarReadCallback(
                    [this,subMode](std::uint32_t busId, Common::MixerPolarSnapshot& snapshot) {
                        const auto source = std::find_if(channels.cbegin(), channels.cend(), [this,subMode](const Channel& item) {
                            return item.id == (subMode ? subWindowChannelId : this->auxWindowChannelId);
                        });
                        const auto target = std::find_if(channels.cbegin(), channels.cend(), [busId,subMode](const Channel& item) {
                            return subMode ? item.subMix && item.id == busId : item.auxId == busId;
                        });
                        return source != channels.cend() && target != channels.cend()
                            && source->modelId && target->modelId
                            && polarRead(source->modelId, target->modelId, snapshot);
                    });
                auxSettingsWindow->setStateCommitCallback([this,subMode] {
                    return commitAuxSettings(subMode ? true : this->auxSettingsWindow->chainEnabled(),subMode);
                });
                auxSettingsWindow->setChainCommitHandler([this,subMode](bool enabled) {
                    return commitAuxSettings(enabled,subMode);
                });
            }
            if (!subMode) auxSettingsWindow->setChainEnabledHandler([this](bool enabled){
                auto it=std::find_if(channels.begin(),channels.end(),[this](const Channel& c){return c.id==this->auxWindowChannelId;});
                if(it!=channels.end()){it->aux=enabled;owner.update();}
            });
        }
        const auto selfId = subMode ? (found->subMix ? found->id : 0) : found->auxId;
        const auto key=subMode?QStringLiteral("sub:%1").arg(found->id):found->auxId?QStringLiteral("aux:%1").arg(found->auxId):QStringLiteral("input:%1").arg(found->id);
        auxSettingsWindow->setSource(key,found->name,selfId,found->layout,auxSourceNumber(*found));
        if (model && found->modelId) {
            if (const auto* strip = model->strip(found->modelId)) {
                auto& sourceBuses = auxState->busesForSource(key, selfId);
                for (auto& bus : sourceBuses) {
                    if (bus.selfRoute) continue;
                    const auto target = std::find_if(channels.cbegin(), channels.cend(), [&bus,subMode](const Channel& channel) {
                        return subMode ? channel.subMix && channel.id == bus.id : channel.auxId == bus.id;
                    });
                    if (target == channels.cend()) continue;
                    const auto send = std::find_if(strip->sends.cbegin(), strip->sends.cend(), [target](const Common::MixerAuxSend& item) {
                        return item.targetId == target->modelId;
                    });
                    bus.enabled = send != strip->sends.cend() && send->enabled;
                    if (send == strip->sends.cend()) continue;
                    bus.preFader = !subMode && send->preFader;
                    bus.panMode = send->panner.mode == Common::MixerPanMode::classic
                        ? Common::AuxPanMode::classic : Common::AuxPanMode::spatial;
                    bus.classicPan = send->panner.classicPan;
                    bus.width = send->panner.width;
                    bus.angleDegrees = send->panner.angleDegrees;
                    bus.sendsLinked = send->panner.linked;
                    bus.showPolarCurves = send->panner.showPolarCurves;
                    bus.showInputCurve = send->panner.showInputCurve;
                    bus.showOutputCurve = send->panner.showOutputCurve;
                    for (std::size_t i = 0; i < bus.adjustablePoints.size() && i < send->panner.points.size(); ++i) {
                        const auto& point = send->panner.points[i];
                        bus.adjustablePoints[i] = QPointF(point.x, point.y);
                        if (i < bus.pointInfluenceRadii.size()) bus.pointInfluenceRadii[i] = point.influenceRadius;
                        if (i < bus.sends.size()) {
                            bus.sends[i].levelDb = point.levelDb;
                            bus.sends[i].mute = point.mute;
                            bus.sends[i].solo = point.solo;
                        }
                    }
                }
                auxSettingsWindow->acceptExternalState();
            }
        }
        auxSettingsWindow->setChainEnabled(subMode ? true : model && found->modelId
            ? model->strip(found->modelId)->auxSendsEnabled : found->aux);
        if(auxSettingsWindow->parentWidget()!=selAuxPage){
            auxSettingsWindow->hide();auxSettingsWindow->setWindowFlags(Qt::Widget);
            auxSettingsWindow->setParent(selAuxPage,Qt::Widget);
            selAuxPage->layout()->addWidget(auxSettingsWindow);
        }
        auxSettingsWindow->show();
    }

    void showSelFx()
    {
        if(!selStack)return;
        const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& c){return c.id==selectedChannelId;});
        if(found==channels.cend())return;
        auto* fx=fxSettingsFor(static_cast<int>(std::distance(channels.cbegin(),found)));
        auto* oldItem=selFxPage->layout()->itemAt(0);
        auto* current=oldItem?oldItem->widget():nullptr;
        if(current!=fx){
            if(current){
                selFxPage->layout()->removeWidget(current);
                current->hide();
            }
            fx->hide();
            fx->setParent(selFxPage,Qt::Widget);
            selFxPage->layout()->addWidget(fx);
        }
        fx->show();
    }

    QRectF channelRect(int index) const
    {
        qreal x = 0.0;
        if (index < inputChannelCount)
            x = inputViewport().left() + index * channelWidth - effectiveInputScrollOffset();
        else if (index < subMixStart())
            x = auxViewport().left() + (index - inputChannelCount) * channelWidth - effectiveAuxScrollOffset();
        else if (index < masterIndex())
            x = subMixViewport().left() + (index-subMixStart())*channelWidth-effectiveSubMixScrollOffset();
        else
            x = owner.width() - margin - channelWidth;
        return { x, topMargin, channelWidth, channelAreaHeight() };
    }

    QRectF inputViewport() const
    {
        const auto available = scrollableAreaWidth();
        const auto inputWidth = std::floor(available * effectiveInputRatio()+1e-7);
        return { margin, topMargin, inputWidth, channelAreaHeight() };
    }

    QRectF auxViewport() const
    {
        const auto input = inputViewport();
        const auto width = std::max(0.0, std::floor(scrollableAreaWidth()*effectiveSubMixBoundary()) - input.width());
        return { input.right() + groupGap, topMargin, width, channelAreaHeight() };
    }

    QRectF subMixViewport() const
    {
        const auto aux=auxViewport();
        return {aux.right()+groupGap,topMargin,
            std::max(0.0,scrollableAreaWidth()-inputViewport().width()-aux.width()),channelAreaHeight()};
    }
    QRectF viewportForChannel(int index) const
    {
        return index < inputChannelCount ? inputViewport() : index < subMixStart() ? auxViewport() : subMixViewport();
    }

    qreal channelAreaHeight() const
    {
        return std::max(0.0, owner.height() - topMargin - margin - scrollbarHeight - scrollbarGap);
    }

    qreal scrollableAreaWidth() const
    {
        return std::max(0.0, monitorRect().left() - groupGap - margin - 2*groupGap);
    }

    QRectF monitorRect() const
    {
        return {owner.width() - margin - 2 * channelWidth - groupGap,
                topMargin, channelWidth, channelAreaHeight()};
    }

    QRectF monitorMeterRect(qreal heightScale) const
    {
        const auto mainMeter = meterRect(masterIndex(), heightScale);
        return mainMeter.translated(monitorRect().left() - channelRect(masterIndex()).left(), 0);
    }

    QRectF monitorFaderTrack() const
    {
        const auto mainTrack = faderTrack(masterIndex());
        return mainTrack.translated(monitorRect().left() - channelRect(masterIndex()).left(), 0);
    }

    QRectF monitorSelectRect(qreal heightScale) const
    {
        return channelButtonRect(masterIndex(), heightScale, 0).translated(
            monitorRect().left() - channelRect(masterIndex()).left(), 0);
    }

    qreal effectiveInputRatio() const
    {
        const auto available = scrollableAreaWidth();
        if (available <= 0.0)
            return .6;
        const auto minimumRatio = std::min(1.0/3.0, channelWidth / available);
        return std::clamp(inputRatio, minimumRatio, 1.0 - 2*minimumRatio);
    }

    qreal effectiveSubMixBoundary() const
    {
        const auto available=scrollableAreaWidth();
        const auto minimum=available>0?std::min(1.0/3.0,channelWidth/available):0.0;
        return std::clamp(subMixBoundary,effectiveInputRatio()+minimum,1.0-minimum);
    }
    QRectF subMixDividerRect() const
    {
        return {auxViewport().right(),topMargin,groupGap,owner.height()-topMargin-margin};
    }
    void setSubMixBoundaryFromPosition(qreal x)
    {
        if(scrollableAreaWidth()>0)subMixBoundary=(x-margin-groupGap)/scrollableAreaWidth();
    }

    QRectF groupDividerRect() const
    {
        const auto input = inputViewport();
        return { input.right(), topMargin, groupGap, owner.height() - topMargin - margin };
    }

    void setInputRatioFromPosition(qreal x)
    {
        const auto available = scrollableAreaWidth();
        if (available <= 0.0)
            return;
        inputRatio = std::clamp((x - margin) / available, 0.0, 1.0);
        inputScrollOffset = effectiveInputScrollOffset();
        auxScrollOffset = effectiveAuxScrollOffset();
    }

    qreal maximumInputScrollOffset() const
    {
        return std::max(0.0, inputChannelCount * channelWidth - inputViewport().width());
    }

    qreal maximumAuxScrollOffset() const
    {
        return std::max(0.0, auxChannelCount() * channelWidth - auxViewport().width());
    }

    qreal effectiveInputScrollOffset() const
    {
        return std::clamp(inputScrollOffset, 0.0, maximumInputScrollOffset());
    }

    qreal effectiveAuxScrollOffset() const
    {
        return std::clamp(auxScrollOffset, 0.0, maximumAuxScrollOffset());
    }

    qreal maximumSubMixScrollOffset() const
    {
        return std::max(0.0,subMixCount()*channelWidth-subMixViewport().width());
    }
    qreal effectiveSubMixScrollOffset() const {return std::clamp(subMixScrollOffset,0.0,maximumSubMixScrollOffset());}
    qreal maximumGroupScroll(int group) const {return group==0?maximumInputScrollOffset():group==1?maximumAuxScrollOffset():maximumSubMixScrollOffset();}
    qreal groupScroll(int group) const {return group==0?effectiveInputScrollOffset():group==1?effectiveAuxScrollOffset():effectiveSubMixScrollOffset();}
    QRectF groupViewport(int group) const {return group==0?inputViewport():group==1?auxViewport():subMixViewport();}
    QRectF scrollbarTrack(int group) const
    {
        const auto viewport = groupViewport(group);
        return { viewport.left() + 4.0, viewport.bottom() + scrollbarGap,
                 std::max(0.0, viewport.width() - 8.0), scrollbarHeight };
    }

    QRectF scrollbarThumb(int group) const
    {
        const auto track = scrollbarTrack(group);
        const auto channelCount = group==0?inputChannelCount:group==1?auxChannelCount():subMixCount();
        const auto contentWidth = channelCount * channelWidth;
        const auto viewportWidth = groupViewport(group).width();
        const auto maximum = maximumGroupScroll(group);
        const auto offset = groupScroll(group);
        const auto thumbWidth = maximum <= 0.0 ? track.width()
            : std::max(32.0, track.width() * viewportWidth / contentWidth);
        const auto travel = std::max(0.0, track.width() - thumbWidth);
        const auto x = track.left() + (maximum <= 0.0 ? 0.0 : travel * offset / maximum);
        return { x, track.top(), thumbWidth, track.height() };
    }

    void setScrollFromThumb(int group, qreal thumbLeft)
    {
        const auto track = scrollbarTrack(group);
        const auto thumb = scrollbarThumb(group);
        const auto travel = std::max(0.0, track.width() - thumb.width());
        const auto maximum = maximumGroupScroll(group);
        const auto normalized = travel <= 0.0 ? 0.0
            : std::clamp((thumbLeft - track.left()) / travel, 0.0, 1.0);
        if(group==0)inputScrollOffset=normalized*maximum;
        else if(group==1)auxScrollOffset=normalized*maximum;
        else subMixScrollOffset=normalized*maximum;
    }

    QRectF faderTrack(int index) const
    {
        const auto card = channelRect(index);
        const auto heightScale = std::clamp(owner.height() / 768.0, .72, 1.35);
        const auto meterBottom = card.top() + 65.0 * heightScale
            + card.height() * .28 + 16.0 * heightScale;
        const auto top = meterBottom + (20.0 + 3.70 * 28.0 + 32.0) * heightScale;
        const auto bottom = card.bottom() - 42.0 * heightScale;
        return { card.center().x() - 3.0, top, 6.0, std::max(1.0, bottom - top) };
    }

    QRectF meterRect(int index, qreal heightScale) const
    {
        const auto card = channelRect(index);
        const auto top = card.top() + 65.0 * heightScale;
        return { card.center().x() - 8.0, top, 16.0, card.height() * .28 + 16.0 * heightScale };
    }

    QRectF meterModeRect(int index, qreal heightScale) const
    {
        const auto meter = meterRect(index, heightScale);
        return { meter.center().x() - 19.0, meter.top() - 20.0 * heightScale,
                 38.0, 16.0 * heightScale };
    }

    QRectF meterHitRect(int index, qreal heightScale) const
    {
        return meterRect(index, heightScale).united(meterModeRect(index, heightScale)).adjusted(-4, -2, 4, 2);
    }

    QRectF channelButtonRect(int index, qreal heightScale, int row) const
    {
        const auto card = channelRect(index);
        const auto buttonHeight = 28.0 * heightScale;
        const auto top = meterRect(index, heightScale).bottom() + 20.0 * heightScale;
        return { card.left() + 6.0, top + buttonHeight * 1.35 * row, card.width() - 12.0, buttonHeight };
    }

    QRectF splitButtonRect(int index, qreal heightScale, int row, bool right) const
    {
        auto button = channelButtonRect(index, heightScale, row);
        const auto halfWidth = (button.width() - 6.0) / 2.0;
        if (right) button.setLeft(button.left() + halfWidth + 6.0);
        else button.setWidth(halfWidth);
        return button;
    }

    QRectF muteButtonRect(int index, qreal heightScale) const
    {
        return index == masterIndex() ? channelButtonRect(index, heightScale, 0)
                                      : splitButtonRect(index, heightScale, 0, false);
    }

    QRectF soloButtonRect(int index, qreal heightScale) const
    {
        return splitButtonRect(index, heightScale, 0, true);
    }

    QRectF selectButtonRect(int index, qreal heightScale) const
    {
        return index == masterIndex() ? channelButtonRect(index, heightScale, 1)
                                      : splitButtonRect(index, heightScale, 1, false);
    }

    QRectF mixButtonRect(int index, qreal heightScale) const
    {
        return splitButtonRect(index, heightScale, 1, true);
    }

    QRectF listenButtonRect(int index, qreal heightScale, bool afl) const
    {
        return splitButtonRect(index, heightScale, 2, afl);
    }

    int hitChannel(const QPointF& position) const
    {
        if (inputViewport().contains(position)) {
            for (int index = 0; index < inputChannelCount; ++index) {
                if (channelRect(index).contains(position))
                    return index;
            }
        }
        if (auxViewport().contains(position)) {
            for (int index = inputChannelCount; index < subMixStart(); ++index) {
                if (channelRect(index).contains(position))
                    return index;
            }
        }
        if(subMixViewport().contains(position))
            for(int index=subMixStart();index<masterIndex();++index)
                if(channelRect(index).contains(position)) return index;
        return channelRect(masterIndex()).contains(position) ? masterIndex() : -1;
    }

    MixerWindow& owner;
    Common::DigitalMixerState* model {};
    MixerWindow::TopologyCommit commit;
    MixerWindow::MeterRead meterRead;
    MixerWindow::PolarRead polarRead;
    MixerWindow::ChannelMeterRead channelMeterRead;
    MixerWindow::EffectAnalysisRead effectAnalysisRead;
    Common::MixerPolarCurve selPolarCurve;
    std::uint64_t polarSourceId {};
    int polarStaleTicks {};
    std::shared_ptr<Common::AuxMixerState> auxState { std::make_shared<Common::AuxMixerState>() };
    QPointer<AuxSettingsWindow> auxSettingsWindow;
    QPointer<AuxSettingsWindow> subSettingsWindow;
    QString subViewSignature;
    std::shared_ptr<Common::AuxMixerState> subState = std::make_shared<Common::AuxMixerState>();
    std::uint32_t subWindowChannelId {};
    QPointer<QWidget> selSubPage;
    QPointer<QPushButton> selSubTab;
    QPointer<QWidget> selWindow;
    QPointer<QLabel> selLabel;
    QPointer<QWidget> selHeader;
    QColor lastHeaderColor;
    QColor lastHeaderTextColor;
    QPointer<QStackedWidget> selStack;
    QPointer<QWidget> selAuxPage;
    QPointer<QWidget> selFxPage;
    QPointer<QPushButton> selAuxTab;
    QPointer<SelMainPage> selMain;
    std::uint32_t selMainChannelId {};
    std::uint32_t selectedChannelId { 1 };
    std::map<int, QPointer<FxSettingsWindow>> fxSettingsWindows;
    std::vector<Channel> channels;
    qreal monitorFader { Common::mixerUnityFaderPosition };
    Common::MixerLayout monitorLayout { Common::MixerLayout::stereo };
    Controls::MeterBallistics monitorMeter;
    Common::MixerPolarCurve monitorPolarCurve;
    QPointer<MonitorSelPage> monitorSel;
    QTimer timer;
    QElapsedTimer meterFrameClock;
    QElapsedTimer meterReadoutClock;
    int dragging = -1;
    int reordering = -1;
    int reorderTarget = -1;
    int scrollbarDragging = 0;
    bool groupDividerDragging = false;
    bool subMixDividerDragging = false;
    qreal scrollbarDragOffset = 0.0;
    qreal inputScrollOffset = 0.0;
    qreal auxScrollOffset = 0.0;
    qreal inputRatio = .6;
    qreal subMixBoundary = .8;
    qreal subMixScrollOffset = 0;
    int inputChannelCount = 8;
    std::uint32_t nextChannelId = 1;
    std::uint32_t auxWindowChannelId {};

    static constexpr qreal margin = 9.0;
    static constexpr qreal topMargin = 40.0;
    static constexpr qreal groupGap = 8.0;
    static constexpr qreal channelWidth = 104.0;
    static constexpr qreal scrollbarHeight = 10.0;
    static constexpr qreal scrollbarGap = 5.0;
};

MixerWindow::MixerWindow(QWidget* parent)
    : MixerWindow(nullptr, {}, {}, parent)
{
}

MixerWindow::MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit, QWidget* parent)
    : MixerWindow(state, std::move(commit), {}, parent)
{
}

MixerWindow::MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit,
                         MeterRead meterRead, QWidget* parent)
    : MixerWindow(state, std::move(commit), std::move(meterRead), {}, parent)
{
}

MixerWindow::MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit,
                         MeterRead meterRead, PolarRead polarRead, QWidget* parent)
    : MixerWindow(state, std::move(commit), std::move(meterRead),
                  std::move(polarRead), {}, parent)
{
}

MixerWindow::MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit,
                         MeterRead meterRead, PolarRead polarRead,
                         ChannelMeterRead channelMeterRead, QWidget* parent)
    : MixerWindow(state, std::move(commit), std::move(meterRead),
                  std::move(polarRead), std::move(channelMeterRead), {}, parent)
{
}

MixerWindow::MixerWindow(Common::DigitalMixerState* state, TopologyCommit commit,
                         MeterRead meterRead, PolarRead polarRead,
                         ChannelMeterRead channelMeterRead,
                         EffectAnalysisRead effectAnalysisRead, QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(*this, state, std::move(commit),
                                   std::move(meterRead), std::move(polarRead),
                                   std::move(channelMeterRead),
                                   std::move(effectAnalysisRead)))
{
    setWindowTitle(QStringLiteral("WinJACKNexus - 调音台"));
    setMinimumSize(760, 560);
    resize(1024, 768);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    if (auto* core = ThemeManager::installedThemeCore())
        connect(core, &ThemeCore::themeChanged, this, [this] {
            impl_->updateSel();
            update();
        });
    auto* menuBar = this->menuBar();
    menuBar->setObjectName(QStringLiteral("mixerMenuBar"));
    auto* channelsMenu = menuBar->addMenu(QStringLiteral("通道管理"));
    auto* inputsMenu = channelsMenu->addMenu(QStringLiteral("添加输入通道"));
    auto* auxMenu = channelsMenu->addMenu(QStringLiteral("添加AUX通道"));
    auto* subMenu = channelsMenu->addMenu(QStringLiteral("添加 Sub Mix 通道"));
    const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                               Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
    for (const auto layout : layouts) {
        connect(inputsMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->addInput(layout); });
        connect(auxMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->addAux(layout); });
        connect(subMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->addSubMix(layout); });
    }
    auto* globalMenu = menuBar->addMenu(QStringLiteral("全局"));
    connect(globalMenu->addAction(QStringLiteral("关闭所有通道独奏/静音")), &QAction::triggered, this, [this] {
        for (auto& channel : impl_->channels) { channel.solo = false; channel.mute = false; }
        if (impl_->model) {
            Common::DigitalMixerState candidate = *impl_->model;
            for (const auto& channel : impl_->channels)
                if (auto* strip = candidate.strip(channel.modelId)) { strip->solo = false; strip->mute = false; }
            if (!impl_->commitTopology(candidate)) {
                for (auto& channel : impl_->channels)
                    if (const auto* strip = impl_->model->strip(channel.modelId)) {
                        channel.solo = strip->solo; channel.mute = strip->mute;
                    }
            }
        }
        update();
    });
    connect(menuBar->addAction(QStringLiteral("查看大屏")), &QAction::triggered, this,
            [this] { impl_->showLargeScreen(); });
}

MixerWindow::~MixerWindow()
{
    for (auto* child : findChildren<QObject*>())
        QObject::disconnect(child, nullptr, this, nullptr);
    // FX editors and AUX controls have callbacks into Impl. Destroy those
    // children while the model and callbacks they reference are still alive.
    for (auto& [id, window] : impl_->fxSettingsWindows)
        if (window) delete window.data();
    if (impl_->auxSettingsWindow) delete impl_->auxSettingsWindow.data();
    // Other child widgets can emit teardown signals into Impl too. Destroy
    // the UI hierarchy before its callback targets and model are released.
    const auto remainingChildren = findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* child : remainingChildren) delete child;
}

bool MixerWindow::channelMixToMain(int index) const
{
    return index >= 0 && index < impl_->masterIndex() && impl_->channels[index].mixToMain;
}

double MixerWindow::mainInputLevelDb() const
{
    return impl_->mainInputLevelDb();
}

double MixerWindow::mainOutputLevelDb() const
{
    return impl_->mainOutputLevelDb();
}

void MixerWindow::paintEvent(QPaintEvent*)
{
    const auto themed = [](const char* role, const QColor& fallback) {
        return Controls::themeTokenColor(QStringLiteral("mixer"), QString::fromLatin1(role), fallback);
    };
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), themed("surface-empty", QColor("#07131f")));
    ThemeBackground::paint(painter, rect(), QStringLiteral("mixer"));
    painter.setOpacity(ThemeBackground::componentOpacity(QStringLiteral("mixer")));

    const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
    for(auto* title : {"Inputs","AUX","Sub Mix"}) {
        const int group=QString(title)==QStringLiteral("Inputs")?0:QString(title)==QStringLiteral("AUX")?1:2;
        const auto viewport=impl_->groupViewport(group);
        QFont font=painter.font();font.setPixelSize(13);font.setBold(true);
        Controls::drawText(painter,{viewport.left(),23,viewport.width(),16},QString::fromLatin1(title),font,
            themed("text-primary",QColor("#dce9ff")),Qt::AlignCenter);
    }
    const auto label = themed("text-primary", QColor("#dce9ff"));
    const auto secondary = themed("text-muted", QColor("#a9bbd7"));
    const auto nameTextColor = [&](const QColor& background) {
        const qreal brightness = .299 * background.red() + .587 * background.green() + .114 * background.blue();
        return brightness > 145 ? themed("control-name-text-dark", QColor("#102033"))
                                : themed("control-name-text-light", QColor("#f5fbff"));
    };
    const auto monitorCard = impl_->monitorRect();
    const auto monitorYellow = themed("fader-monitor-thumb", QColor("#ffe15a"));
    Controls::drawSurface(painter, monitorCard.adjusted(1, 1, -1, -1), Qt::NoBrush, monitorYellow, 4, 1);
    Controls::drawSurface(painter, {monitorCard.left() + 3, monitorCard.top() + heightScale,
                                   monitorCard.width() - 6, 24 * heightScale}, monitorYellow, monitorYellow, 3);
    drawAdaptiveText(painter, {monitorCard.left(), monitorCard.top() + 2 * heightScale,
                               monitorCard.width(), 20 * heightScale},
                     QStringLiteral("监听"), nameTextColor(monitorYellow), 16 * heightScale, 10 * heightScale,
                     Qt::AlignCenter, true);
    const auto monitorMeterRect = impl_->monitorMeterRect(heightScale);
    Controls::MeterStyle monitorStyle;
    monitorStyle.vertical = true;
    monitorStyle.border = themed("meter-level-border", QColor("#07131f"));
    monitorStyle.zones = {{-12, themed("monitor-meter-low-active", QColor("#2bce75")),
                                 themed("monitor-meter-low-inactive", QColor("#163e2e"))},
                          {0, themed("monitor-meter-mid-active", QColor("#f4cd42")),
                               themed("monitor-meter-mid-inactive", QColor("#4a3f1d"))},
                          {12, themed("monitor-meter-high-active", QColor("#f35656")),
                               themed("monitor-meter-high-inactive", QColor("#4c242a"))}};
    Controls::drawSurface(painter, monitorMeterRect,
                          themed("meter-level-surface", QColor("#101a22")),
                          themed("meter-level-surface-border", QColor("#0a0f14")), 3, 4);
    Controls::drawMeter(painter, monitorMeterRect.adjusted(2.5, 2.5, -2.5, -2.5),
                        impl_->monitorMeter.levelDb(), impl_->monitorMeter.peakDb(), monitorStyle);
    Controls::ButtonStyle monitorSelStyle;
    monitorSelStyle.activeBorder = themed("monitor-select-accent", monitorYellow);
    Controls::drawStyledButton(painter, impl_->monitorSelectRect(heightScale),
                               QStringLiteral("SEL"), false, monitorSelStyle);
    Controls::FaderStyle monitorFaderStyle;
    monitorFaderStyle.accent = monitorYellow;
    monitorFaderStyle.track = themed("fader-monitor-track", monitorFaderStyle.track);
    monitorFaderStyle.grip = themed("fader-monitor-grip", monitorFaderStyle.grip);
    monitorFaderStyle.thumbWidth = monitorCard.width() * .26;
    monitorFaderStyle.thumbHeight = 40 * heightScale;
    Controls::drawFader(painter, impl_->monitorFaderTrack(), impl_->monitorFader,
                        monitorFaderStyle);
    drawText(painter, {monitorCard.left(), impl_->monitorFaderTrack().bottom() + 10 * heightScale,
                       monitorCard.width(), 27 * heightScale},
             faderDbText(impl_->monitorFader), label, 12 * heightScale);

    for (int index = 0; index < static_cast<int>(impl_->channels.size()); ++index) {
        painter.save();
        if (index != impl_->masterIndex()) {
            const auto viewport = impl_->viewportForChannel(index);
            painter.setClipRect(viewport);
            if (!impl_->channelRect(index).intersects(viewport)) {
                painter.restore();
                continue;
            }
        }
        const auto& channel = impl_->channels[index];
        const auto card = impl_->channelRect(index);
        if (impl_->reordering >= 0 && index == impl_->reorderTarget
            && index != impl_->reordering) {
            painter.setPen(QPen(themed("group-divider", QColor("#45bfff")), 2));
            const auto edge = index > impl_->reordering ? card.right() - 2 : card.left() + 2;
            painter.drawLine(QPointF(edge, card.top()),
                             QPointF(edge, card.bottom()));
        }
        if (index != 0 && index != impl_->inputChannelCount && index != impl_->masterIndex()) {
            painter.setPen(QPen(themed("group-divider",QColor("#243c57")), 1));
            painter.drawLine(QPointF(card.left(), card.top()), QPointF(card.left(), card.bottom()));
        }
        if (index == impl_->inputChannelCount - 1 || index == impl_->masterIndex() - 1) {
            painter.setPen(QPen(themed("group-divider",QColor("#243c57")), 1));
            const auto right = card.right() - 0.5;
            painter.drawLine(QPointF(right, card.top()), QPointF(right, card.bottom()));
        }

        Controls::drawSurface(painter, card.adjusted(1, 1, -1, -1), Qt::NoBrush, channel.accent, 4, 1);
        Controls::drawSurface(painter, {card.left() + 3, card.top() + heightScale,
                                       card.width() - 6, 24 * heightScale}, channel.accent, channel.accent, 3);
        const auto top = card.top() + 2 * heightScale;
        drawAdaptiveText(painter, { card.left(), top, card.width(), 20 * heightScale }, channel.name,
                 nameTextColor(channel.accent), 16 * heightScale, 10 * heightScale, Qt::AlignCenter, true);
        drawAdaptiveText(painter, { card.left(), top + 26 * heightScale, card.width(), 14 * heightScale }, channel.source,
                 secondary, 12 * heightScale, 8 * heightScale);

        const auto meter = impl_->meterRect(index, heightScale);
        const auto meterMode = impl_->meterModeRect(index, heightScale);
        const auto inputLevel = index == impl_->masterIndex()
            ? impl_->mainInputLevelDb()
            : channel.level + (channel.realMeter ? 0.0 : channel.inputGainDb);
        const auto meterLevel = channel.realMeter ? channel.meterBallistics.levelDb()
            : index == impl_->masterIndex() && channel.postFaderMeter
                ? impl_->mainOutputLevelDb()
                : std::clamp(inputLevel + (channel.postFaderMeter ? faderDbValue(channel.fader) : 0.0), -72.0, 12.0);
        Controls::drawSurface(painter,meterMode,
            channel.postFaderMeter ? themed("control-meter-mode-surface-post",QColor("#163a51"))
                                   : themed("control-meter-mode-surface-input",QColor("#182735")),
            channel.postFaderMeter ? themed("control-meter-mode-border-post",QColor("#33b6ff"))
                                   : themed("control-meter-mode-border-input",QColor("#6f849a")),4);
        drawText(painter, meterMode, channel.postFaderMeter ? QStringLiteral("POST") : QStringLiteral("IN"),
                 channel.postFaderMeter ? themed("control-meter-mode-text-post",QColor("#8edcff"))
                                        : secondary, 9 * heightScale, Qt::AlignCenter, true);
        Controls::drawSurface(painter,meter,
            themed("meter-level-surface",QColor("#101a22")),
            themed("meter-level-surface-border",QColor("#0a0f14")),3,4);
        const auto innerMeter = meter.adjusted(2.5, 2.5, -2.5, -2.5);
        const auto yForDb = [&](qreal db) {
            const auto normalized = std::clamp((db + 72.0) / 84.0, 0.0, 1.0);
            return innerMeter.bottom() - normalized * innerMeter.height();
        };
        Controls::MeterStyle meterStyle;
        meterStyle.vertical=true;
        meterStyle.border=themed("meter-level-border",QColor("#07131f"));
        meterStyle.zones={{-12,themed("meter-level-zone-low-active",QColor("#26dc68")),
                               themed("meter-level-zone-low-inactive",QColor("#163e2e"))},
                          {0,themed("meter-level-zone-mid-active",QColor("#ffe15a")),
                             themed("meter-level-zone-mid-inactive",QColor("#4a3f1d"))},
                          {12,themed("meter-level-zone-high-active",QColor("#ff5353")),
                              themed("meter-level-zone-high-inactive",QColor("#4c242a"))}};
        auto scaleStyle=meterStyle;
        scaleStyle.ticks={{12,QStringLiteral("+12")},{0,QStringLiteral("0")},
                          {-12,QStringLiteral("-12")},{-24,QStringLiteral("-24")},
                          {-36,QStringLiteral("-36")},{-48,QStringLiteral("-48")},
                          {-60,QStringLiteral("-60")},{-72,QStringLiteral("-72")}};
        scaleStyle.ticksLeft=true;
        scaleStyle.tickBandPixels=card.width()*.34;
        scaleStyle.tickFontPixelSize=std::max(1,qRound(9*heightScale));
        scaleStyle.labelColor=secondary;
        Controls::drawMeterScale(painter,{card.left()+2,innerMeter.top(),
            innerMeter.left()-card.left()-6,innerMeter.height()},scaleStyle);
        Controls::drawMeter(painter,innerMeter,meterLevel,
            channel.realMeter?channel.meterBallistics.peakDb():meterLevel,meterStyle);
        painter.setPen(QPen(themed("meter-level-threshold-line",QColor("#07131f")), 1));
        painter.drawLine(QPointF(innerMeter.left(), yForDb(-12.0)), QPointF(innerMeter.right(), yForDb(-12.0)));
        painter.drawLine(QPointF(innerMeter.left(), yForDb(0.0)), QPointF(innerMeter.right(), yForDb(0.0)));

        const auto drawButton = [&](const QRectF& button, const QString& text, const QColor& color, bool active) {
            Controls::drawSurface(painter,button,
                active ? withAlpha(color, 65) : themed("control-button-surface-normal",QColor("#16283a")),
                active ? color : withAlpha(color, 190),5,1.1);
            drawText(painter, button, text,
                active ? themed("control-button-text-selected",QColor("#ffffff"))
                       : themed("control-button-text-normal",label),
                12 * heightScale, Qt::AlignCenter, true);
        };
        const bool sipActive=std::any_of(impl_->channels.cbegin(),impl_->channels.cend(),[](const Channel& item) {
            return item.number!=QStringLiteral("MAIN") && item.solo;
        });
        const bool effectiveMute=channel.mute || (sipActive && index!=impl_->masterIndex() && !channel.solo);
        drawButton(impl_->muteButtonRect(index, heightScale), index == impl_->masterIndex() ? "MUTE" : "M",
                   themed("control-button-mute-accent",QColor("#ff4f55")), effectiveMute);
        if(index!=impl_->masterIndex())
            drawButton(impl_->soloButtonRect(index, heightScale), "S",
                       themed("control-button-solo-accent",QColor("#f7aa2f")), channel.solo);
        drawButton(impl_->selectButtonRect(index, heightScale), "SEL",
                   themed("control-button-select-accent",QColor("#66809b")), false);
        if (index != impl_->masterIndex())
            drawButton(impl_->mixButtonRect(index, heightScale), "MIX",
                       themed("control-button-mix-accent",QColor("#24b8ff")), channel.mixToMain);
        if (index != impl_->masterIndex()) {
            drawButton(impl_->listenButtonRect(index, heightScale, false), "PFL",
                       themed("control-button-pfl-accent", monitorYellow),
                       channel.listenMode == Common::MixerListenMode::pfl);
            drawButton(impl_->listenButtonRect(index, heightScale, true), "AFL",
                       themed("control-button-afl-accent", monitorYellow),
                       channel.listenMode == Common::MixerListenMode::afl);
        }

        const auto faderKind = channel.subMix ? "submix" : index < impl_->inputChannelCount ? "input"
            : index < impl_->masterIndex() ? "aux" : "master";
        const auto faderColor = themed(
            QStringLiteral("fader-%1-thumb").arg(QString::fromLatin1(faderKind)).toLatin1().constData(),
            channel.subMix ? QColor("#37d879") : index < impl_->inputChannelCount ? QColor("#9aa8b7")
                : index < impl_->masterIndex() ? QColor("#33b6ff") : QColor("#ff5353"));
        const auto track = impl_->faderTrack(index);
        Controls::FaderStyle faderStyle;
        faderStyle.accent = faderColor;
        faderStyle.track = themed(
            QStringLiteral("fader-%1-track").arg(QString::fromLatin1(faderKind)).toLatin1().constData(),
            faderStyle.track);
        faderStyle.grip = themed(
            QStringLiteral("fader-%1-grip").arg(QString::fromLatin1(faderKind)).toLatin1().constData(),
            faderStyle.grip);
        faderStyle.thumbWidth = card.width() * .26;
        faderStyle.thumbHeight = 40 * heightScale;
        Controls::drawFader(painter, track, channel.fader, faderStyle);

        const auto db = faderDbText(channel.fader);
        drawText(painter, { card.left(), track.bottom() + 10 * heightScale, card.width(), 27 * heightScale }, db, label, 12 * heightScale);
        painter.restore();
    }

    const auto drawScrollbar = [&](int group, const QColor& color) {
        const auto track = impl_->scrollbarTrack(group);
        Controls::drawSurface(painter,track,
            themed("control-scrollbar-track",QColor("#101c29")),{},track.height()/2.0);
        const auto thumb = impl_->scrollbarThumb(group).adjusted(1, 1, -1, -1);
        Controls::drawSurface(painter,thumb,withAlpha(color,210),{},thumb.height()/2.0);
    };
    drawScrollbar(0, themed("control-scrollbar-input-thumb",QColor("#45bfff")));
    drawScrollbar(1, themed("control-scrollbar-aux-thumb",QColor("#8f78ff")));

    drawScrollbar(2,themed("control-scrollbar-submix-thumb",QColor("#37d879")));
    const auto subDivider=impl_->subMixDividerRect();
    Controls::drawSurface(painter,{subDivider.center().x()-1.5,subDivider.top(),3.0,subDivider.height()},themed("resize-submix-handle",QColor("#6d8fac")),{},1.0);
    const auto divider = impl_->groupDividerRect();
    Controls::drawSurface(painter,
        QRectF(divider.center().x() - 1.5, divider.top(), 3.0, divider.height()),
        themed("resize-handle", QColor("#6d8fac")), {}, 1.0);
    const auto aux = impl_->subMixViewport();
    const auto boundaryX = (aux.right() + monitorCard.left()) / 2.0;
    Controls::drawSurface(painter,
        QRectF(boundaryX - 1.5, monitorCard.top(), 3.0, monitorCard.height()),
        themed("category-divider", QColor("#5d7e9d")), {}, 1.0);
    painter.setOpacity(1.0);
    ThemeBackground::paint(painter, rect(), QStringLiteral("mixer"), true);
}

void MixerWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        const auto index = impl_->hitChannel(event->position());
        if (index < 0) return;
        QMenu menu(this);
        auto* remove = menu.addAction(QStringLiteral("删除通道"));
        auto* copy = menu.addAction(QStringLiteral("复制通道"));
        remove->setEnabled(index != impl_->masterIndex()
                           && (index < impl_->inputChannelCount ? impl_->inputChannelCount > 1
                                                               : impl_->auxChannelCount() > 1));
        copy->setEnabled(index != impl_->masterIndex());
        auto* typeMenu = menu.addMenu(QStringLiteral("通道类型"));
        typeMenu->setEnabled(index != impl_->masterIndex());
        const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                   Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
        for (const auto layout : layouts) {
            auto* action = typeMenu->addAction(Common::auxLayoutName(layout));
            action->setCheckable(true);
            action->setChecked(impl_->channels[index].layout == layout);
        }
        menu.addSeparator();
        auto* sel = menu.addAction(QStringLiteral("打开SEL设置"));
        const auto id = impl_->channels[index].id;
        const auto withChannel = [this, id](auto action) {
            const auto found = std::find_if(impl_->channels.begin(), impl_->channels.end(), [id](const Channel& item) {
                return item.id == id;
            });
            if (found != impl_->channels.end())
                action(static_cast<int>(std::distance(impl_->channels.begin(), found)));
        };
        connect(remove, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->removeChannel(current); });
        });
        connect(copy, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->copyChannel(current); });
        });
        for (int layoutIndex = 0; layoutIndex < static_cast<int>(layouts.size()); ++layoutIndex) {
            connect(typeMenu->actions()[layoutIndex], &QAction::triggered, this,
                    [this, withChannel, layout = layouts[layoutIndex]] {
                        withChannel([this, layout](int current) { impl_->setChannelLayout(current, layout); });
                    });
        }
        connect(sel, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->selectChannel(current); impl_->showLargeScreen(); });
        });
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (impl_->groupDividerRect().adjusted(-3, 0, 3, 0).contains(event->position())) {
        impl_->groupDividerDragging = true;
        impl_->setInputRatioFromPosition(event->position().x() - Impl::groupGap / 2.0);
        return;
    }

    if(impl_->subMixDividerRect().adjusted(-3,0,3,0).contains(event->position())) {
        impl_->subMixDividerDragging=true;
        impl_->setSubMixBoundaryFromPosition(event->position().x()-Impl::groupGap/2.0);return;
    }
    for (const auto group : {0,1,2}) {
        const auto thumb = impl_->scrollbarThumb(group).adjusted(-2, -3, 2, 3);
        const auto track = impl_->scrollbarTrack(group).adjusted(0, -3, 0, 3);
        if (thumb.contains(event->position())) {
            impl_->scrollbarDragging = group+1;
            impl_->scrollbarDragOffset = event->position().x() - thumb.left();
            return;
        }
        if (track.contains(event->position())) {
            impl_->setScrollFromThumb(group, event->position().x() - impl_->scrollbarThumb(group).width() / 2.0);
            update();
            return;
        }
    }

    if (impl_->monitorRect().contains(event->position())) {
        const auto track = impl_->monitorFaderTrack().adjusted(-18, -20, 18, 20);
        const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
        if (impl_->monitorSelectRect(heightScale).contains(event->position())) {
            impl_->showMonitorSel();
            return;
        }
        const QRectF readout(impl_->monitorRect().left(), impl_->monitorFaderTrack().bottom()
                             + 10 * heightScale, impl_->monitorRect().width(), 27 * heightScale);
        if (readout.contains(event->position())) {
            Controls::editNumericValue(this, readout.toAlignedRect(), faderDbValue(impl_->monitorFader),
                {-72, 12, 0, 1}, [this](double db) {
                    const auto position = faderPositionForDb(db);
                    if (impl_->model) {
                        auto candidate = *impl_->model;
                        candidate.setMonitorFaderPosition(position);
                        if (!impl_->commitTopology(candidate)) return;
                    }
                    impl_->monitorFader = position;
                    update();
                });
        } else if (track.contains(event->position())) {
            impl_->dragging = -2;
            mouseMoveEvent(event);
        }
        return;
    }

    const auto index = impl_->hitChannel(event->position());
    if (index < 0)
        return;

    const auto card = impl_->channelRect(index);
    const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
    const auto titleTop = card.top();
    if (index != impl_->masterIndex()
        && QRectF(card.left(), titleTop, card.width(), 43.0 * heightScale)
               .contains(event->position())) {
        impl_->reordering = index;
        impl_->reorderTarget = index;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const auto fader=impl_->faderTrack(index);
    const QRectF readout(card.left(),fader.bottom()+10*heightScale,
                         card.width(),27*heightScale);
    if(readout.contains(event->position())) {
        const auto id=impl_->channels[index].id;
        Controls::editNumericValue(this,readout.toAlignedRect(),
            faderDbValue(impl_->channels[index].fader),{-72,12,0,1},
            [this,id](double db) {
                const auto found=std::find_if(impl_->channels.begin(),impl_->channels.end(),
                    [id](const Channel& item) { return item.id==id; });
                if(found==impl_->channels.end()) return;
                found->fader=faderPositionForDb(db);
                impl_->commitChannel(*found); update();
            });
        return;
    }
    if (impl_->meterHitRect(index, heightScale).contains(event->position())) {
        impl_->channels[index].postFaderMeter = !impl_->channels[index].postFaderMeter;
        WJN_LOG_INFO("Mixer meter mode id=%llu mode=%s",
            static_cast<unsigned long long>(impl_->channels[index].modelId),
            impl_->channels[index].postFaderMeter ? "post-fader" : "post-gain");
        update();
        return;
    }
    auto track = impl_->faderTrack(index).adjusted(-card.width() * .18, -20, card.width() * .18, 20);
    track.setTop(std::max(track.top(), impl_->channelButtonRect(index, heightScale,
        index == impl_->masterIndex() ? 1 : 2).bottom() + 4.0 * heightScale));
    if (track.contains(event->position())) {
        impl_->dragging = index;
        mouseMoveEvent(event);
        return;
    }

    auto& channel = impl_->channels[index];
    if (impl_->muteButtonRect(index, heightScale).contains(event->position()))
        channel.mute = !channel.mute;
    else if (index!=impl_->masterIndex()&&impl_->soloButtonRect(index, heightScale).contains(event->position()))
        channel.solo = !channel.solo;
    else if (impl_->selectButtonRect(index, heightScale).contains(event->position())) {
        impl_->selectChannel(index);
        impl_->showLargeScreen();
        return;
    }
    else if (index != impl_->masterIndex() && impl_->mixButtonRect(index, heightScale).contains(event->position()))
        channel.mixToMain = !channel.mixToMain;
    else if (index != impl_->masterIndex()
             && impl_->listenButtonRect(index, heightScale, false).contains(event->position()))
        channel.listenMode = channel.listenMode == Common::MixerListenMode::pfl
            ? Common::MixerListenMode::off : Common::MixerListenMode::pfl;
    else if (index != impl_->masterIndex()
             && impl_->listenButtonRect(index, heightScale, true).contains(event->position()))
        channel.listenMode = channel.listenMode == Common::MixerListenMode::afl
            ? Common::MixerListenMode::off : Common::MixerListenMode::afl;
    impl_->commitChannel(channel);
    update();
}

void MixerWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(event->button()!=Qt::LeftButton) return;
    if (impl_->monitorFaderTrack().adjusted(-18, -20, 18, 20).contains(event->position())) {
        impl_->dragging = -1;
        Controls::resetNumericValue({-72, 12, 0, 1}, [this](double db) {
            const auto position = faderPositionForDb(db);
            if (impl_->model) {
                auto candidate = *impl_->model;
                candidate.setMonitorFaderPosition(position);
                if (!impl_->commitTopology(candidate)) return;
            }
            impl_->monitorFader = position;
            update();
        });
        return;
    }
    const int index=impl_->hitChannel(event->position());
    if(index<0) return;
    const auto card=impl_->channelRect(index);
    const auto heightScale=std::clamp(height()/768.0,.72,1.35);
    auto track=impl_->faderTrack(index).adjusted(-card.width()*.18,-20,
                                               card.width()*.18,20);
    track.setTop(std::max(track.top(),impl_->channelButtonRect(index,heightScale,
                          index == impl_->masterIndex() ? 1 : 2).bottom()
                          +4.0*heightScale));
    if(!track.contains(event->position())) return;
    impl_->dragging=-1;
    Controls::resetNumericValue({-72,12,0,1},[this,index](double db) {
        auto& channel=impl_->channels[index];
        channel.fader=faderPositionForDb(db);
        impl_->commitChannel(channel); update();
    });
}

void MixerWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (impl_->reordering >= 0 && (event->buttons() & Qt::LeftButton)) {
        const auto from = impl_->reordering;
        const auto first = from < impl_->inputChannelCount ? 0 : impl_->channels[from].subMix ? impl_->subMixStart() : impl_->inputChannelCount;
        const auto end = from < impl_->inputChannelCount
            ? impl_->inputChannelCount : impl_->channels[from].subMix ? impl_->masterIndex() : impl_->subMixStart();
        const auto viewport = impl_->viewportForChannel(from);
        if (viewport.contains(event->position())) {
            int target = first;
            for (int index = first; index < end; ++index) {
                if (event->position().x() >= impl_->channelRect(index).center().x()) target = index;
            }
            if (impl_->reorderTarget != target) {
                impl_->reorderTarget = target;
                update();
            }
        }
        return;
    }
    if(impl_->subMixDividerDragging&&(event->buttons()&Qt::LeftButton)) {
        impl_->setSubMixBoundaryFromPosition(event->position().x()-Impl::groupGap/2.0);update();return;
    }
    if (impl_->groupDividerDragging && (event->buttons() & Qt::LeftButton)) {
        impl_->setInputRatioFromPosition(event->position().x() - Impl::groupGap / 2.0);
        update();
        return;
    }
    if (impl_->scrollbarDragging != 0 && (event->buttons() & Qt::LeftButton)) {
        const auto group = impl_->scrollbarDragging-1;
        impl_->setScrollFromThumb(group, event->position().x() - impl_->scrollbarDragOffset);
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        if (impl_->groupDividerRect().adjusted(-3, 0, 3, 0).contains(event->position())
            || impl_->subMixDividerRect().adjusted(-3,0,3,0).contains(event->position())) {
            setCursor(Qt::SplitHCursor);
        } else {
            const auto index = impl_->hitChannel(event->position());
            const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
            setCursor(index >= 0 && impl_->meterHitRect(index, heightScale).contains(event->position())
                ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        return;
    }
    if (impl_->dragging == -2) {
        const auto track = impl_->monitorFaderTrack();
        const auto position = std::clamp((event->position().y() - track.top()) / track.height(), 0.0, 1.0);
        if (impl_->model) {
            auto candidate = *impl_->model;
            candidate.setMonitorFaderPosition(position);
            if (!impl_->commitTopology(candidate)) return;
        }
        impl_->monitorFader = position;
        update();
        return;
    }
    if (impl_->dragging < 0)
        return;
    const auto track = impl_->faderTrack(impl_->dragging);
    impl_->channels[impl_->dragging].fader = std::clamp((event->position().y() - track.top()) / track.height(), 0.0, 1.0);
    impl_->commitChannel(impl_->channels[impl_->dragging]);
}

void MixerWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (impl_->reordering >= 0 && event->button() == Qt::LeftButton) {
        const auto from = impl_->reordering;
        const auto to = impl_->reorderTarget;
        impl_->reordering = -1;
        impl_->reorderTarget = -1;
        if (to >= 0) impl_->moveChannel(from, to);
        unsetCursor();
        update();
    }
    impl_->dragging = -1;
    impl_->scrollbarDragging = 0;
    impl_->groupDividerDragging = false;
    impl_->subMixDividerDragging = false;
}

void MixerWindow::wheelEvent(QWheelEvent* event)
{
    const auto input = impl_->inputViewport().contains(event->position());
    const auto aux = impl_->auxViewport().contains(event->position());
    const auto sub=impl_->subMixViewport().contains(event->position());
    if (!input && !aux && !sub) {
        QWidget::wheelEvent(event);
        return;
    }

    const auto angle = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
    if (input)
        impl_->inputScrollOffset = std::clamp(impl_->effectiveInputScrollOffset() - angle / 120.0 * 54.0,
                                             0.0, impl_->maximumInputScrollOffset());
    else if(aux)
        impl_->auxScrollOffset = std::clamp(impl_->effectiveAuxScrollOffset() - angle / 120.0 * 54.0,
                                           0.0, impl_->maximumAuxScrollOffset());
    if(sub)impl_->subMixScrollOffset=std::clamp(impl_->effectiveSubMixScrollOffset()-angle/120.0*54.0,0.0,impl_->maximumSubMixScrollOffset());
    update();
    event->accept();
}

}
