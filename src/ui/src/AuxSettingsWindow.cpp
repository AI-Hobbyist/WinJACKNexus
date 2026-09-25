#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {

using Common::AuxBusState;
using Common::AuxChannelLayout;

constexpr double pi = 3.14159265358979323846;

QColor alpha(QColor color, int value)
{
    color.setAlpha(value);
    return color;
}

void text(QPainter& painter, const QRectF& rect, const QString& value, const QColor& color,
          qreal size, int alignment = Qt::AlignCenter, bool bold = false)
{
    auto font = painter.font();
    font.setPixelSize(std::max(8, qRound(size)));
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, alignment, value);
}

void panel(QPainter& painter, const QRectF& rect, const QColor& border = QColor("#183a56"))
{
    painter.setPen(QPen(border, 1));
    painter.setBrush(QColor("#102235"));
    painter.drawRoundedRect(rect, 7, 7);
}

void toggle(QPainter& painter, const QRectF& rect, bool checked)
{
    const auto margin = rect.height() / 12.0;
    const auto thumbSize = rect.height() - 2.0 * margin;
    painter.setPen(Qt::NoPen);
    painter.setBrush(checked ? QColor("#187ee8") : QColor("#38516d"));
    painter.drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QRectF(checked ? rect.right() - margin - thumbSize : rect.left() + margin,
                               rect.top() + margin, thumbSize, thumbSize));
}

QString dbText(double value)
{
    if (value < -72.0)
        return QStringLiteral("−∞ dB");
    return QStringLiteral("%1%2 dB").arg(value > 0.0 ? QStringLiteral("+") : QString(),
                                           QString::number(value, 'f', 1));
}

double positionFromDb(double value)
{
    return std::clamp((12.0 - value) / 84.0, 0.0, 1.0);
}

QString channelNames(AuxChannelLayout layout)
{
    switch(layout){
    case AuxChannelLayout::mono: return QStringLiteral("单声道");
    case AuxChannelLayout::stereo: return QStringLiteral("立体声");
    case AuxChannelLayout::surround51: return QStringLiteral("5.1");
    case AuxChannelLayout::surround71: return QStringLiteral("7.1");
    }
    return {};
}

QString mappingLabel(const QString& sourceNumber, const AuxBusState& bus)
{
    return QStringLiteral("%1 (%2) → AUX %3 (%4)")
        .arg(sourceNumber,channelNames(bus.sourceLayout),QString::number(bus.id),
             channelNames(bus.targetLayout));
}

double dbFromPosition(double position)
{
    return 12.0 - std::clamp(position, 0.0, 1.0) * 84.0;
}

}

class AuxSettingsWindow::Impl {
public:
    explicit Impl(AuxSettingsWindow& owner, std::shared_ptr<Common::AuxMixerState> state)
        : owner(owner), state(std::move(state))
    {
        meterTimer.setInterval(70);
        QObject::connect(&meterTimer, &QTimer::timeout, &owner, [this] {
            for (auto& bus : buses()) {
                for (auto& send : bus.sends) {
                    const auto base = send.levelDb < -72.0 ? -72.0 : send.levelDb - 3.0;
                    const auto jitter = (QRandomGenerator::global()->bounded(61) - 30) / 10.0;
                    send.meterDb += (std::clamp(base + jitter, -72.0, 12.0) - send.meterDb) * .25;
                }
            }
            this->owner.update();
        });
        meterTimer.start();
    }

    std::vector<AuxBusState>& buses() const
    {
        return state->busesForSource(sourceKey, selfAuxId);
    }

    AuxBusState* selected() const
    {
        return state->busForSource(sourceKey, selectedId, selfAuxId);
    }

    qreal contentScale() const
    {
        return std::min(owner.width() / 1360.0, owner.height() / 813.0);
    }

    qreal scaleX() const { return contentScale(); }
    qreal scaleY() const { return contentScale(); }
    QPointF contentOrigin() const
    {
        const auto scale = contentScale();
        return { (owner.width() - 1360.0 * scale) * .5,
                 (owner.height() - 813.0 * scale) * .5 };
    }

    QRectF scaled(qreal x, qreal y, qreal width, qreal height) const
    {
        const auto scale = contentScale();
        const auto origin = contentOrigin();
        return { origin.x() + x * scale, origin.y() + y * scale,
                 width * scale, height * scale };
    }

    QRectF leftPanel() const { return scaled(5, 11, 390, 790); }
    QRectF addButton() const { return scaled(248, 12, 102, 38); }
    QRectF masterToggle() const { return scaled(202, 18, 40, 24); }
    QRectF removeButton() const { return scaled(356, 12, 31, 38); }
    QRectF listViewport() const { return scaled(10, 58, 380, 735); }
    QRectF listScrollbarTrack() const { return scaled(376, 62, 8, 727); }
    QRectF topBar() const { return scaled(406, 11, 559, 544); }
    QRectF pannerPanel() const { return scaled(420, 95, 531, 448); }
    QRectF parametersPanel() const { return scaled(406, 566, 559, 235); }
    QRectF sendsPanel() const { return scaled(976, 11, 359, 790); }
    QRectF resetButton() const { return scaled(1220, 52, 100, 34); }
    QRectF sendsLinkButton() const { return scaled(1085, 57, 42, 24); }
    QRectF directivityButton() const { return scaled(755, 56, 42, 24); }
    QRectF spatialModeButton() const { return scaled(420, 52, 137, 34); }
    QRectF classicModeButton() const { return scaled(567, 52, 137, 34); }
    QRectF parameterKnob(int index) const { return scaled(468+index*178, 656, 82, 82); }
    QRectF classicPanKnob() const { return scaled(511, 144, 350, 350); }

    QRectF listRow(int index) const
    {
        return scaled(14, 62 + index * 101 - listScroll, 358, 95);
    }

    QRectF listPowerToggle(int index) const
    {
        const auto row = listRow(index);
        return { row.left() + 14 * scaleX(), row.top() + 24 * scaleY(),
                 40 * scaleX(), 24 * scaleY() };
    }

    QRectF listGear(int index) const
    {
        const auto row = listRow(index);
        return { row.right() - 44 * scaleX(), row.top() + 18 * scaleY(),
                 34 * scaleX(), 36 * scaleY() };
    }

    qreal maximumListScroll() const
    {
        return std::max(0.0, buses().size() * 101.0 - 735.0);
    }

    QRectF listScrollbarThumb() const
    {
        const auto track = listScrollbarTrack();
        const auto contentHeight = std::max(735.0, buses().size() * 79.0);
        const auto thumbHeight = std::max(44.0 * scaleY(), track.height() * 735.0 / contentHeight);
        const auto travel = std::max(0.0, track.height() - thumbHeight);
        const auto maximum = maximumListScroll();
        const auto normalized = maximum <= 0.0 ? 0.0 : std::clamp(listScroll / maximum, 0.0, 1.0);
        return { track.left(), track.top() + travel * normalized, track.width(), thumbHeight };
    }

    void setListScrollFromThumb(qreal thumbTop)
    {
        const auto track = listScrollbarTrack();
        const auto thumb = listScrollbarThumb();
        const auto travel = std::max(0.0, track.height() - thumb.height());
        const auto normalized = travel <= 0.0 ? 0.0
            : std::clamp((thumbTop - track.top()) / travel, 0.0, 1.0);
        listScroll = normalized * maximumListScroll();
    }

    QRectF listKnob(int index) const
    {
        const auto row = listRow(index);
        return { row.left() + 156 * scaleX(), row.top() + 16 * scaleY(),
                 40 * scaleX(), 40 * scaleY() };
    }

    QRectF pannerCircle() const
    {
        const auto area = pannerPanel().adjusted(65 * scaleX(), 34 * scaleY(), -65 * scaleX(), -34 * scaleY());
        const auto side = std::min(area.width(), area.height());
        return { area.center().x() - side / 2.0, area.center().y() - side / 2.0, side, side };
    }

    QPointF pointToScreen(const QPointF& point) const
    {
        const auto circle = pannerCircle();
        const auto radius=159.0*contentScale();
        return circle.center() + point*radius;
    }

    QPointF screenToPoint(const QPointF& point) const
    {
        const auto circle = pannerCircle();
        const auto radius=159.0*contentScale();
        return Common::auxClampPannerPoint((point-circle.center())/radius);
    }

    QRectF sendStrip(int index) const
    {
        return scaled(992, 100+index*86, 327, 80);
    }

    int effectiveSelectedPoint() const
    {
        const auto* bus = selected();
        if (bus == nullptr || bus->adjustablePoints.empty())
            return -1;
        const int index=std::clamp(selectedPoint, 0, static_cast<int>(bus->adjustablePoints.size()) - 1);
        return Common::auxHasSpatialPoint(bus->sourceLayout,index) ? index : 0;
    }

    double pointInfluence(const AuxBusState& bus, int index) const
    {
        return index >= 0 && index < static_cast<int>(bus.pointInfluenceRadii.size())
            ? bus.pointInfluenceRadii[index] : 0.1;
    }

    qreal pointInfluencePixelRadius(const AuxBusState& bus, int index) const
    {
        return Common::auxInfluencePixelRadius(pointInfluence(bus,index))*contentScale();
    }

    void setPointInfluence(AuxBusState& bus, int index, double radius)
    {
        Common::auxSetPointInfluenceRadius(bus,index,radius);
    }

    void setLinkedWidth(AuxBusState& bus, double width)
    {
        bus.width = std::clamp(width, 0.0, 1.0);
    }

    void setAngle(AuxBusState& bus, double angle)
    {
        Common::auxSetPannerAngle(bus,angle);
    }

    void changed()
    {
        publishPan();
        if (stateChanged)
            stateChanged();
        owner.update();
    }

    void publishPan()
    {
        if(const auto* bus=selected()){
            owner.setProperty("auxPanMode",static_cast<int>(bus->panMode));
            owner.setProperty("auxClassicPan",bus->classicPan);
            owner.setProperty("auxPannerWidth",bus->width);
            const auto index=effectiveSelectedPoint();
            if(index>=0)owner.setProperty("auxPannerInfluence",pointInfluence(*bus,index));
        }
    }

    AuxSettingsWindow& owner;
    std::shared_ptr<Common::AuxMixerState> state;
    std::function<void()> stateChanged;
    std::function<void(bool)> chainEnabledHandler;
    bool chainEnabled {};
    bool threeD {};
    Common::AuxChannelLayout sourceLayout { Common::AuxChannelLayout::stereo };
    QTimer meterTimer;
    QString sourceKey;
    QString sourceName;
    QString sourceNumber;
    std::uint32_t selfAuxId {};
    std::uint32_t selectedId {};
    qreal listScroll {};
    bool listScrollbarDragging {};
    qreal listScrollbarDragOffset {};
    int selectedPoint {};
    int draggedPoint { -1 };
    int draggedInfluencePoint { -1 };
    int draggedSend { -1 };
    std::vector<double> draggedSendLevels;
    std::uint32_t draggedListBusId {};
    std::vector<double> draggedListLevels;
    int draggedParameter { -1 };
    bool draggedClassicPan {};
    QPointF dragStart;
    double dragStartValue {};
};

AuxSettingsWindow::AuxSettingsWindow(std::shared_ptr<Common::AuxMixerState> state, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , impl_(std::make_unique<Impl>(*this, std::move(state)))
{
    setWindowTitle(QStringLiteral("WinJACKNexus - AUX 设置"));
    setMinimumSize(980, 640);
    resize(1360, 813);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

AuxSettingsWindow::~AuxSettingsWindow() = default;

void AuxSettingsWindow::setSource(const QString& sourceKey, const QString& sourceName,
                                  std::uint32_t selfAuxId, Common::AuxChannelLayout layout,
                                  const QString& sourceNumber)
{
    impl_->sourceKey = sourceKey;
    impl_->sourceName = sourceName;
    impl_->sourceNumber = sourceNumber.isEmpty() ? sourceName : sourceNumber;
    impl_->selfAuxId = selfAuxId;
    impl_->sourceLayout = layout;
    const auto& buses = impl_->buses();
    for (const auto& bus : buses)
        impl_->state->setLayouts(sourceKey, bus.id, layout, bus.targetLayout, selfAuxId);
    impl_->selectedId = buses.empty() ? 0 : buses.front().id;
    impl_->selectedPoint = 0;
    impl_->publishPan();
    setWindowTitle(QStringLiteral("WinJACKNexus - %1 AUX 设置").arg(sourceName));
    update();
}

void AuxSettingsWindow::selectBus(std::uint32_t id)
{
    if (impl_->state->busForSource(impl_->sourceKey, id, impl_->selfAuxId) != nullptr) {
        impl_->selectedId = id;
        impl_->selectedPoint = 0;
        impl_->publishPan();
    }
    update();
}

void AuxSettingsWindow::setStateChangedCallback(std::function<void()> callback)
{
    impl_->stateChanged = std::move(callback);
}

void AuxSettingsWindow::setChainEnabled(bool enabled)
{
    impl_->chainEnabled = enabled;
    update();
}

bool AuxSettingsWindow::chainEnabled() const
{
    return impl_->chainEnabled;
}

void AuxSettingsWindow::setChainEnabledHandler(std::function<void(bool)> handler)
{
    impl_->chainEnabledHandler = std::move(handler);
}

void AuxSettingsWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#0b1a2b"));
    const auto sx = impl_->scaleX();
    const auto sy = impl_->scaleY();
    const auto label = QColor("#e8f2ff");
    const auto secondary = QColor("#aec7eb");
    const auto cyan = QColor("#26bfff");
    const auto blue = QColor("#168dff");

    panel(painter, impl_->leftPanel(), QColor("#2b4662"));
    text(painter, impl_->scaled(26, 12, 172, 38), impl_->sourceName + QStringLiteral(" · AUX"), label, 18 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
    toggle(painter, impl_->masterToggle(), impl_->chainEnabled);
    painter.setPen(QPen(QColor("#0b699d"), 1));
    painter.setBrush(QColor("#073158"));
    painter.drawRoundedRect(impl_->addButton(), 6, 6);
    text(painter, impl_->addButton(), QStringLiteral("＋  添加 AUX"), QColor("#54d8ff"), 15 * sy);
    painter.setBrush(QColor("#172d42"));
    painter.drawRoundedRect(impl_->removeButton(), 6, 6);
    text(painter, impl_->removeButton(), QStringLiteral("−"), label, 21 * sy, Qt::AlignCenter, true);

    painter.save();
    painter.setClipRect(impl_->listViewport());
    for (int index = 0; index < static_cast<int>(impl_->buses().size()); ++index) {
        const auto& bus = impl_->buses()[index];
        const auto row = impl_->listRow(index);
        const auto selected = bus.id == impl_->selectedId;
        const QColor busAccent=bus.accent.isValid()?bus.accent:QColor("#20d9ff");
        painter.setPen(QPen(selected ? busAccent : (bus.accent.isValid()?busAccent.darker(180):QColor("#17374f")), selected ? 1.5 : 1.0));
        painter.setBrush(selected ? QColor("#0d3048") : QColor("#0a1d2c"));
        painter.drawRoundedRect(row, 7, 7);
        if (selected)
            painter.fillRect(QRectF(row.left(), row.top() + 3, 5 * sx, row.height() - 6), busAccent);

        toggle(painter, impl_->listPowerToggle(index), impl_->chainEnabled && bus.enabled && !bus.selfRoute);
        text(painter, { row.left() + 64 * sx, row.top() + 10 * sy, 94 * sx, 25 * sy },
             QStringLiteral("AUX %1").arg(bus.id), label, 15 * sy, Qt::AlignLeft | Qt::AlignVCenter, true);
        text(painter, { row.left() + 64 * sx, row.top() + 36 * sy, 94 * sx, 22 * sy }, bus.name,
             QColor("#91b9e8"), 13 * sy, Qt::AlignLeft | Qt::AlignVCenter);

        const auto knob = impl_->listKnob(index);
        const auto knobCenter = knob.center();
        const auto level = bus.sends.empty() ? -72.0 : bus.sends.front().levelDb;
        const auto normalized = std::clamp((level + 72.0) / 84.0, 0.0, 1.0);
        const QRectF arc = knob.adjusted(-3 * sx, -3 * sy, 3 * sx, 3 * sy);
        painter.setPen(QPen(QColor("#254762"), 5 * sx, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(arc, 225 * 16, -270 * 16);
        if (!bus.selfRoute) {
            painter.setPen(QPen(blue, 5 * sx, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(arc, 225 * 16, qRound(-270.0 * normalized * 16.0));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(bus.selfRoute ? QColor("#385064") : QColor("#1b3b57"));
        painter.drawEllipse(knobCenter, 18 * sx, 18 * sy);
        const auto indicatorAngle = (135.0 + 270.0 * normalized) * pi / 180.0;
        const auto direction = QPointF(std::cos(indicatorAngle) * sx, std::sin(indicatorAngle) * sy);
        painter.setPen(QPen(bus.selfRoute ? secondary : QColor("#d9f4ff"), 3 * sx,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(knobCenter + direction * 8, knobCenter + direction * 17);
        text(painter, { knobCenter.x() - 38 * sx, row.top() + 55 * sy, 76 * sx, 16 * sy },
             bus.selfRoute ? QStringLiteral("自身 禁用") : dbText(level),
             bus.selfRoute ? secondary : cyan, 10 * sy);

        const QRectF pre(row.left() + 220 * sx, row.top() + 18 * sy, 45 * sx, 36 * sy);
        const QRectF post(row.left() + 265 * sx, row.top() + 18 * sy, 48 * sx, 36 * sy);
        painter.setPen(QPen(QColor("#315370"), 1));
        painter.setBrush(bus.preFader ? QColor("#087ee4") : QColor("#172d43"));
        painter.drawRoundedRect(pre, 5, 5);
        painter.setBrush(!bus.preFader ? QColor("#087ee4") : QColor("#172d43"));
        painter.drawRoundedRect(post, 5, 5);
        text(painter, pre, QStringLiteral("Pre"), bus.preFader ? QColor("#ffffff") : secondary, 12 * sy);
        text(painter, post, QStringLiteral("Post"), !bus.preFader ? QColor("#ffffff") : secondary, 12 * sy);
        const auto gear = impl_->listGear(index);
        painter.setBrush(QColor("#142b3e"));
        painter.drawRoundedRect(gear, 5, 5);
        text(painter, gear, QStringLiteral("⚙"), label, 18 * sy);
        painter.setPen(QPen(QColor("#24445d"),1));
        painter.drawLine(QPointF(row.left()+12*sx,row.top()+72*sy),
                         QPointF(row.right()-12*sx,row.top()+72*sy));
        text(painter,{row.left()+16*sx,row.top()+75*sy,row.width()-32*sx,17*sy},
             mappingLabel(impl_->sourceNumber,bus),QColor("#a8c8e7"),12*sy,
             Qt::AlignCenter);
    }
    painter.restore();

    const auto scrollbarTrack = impl_->listScrollbarTrack();
    const auto scrollbarThumb = impl_->listScrollbarThumb();
    const auto listScrollable = impl_->maximumListScroll() > 0.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#10283a"));
    painter.drawRoundedRect(scrollbarTrack, scrollbarTrack.width() * .5, scrollbarTrack.width() * .5);
    painter.setBrush(!listScrollable ? QColor("#29465b")
        : impl_->listScrollbarDragging ? QColor("#56d7ff") : QColor("#258fc3"));
    painter.drawRoundedRect(scrollbarThumb, scrollbarThumb.width() * .5, scrollbarThumb.width() * .5);

    auto* bus = impl_->selected();
    if (bus == nullptr)
        return;

    painter.save();
    painter.translate(impl_->contentOrigin());
    painter.scale(sx, sy);
    const auto drawPanel=[&](QRectF rect){
        painter.setPen(QColor("#2b4662"));painter.setBrush(QColor("#102235"));painter.drawRoundedRect(rect,7,7);
    };
    const auto drawText=[&](QRectF rect,const QString& value,int size=15,QColor color=QColor("#e8f2ff"),bool bold=false,int align=Qt::AlignLeft|Qt::AlignVCenter){
        auto font=painter.font();font.setPixelSize(size);font.setBold(bold);painter.setFont(font);painter.setPen(color);painter.drawText(rect,align,value);
    };
    const auto drawButton=[&](QRectF rect,const QString& value,bool active=false){
        painter.setPen(active?cyan:QColor("#45617e"));painter.setBrush(active?QColor("#123e61"):QColor("#192d43"));painter.drawRoundedRect(rect,5,5);
        drawText(rect,value,14,active?QColor("#d7f7ff"):secondary,false,Qt::AlignCenter);
    };
    const auto drawToggle=[&](QRectF rect,bool active){
        painter.setPen(Qt::NoPen);painter.setBrush(active?QColor("#168dff"):QColor("#263e53"));painter.drawRoundedRect(rect,rect.height()/2,rect.height()/2);
        painter.setBrush(Qt::white);painter.drawEllipse(QPointF(active?rect.right()-rect.height()/2:rect.left()+rect.height()/2,rect.center().y()),rect.height()*.34,rect.height()*.34);
    };
    const auto drawKnob=[&](QPointF center,qreal radius,qreal fraction){
        fraction=std::clamp(fraction,0.0,1.0);
        const qreal stroke=radius>=80?10:5;
        painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor("#254762"),stroke,Qt::SolidLine,Qt::RoundCap));
        painter.drawArc(QRectF(center.x()-radius,center.y()-radius,2*radius,2*radius),225*16,-270*16);
        painter.setPen(QPen(cyan,stroke,Qt::SolidLine,Qt::RoundCap));
        painter.drawArc(QRectF(center.x()-radius,center.y()-radius,2*radius,2*radius),225*16,qRound(-270*16*fraction));
        painter.setPen(Qt::NoPen);painter.setBrush(QColor("#1b3b57"));painter.drawEllipse(center,radius>=80?radius*.78:radius-6,radius>=80?radius*.78:radius-6);
        const qreal angle=(135+270*fraction)*pi/180.0;
        painter.setPen(QPen(QColor("#d9f4ff"),3,Qt::SolidLine,Qt::RoundCap));
        painter.drawLine(center+QPointF(std::cos(angle),std::sin(angle))*(radius*.4),center+QPointF(std::cos(angle),std::sin(angle))*(radius-7));
    };
    const auto drawMuteSolo=[&](QRectF rect,const QString& value,bool active,QColor accent){
        painter.setPen(accent);QColor fill=accent;fill.setAlpha(active?85:25);painter.setBrush(fill);painter.drawRoundedRect(rect,5,5);
        drawText(rect,value,15,QColor("#f7f8ff"),true,Qt::AlignCenter);
    };
    const bool canClassic=Common::auxSupportsClassicPan(bus->sourceLayout,bus->targetLayout);
    const bool classic=canClassic&&bus->panMode==Common::AuxPanMode::classic;
    drawPanel({406,11,559,544});drawPanel({406,566,559,235});drawPanel({976,11,359,790});
    drawText({420,20,250,29},QStringLiteral("空间声像器"),17,label,true);
    drawText({784,20,165,29},QStringLiteral("AUX %1 · %2").arg(bus->id).arg(bus->name),12,secondary,false,Qt::AlignRight|Qt::AlignVCenter);
    drawButton({420,52,137,34},QStringLiteral("空间声像器"),!classic);
    painter.save();if(!canClassic)painter.setOpacity(.32);
    drawButton({567,52,137,34},QStringLiteral("经典 Pan"),classic);
    painter.restore();
    if(!classic){drawText({711,52,100,34},QStringLiteral("显示分布图"),13,secondary);drawToggle({814,56,42,24},bus->showDirectivity);}
    drawPanel({420,95,531,448});
    const QPointF center(686,319);
    if(classic){
        Controls::Palette panColors;
        panColors.border=QColor("#254762");panColors.accent=cyan;
        panColors.panel=QColor("#1b3b57");panColors.text=QColor("#d9f4ff");
        Controls::drawPanKnob(painter,center,bus->classicPan,170,panColors);
        drawText({center.x()-110,105,220,35},QStringLiteral("声像"),19,label,true,Qt::AlignCenter);
        drawText({center.x()-190,505,90,25},QStringLiteral("L100%"),15,secondary,true,Qt::AlignCenter);
        drawText({center.x()-20,505,40,25},QStringLiteral("C"),17,secondary,true,Qt::AlignCenter);
        drawText({center.x()+100,505,90,25},QStringLiteral("R100%"),15,secondary,true,Qt::AlignCenter);
        const auto direction=bus->classicPan<0?QStringLiteral("L"):bus->classicPan>0?QStringLiteral("R"):QStringLiteral("C");
        drawText({center.x()-110,339,220,30},QStringLiteral("%1 %2%").arg(direction).arg(qRound(std::abs(bus->classicPan)*100)),16,label,true,Qt::AlignCenter);
    }else{
        constexpr qreal radius=190,pointRadius=159;
        painter.setBrush(Qt::NoBrush);
        for(int ring=1;ring<=5;++ring){painter.setPen(QPen(QColor("#244a66"),1));painter.drawEllipse(center,radius*ring/5,radius*ring/5);}
        for(int angle=0;angle<360;angle+=30){const qreal radians=angle*pi/180.0;painter.setPen(QPen(angle%90==0?QColor("#426884"):QColor("#1b3d56"),1));painter.drawLine(center,center+QPointF(std::sin(radians),-std::cos(radians))*radius);}
        const auto selectedPoint=impl_->effectiveSelectedPoint();
        const auto outputGains=selectedPoint>=0
            ? Common::auxSpatialOutputGains(*bus,selectedPoint):std::vector<double>{};
        const auto references=Common::auxLayoutReferencePoints(bus->targetLayout);
        for(int i=0;i<static_cast<int>(references.size());++i){
            if(!Common::auxHasSpatialPoint(bus->targetLayout,i))continue;
            const auto gain=bus->showDirectivity&&i<static_cast<int>(outputGains.size())
                ? outputGains[i]:0.0;
            painter.setPen(QPen(QColor(138,169,214,110+qRound(gain*145)),2));
            painter.setBrush(QColor(109,141,184,75+qRound(gain*165)));
            painter.drawEllipse(center+references[i]*pointRadius,10+gain*3,10+gain*3);
        }
        const auto labels=Common::auxLayoutChannelLabels(bus->sourceLayout);
        for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
            if(!Common::auxHasSpatialPoint(bus->sourceLayout,i))continue;
            const auto point=center+bus->adjustablePoints[i]*pointRadius;
            const bool selected=i==selectedPoint;
            const auto outerRadius=Common::auxInfluencePixelRadius(impl_->pointInfluence(*bus,i));
            painter.setPen(QPen(selected?QColor("#79e3ff"):QColor(85,189,233,115),selected?2.5:1.2));
            painter.setBrush(QColor(22,141,255,selected?28:9));painter.drawEllipse(point,outerRadius,outerRadius);
            if(selected){painter.setPen(Qt::NoPen);painter.setBrush(QColor("#79e3ff"));
                painter.drawEllipse(point+QPointF(outerRadius,0),4,4);}
            if(selected){painter.setPen(QPen(QColor("#7ee8ff"),5));painter.setBrush(Qt::NoBrush);painter.drawEllipse(point,14,14);}
            painter.setPen(QPen(selected?QColor("#e8fbff"):QColor("#9dc3ff"),selected?3:2));painter.setBrush(selected?QColor("#42a8ff"):QColor("#2e9dff"));painter.drawEllipse(point,10,10);
            if(i<labels.size())drawText({point.x()-17,point.y()+14,34,19},labels[i],12,label,true,Qt::AlignCenter);
        }
        painter.setPen(Qt::NoPen);painter.setBrush(Qt::white);painter.drawEllipse(center,4,4);
        drawText({center.x()-22,105,44,20},QStringLiteral("90°"),12,secondary,false,Qt::AlignCenter);
        drawText({center.x()+radius+8,309,45,20},QStringLiteral("0°"),12,secondary);
        drawText({center.x()-radius-53,309,45,20},QStringLiteral("180°"),12,secondary);
        drawText({center.x()-24,516,48,20},QStringLiteral("270°"),12,secondary,false,Qt::AlignCenter);
    }
    drawText({420,574,300,27},QStringLiteral("空间声像参数"),17,label,true);
    painter.save();if(classic)painter.setOpacity(.32);
    for(int i=0;i<3;++i){
        const qreal x=420+i*178;
        drawPanel({x,609,168,178});
        drawText({x+10,616,148,25},i==0?QStringLiteral("宽度"):i==1?QStringLiteral("前后"):QStringLiteral("角度"),15,label,true,Qt::AlignCenter);
        const auto selectedPoint=impl_->effectiveSelectedPoint();
        const qreal fraction=i==0?bus->width:i==1&&selectedPoint>=0?(bus->adjustablePoints[selectedPoint].y()+1)/2:(bus->angleDegrees+180)/360;
        drawKnob({x+84,694},35,fraction);
        const QString value=i==0?QStringLiteral("%1%").arg(qRound(bus->width*100)):i==1&&selectedPoint>=0?QStringLiteral("%1%").arg(qRound(bus->adjustablePoints[selectedPoint].y()*100)):QStringLiteral("%1°").arg(qRound(bus->angleDegrees));
        drawText({x+10,748,148,26},value,15,label,true,Qt::AlignCenter);
    }
    painter.restore();
    drawText({992,20,135,29},QStringLiteral("分控旋钮"),17,label,true);
    drawText({992,54,90,28},QStringLiteral("全通道链接"),13,secondary);drawToggle({1085,57,42,24},bus->sendsLinked);
    drawButton({1220,52,100,34},QStringLiteral("重置"));
    const std::array<QString,8> sendLabels{QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
    for(int i=0;i<8;++i){
        painter.save();if(i>=static_cast<int>(bus->sends.size()))painter.setOpacity(.32);
        const qreal x=992,y=100+i*86;
        drawPanel({x,y,327,80});
        drawText({x+7,y+23,30,28},i==0&&bus->sourceLayout==AuxChannelLayout::mono
                     ? QStringLiteral("M") : sendLabels[i],i==3?12:15,label,true,Qt::AlignCenter);
        if(i==3&&i<static_cast<int>(bus->sends.size()))
            drawText({x+3,y+48,38,16},QStringLiteral("强度"),11,secondary,false,Qt::AlignCenter);
        const auto* send=i<static_cast<int>(bus->sends.size())?&bus->sends[i]:nullptr;
        const qreal level=send?send->levelDb:-2.0,meterDb=send?send->meterDb:-72.0;
        drawKnob({x+70,y+32},23,(level+72)/84.0);
        drawText({x+35,y+57,72,18},QStringLiteral("%1 dB").arg(level,0,'f',1),11,label,true,Qt::AlignCenter);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor("#06131f"));painter.drawRoundedRect(QRectF(x+112,y+25,200,13),3,3);
        const qreal lit=200*std::clamp((meterDb+72)/84.0,0.0,1.0);
        const auto segment=[&](qreal low,qreal high,QColor color){const qreal left=200*(low+72)/84.0,right=200*(high+72)/84.0,width=std::min(right,lit)-left;if(width>0){painter.setBrush(color);painter.drawRect(QRectF(x+112+left,y+27,width,9));}};
        segment(-72,-12,QColor("#17d675"));segment(-12,0,QColor("#f2df48"));segment(0,12,QColor("#f2484d"));
        for(const int mark:{-72,-48,-24,-12,0,12}){const qreal tick=x+112+200*(mark+72)/84.0;painter.setPen(QPen(QColor("#6685a2"),1));painter.drawLine(QPointF(tick,y+20),QPointF(tick,y+23));drawText({tick-15,y+3,30,16},mark==12?QStringLiteral("+12"):QString::number(mark),9,secondary,false,Qt::AlignCenter);}
        drawMuteSolo({x+174,y+45,62,29},QStringLiteral("M"),send&&send->mute,QColor("#ff4b62"));
        drawMuteSolo({x+244,y+45,62,29},QStringLiteral("S"),send&&send->solo,QColor("#ffae2d"));
        painter.restore();
    }
    painter.restore();
}

void AuxSettingsWindow::mousePressEvent(QMouseEvent* event)
{
    const auto position = event->position();
    if (impl_->masterToggle().contains(position)) {
        impl_->chainEnabled = !impl_->chainEnabled;
        if (impl_->chainEnabledHandler) impl_->chainEnabledHandler(impl_->chainEnabled);
        update();
        return;
    }
    if (impl_->addButton().contains(position)) {
        QMenu menu(this);
        const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                   Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
        for (const auto layout : layouts) {
            connect(menu.addAction(Common::auxLayoutName(layout)), &QAction::triggered, this, [this, layout] {
                impl_->selectedId = impl_->state->addBus(layout).id;
                impl_->selectedPoint = 0;
                impl_->listScroll = impl_->maximumListScroll();
                impl_->changed();
            });
        }
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (impl_->removeButton().contains(position)) {
        const auto previous = impl_->selectedId;
        if (impl_->state->removeBus(previous)) {
            impl_->selectedId = impl_->buses().front().id;
            impl_->listScroll = std::clamp(impl_->listScroll, 0.0, impl_->maximumListScroll());
            impl_->changed();
        }
        return;
    }
    if (impl_->maximumListScroll() > 0.0) {
        const auto thumb = impl_->listScrollbarThumb().adjusted(-3, -2, 3, 2);
        const auto track = impl_->listScrollbarTrack().adjusted(-3, 0, 3, 0);
        if (thumb.contains(position)) {
            impl_->listScrollbarDragging = true;
            impl_->listScrollbarDragOffset = position.y() - impl_->listScrollbarThumb().top();
            update();
            return;
        }
        if (track.contains(position)) {
            impl_->setListScrollFromThumb(position.y() - impl_->listScrollbarThumb().height() * .5);
            update();
            return;
        }
    }
    for (int index = 0; index < static_cast<int>(impl_->buses().size()); ++index) {
        const auto row = impl_->listRow(index);
        if (!row.contains(position))
            continue;
        auto& bus = impl_->buses()[index];
        if (impl_->selectedId != bus.id)
            impl_->selectedPoint = 0;
        impl_->selectedId = bus.id;
        if (impl_->listGear(index).contains(position)) {
            const auto id = bus.id;
            QMenu menu(this);
            auto* clone = menu.addAction(QStringLiteral("克隆"));
            auto* remove = menu.addAction(QStringLiteral("删除"));
            remove->setEnabled(impl_->buses().size() > 1);
            connect(clone, &QAction::triggered, this, [this, id] {
                if (auto* copied = impl_->state->cloneBus(id)) {
                    impl_->selectedId = copied->id;
                    impl_->selectedPoint = 0;
                    impl_->listScroll = impl_->maximumListScroll();
                    impl_->changed();
                }
            });
            connect(remove, &QAction::triggered, this, [this, id, index] {
                if (impl_->state->removeBus(id)) {
                    impl_->selectedId = impl_->buses()[std::min(index, static_cast<int>(impl_->buses().size()) - 1)].id;
                    impl_->selectedPoint = 0;
                    impl_->listScroll = std::clamp(impl_->listScroll, 0.0, impl_->maximumListScroll());
                    impl_->changed();
                }
            });
            auto* typeMenu = menu.addMenu(QStringLiteral("切换声道类型"));
            const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                       Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
            for (const auto layout : layouts) {
                auto* action = typeMenu->addAction(Common::auxLayoutName(layout));
                action->setCheckable(true);
                action->setChecked(bus.targetLayout == layout);
                connect(action, &QAction::triggered, this, [this, id, layout] {
                    if (impl_->state->setBusLayout(id, layout)) {
                        if (impl_->selfAuxId == id)
                            impl_->sourceLayout = layout;
                        impl_->changed();
                    }
                });
            }
            menu.exec(event->globalPosition().toPoint());
            update();
            return;
        }
        const QRectF pre(row.left() + 220 * impl_->scaleX(), row.top() + 18 * impl_->scaleY(),
                         45 * impl_->scaleX(), 36 * impl_->scaleY());
        const QRectF post(row.left() + 265 * impl_->scaleX(), row.top() + 18 * impl_->scaleY(),
                          48 * impl_->scaleX(), 36 * impl_->scaleY());
        if (impl_->listKnob(index).adjusted(-5, -5, 5, 5).contains(position) && !bus.selfRoute) {
            impl_->draggedListBusId = bus.id;
            impl_->dragStart = position;
            impl_->draggedListLevels.clear();
            impl_->draggedListLevels.reserve(bus.sends.size());
            for (const auto& send : bus.sends)
                impl_->draggedListLevels.push_back(send.levelDb);
            return;
        }
        if (impl_->listPowerToggle(index).contains(position) && !bus.selfRoute && impl_->chainEnabled)
            bus.enabled = !bus.enabled;
        else if (pre.contains(position))
            bus.preFader = true;
        else if (post.contains(position))
            bus.preFader = false;
        impl_->changed();
        return;
    }
    auto* bus = impl_->selected();
    if (bus == nullptr)
        return;
    if (impl_->spatialModeButton().contains(position)) {
        bus->panMode=Common::AuxPanMode::spatial;impl_->changed();return;
    }
    if (impl_->classicModeButton().contains(position)) {
        if(Common::auxSupportsClassicPan(bus->sourceLayout,bus->targetLayout)){
            bus->panMode=Common::AuxPanMode::classic;impl_->changed();
        }
        return;
    }
    if (impl_->resetButton().contains(position)) {
        impl_->state->resetBus(impl_->sourceKey,bus->id,impl_->selfAuxId);impl_->changed();return;
    }
    if (impl_->sendsLinkButton().contains(position)) {
        bus->sendsLinked=!bus->sendsLinked;impl_->changed();return;
    }
    if (bus->panMode==Common::AuxPanMode::classic) {
        if (impl_->classicPanKnob().contains(position)) {
            impl_->draggedClassicPan=true;impl_->dragStart=position;impl_->dragStartValue=bus->classicPan;
        }
    } else {
        if (impl_->directivityButton().contains(position)) {
            bus->showDirectivity=!bus->showDirectivity;impl_->changed();return;
        }
        for(int i=0;i<3;++i){
            if(impl_->parameterKnob(i).contains(position)){
                impl_->draggedParameter=i;impl_->dragStart=position;
                const int selected=impl_->effectiveSelectedPoint();
                impl_->dragStartValue=i==0?bus->width:i==1&&selected>=0?
                    (bus->adjustablePoints[selected].y()+1)/2:(bus->angleDegrees+180)/360;
                return;
            }
        }
        for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
            if(!Common::auxHasSpatialPoint(bus->sourceLayout,i))continue;
            if(QLineF(position,impl_->pointToScreen(bus->adjustablePoints[i])).length()
               <=12.0*impl_->contentScale()){
                impl_->selectedPoint=i;impl_->draggedPoint=i;impl_->changed();return;
            }
        }
        int influencePoint=-1;
        qreal closestInfluence=9.0*impl_->contentScale();
        for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
            if(!Common::auxHasSpatialPoint(bus->sourceLayout,i))continue;
            const auto distance=QLineF(position,impl_->pointToScreen(bus->adjustablePoints[i])).length();
            const auto error=std::abs(distance-impl_->pointInfluencePixelRadius(*bus,i));
            if(error<closestInfluence){closestInfluence=error;influencePoint=i;}
        }
        if(influencePoint>=0){
            impl_->selectedPoint=influencePoint;
            impl_->draggedInfluencePoint=influencePoint;
            impl_->changed();return;
        }
        if(impl_->pannerCircle().contains(position)&&!bus->adjustablePoints.empty()){
            const QPointF point=impl_->screenToPoint(position);
            qreal closest=1e9;int selected=0;
            for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                if(!Common::auxHasSpatialPoint(bus->sourceLayout,i))continue;
                const auto delta=position-impl_->pointToScreen(bus->adjustablePoints[i]);
                const qreal distance=QPointF::dotProduct(delta,delta);
                if(distance<closest){closest=distance;selected=i;}
            }
            impl_->selectedPoint=selected;impl_->draggedPoint=selected;
            bus->adjustablePoints[selected]=point;impl_->changed();return;
        }
    }
    for(int i=0;i<static_cast<int>(bus->sends.size());++i){
        const auto row=impl_->sendStrip(i);
        if(!row.contains(position))continue;
        const auto x=row.left(),y=row.top(),sx=impl_->scaleX(),sy=impl_->scaleY();
        const QRectF mute(x+174*sx,y+45*sy,62*sx,29*sy);
        const QRectF solo(x+244*sx,y+45*sy,62*sx,29*sy);
        if(mute.contains(position)){bus->sends[i].mute=!bus->sends[i].mute;impl_->changed();return;}
        if(solo.contains(position)){bus->sends[i].solo=!bus->sends[i].solo;impl_->changed();return;}
        if(QRectF(x+44*sx,y+7*sy,52*sx,50*sy).contains(position)){
            impl_->draggedSend=i;impl_->dragStart=position;
            impl_->draggedSendLevels.clear();
            for(const auto& send:bus->sends)impl_->draggedSendLevels.push_back(send.levelDb);
            return;
        }
    }
}

void AuxSettingsWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (impl_->listScrollbarDragging && (event->buttons() & Qt::LeftButton)) {
        impl_->setListScrollFromThumb(event->position().y() - impl_->listScrollbarDragOffset);
        update();
        return;
    }
    if (impl_->draggedListBusId != 0 && (event->buttons() & Qt::LeftButton)) {
        auto* draggedBus = impl_->state->busForSource(impl_->sourceKey, impl_->draggedListBusId,
                                                      impl_->selfAuxId);
        if (draggedBus == nullptr || draggedBus->selfRoute)
            return;
        const auto deltaDb = (impl_->dragStart.y() - event->position().y()) / (120.0 * impl_->scaleY()) * 84.0;
        for (int index = 0; index < static_cast<int>(draggedBus->sends.size())
             && index < static_cast<int>(impl_->draggedListLevels.size()); ++index) {
            draggedBus->sends[index].levelDb = std::clamp(impl_->draggedListLevels[index] + deltaDb,
                                                          -72.01, 12.0);
        }
        impl_->changed();
        return;
    }
    auto* bus = impl_->selected();
    if (bus == nullptr || !(event->buttons() & Qt::LeftButton))
        return;
    if(impl_->draggedClassicPan && bus->panMode==Common::AuxPanMode::classic){
        bus->classicPan=std::clamp(impl_->dragStartValue+(impl_->dragStart.y()-event->position().y())/impl_->scaleY()*.01,-1.0,1.0);
        impl_->changed();return;
    }
    if(bus->panMode==Common::AuxPanMode::classic)return;
    if(impl_->draggedInfluencePoint>=0
       && impl_->draggedInfluencePoint<static_cast<int>(bus->adjustablePoints.size())){
        const auto point=impl_->pointToScreen(bus->adjustablePoints[impl_->draggedInfluencePoint]);
        const auto radius=QLineF(event->position(),point).length()/impl_->contentScale();
        impl_->setPointInfluence(*bus,impl_->draggedInfluencePoint,Common::auxInfluenceRadiusForPixels(radius));
        impl_->changed();return;
    }
    if (impl_->draggedPoint >= 0 && impl_->draggedPoint < static_cast<int>(bus->adjustablePoints.size())) {
        bus->adjustablePoints[impl_->draggedPoint] = impl_->screenToPoint(event->position());
        impl_->changed();
        return;
    }
    if (impl_->draggedSend >= 0 && impl_->draggedSend < static_cast<int>(bus->sends.size())) {
        const auto targetLevel = std::clamp(impl_->draggedSendLevels[impl_->draggedSend]
            +(impl_->dragStart.y()-event->position().y())/impl_->scaleY()*.2,-72.0,12.0);
        if (bus->sendsLinked
            && impl_->draggedSendLevels.size() == bus->sends.size()) {
            qreal lower=-84,upper=84;
            for(const auto level:impl_->draggedSendLevels){lower=std::max(lower,-72.0-level);upper=std::min(upper,12.0-level);}
            const auto deltaDb=std::clamp(targetLevel-impl_->draggedSendLevels[impl_->draggedSend],lower,upper);
            for (int index = 0; index < static_cast<int>(bus->sends.size()); ++index) {
                bus->sends[index].levelDb = impl_->draggedSendLevels[index] + deltaDb;
            }
        } else {
            bus->sends[impl_->draggedSend].levelDb = targetLevel;
        }
        impl_->changed();
        return;
    }
    if (impl_->draggedParameter >= 0) {
        const auto normalized = std::clamp(impl_->dragStartValue
            +(impl_->dragStart.y()-event->position().y())/impl_->scaleY()*.005,0.0,1.0);
        if (impl_->draggedParameter == 0)
            impl_->setLinkedWidth(*bus, normalized);
        else if (impl_->draggedParameter == 1 && impl_->effectiveSelectedPoint()>=0)
            bus->adjustablePoints[impl_->effectiveSelectedPoint()].setY(normalized*2-1);
        else if (impl_->draggedParameter == 2)
            impl_->setAngle(*bus, normalized * 360.0 - 180.0);
        impl_->changed();
    }
}

void AuxSettingsWindow::mouseReleaseEvent(QMouseEvent*)
{
    impl_->listScrollbarDragging = false;
    impl_->draggedPoint = -1;
    impl_->draggedInfluencePoint = -1;
    impl_->draggedSend = -1;
    impl_->draggedSendLevels.clear();
    impl_->draggedListBusId = 0;
    impl_->draggedListLevels.clear();
    impl_->draggedParameter = -1;
    impl_->draggedClassicPan = false;
}

void AuxSettingsWindow::wheelEvent(QWheelEvent* event)
{
    if (auto* bus = impl_->selected(); bus != nullptr && bus->panMode==Common::AuxPanMode::spatial
        && impl_->pannerPanel().contains(event->position())) {
        for (int index = 0; index < static_cast<int>(bus->adjustablePoints.size()); ++index) {
            if(!Common::auxHasSpatialPoint(bus->sourceLayout,index))continue;
            const auto point = impl_->pointToScreen(bus->adjustablePoints[index]);
            const auto distance = QLineF(event->position(), point).length();
            const auto radius = impl_->pointInfluencePixelRadius(*bus, index);
            if (distance <= 16.0 || std::abs(distance - radius) <= 7.0) {
                impl_->selectedPoint = index;
                const auto steps = event->angleDelta().y() / 120.0;
                impl_->setPointInfluence(*bus, index, impl_->pointInfluence(*bus, index) + steps * 0.02);
                impl_->changed();
                event->accept();
                return;
            }
        }
    }
    if (!impl_->listViewport().contains(event->position())) {
        QWidget::wheelEvent(event);
        return;
    }
    impl_->listScroll = std::clamp(impl_->listScroll - event->angleDelta().y() / 120.0 * 55.0,
                                   0.0, impl_->maximumListScroll());
    update();
    event->accept();
}

}
