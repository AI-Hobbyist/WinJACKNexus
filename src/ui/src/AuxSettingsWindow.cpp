#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/MixerPolarPlot.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeBackground.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <QFontMetricsF>
#include <QHelpEvent>
#include <QToolTip>
#include <QElapsedTimer>
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
#include <unordered_map>

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
    Controls::drawText(painter,rect,value,font,color,Qt::Alignment(alignment));
}

void panel(QPainter& painter, const QRectF& rect, const QColor& border = QColor("#183a56"))
{
    Controls::drawSurface(painter,rect,
        Controls::themeTokenColor("sel-aux","surface-panel",QColor("#102235")),border,7);
}

void toggle(QPainter& painter, const QRectF& rect, bool checked)
{
    Controls::drawToggle(painter, rect, checked,
        Controls::themeTokenColor("sel-aux","control-toggle-indicator-on",QColor("#187ee8")),
        Controls::themeTokenColor("sel-aux","control-toggle-indicator-off",QColor("#38516d")));
}

QString elidedName(const QPainter& painter, const QString& value, qreal size, qreal width, bool bold = false)
{
    auto font = painter.font();
    font.setPixelSize(std::max(8, qRound(size)));
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return QFontMetricsF(font).elidedText(value, Qt::ElideRight, std::max(0.0, width));
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
    return QStringLiteral("%1 (%2) → %3 (%4)")
        .arg(sourceNumber,channelNames(bus.sourceLayout),bus.name,
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
        bindRefreshTimer(meterTimer, &owner);
        QObject::connect(&meterTimer, &QTimer::timeout, &owner, [this] {
            const auto& settings=ApplicationSettings::instance();
            const auto elapsedMilliseconds=meterFrameClock.isValid()
                ? meterFrameClock.restart() : -1;
            if(elapsedMilliseconds<0) meterFrameClock.start();
            const double frameSeconds=elapsedMilliseconds>0
                ? elapsedMilliseconds/1000.0 : 1.0/settings.frameRate();
            for (auto& bus : buses()) {
                auto& meters=busMeters[bus.id];
                for (std::size_t index=0;index<bus.sends.size()&&index<meters.size();++index) {
                    auto& send=bus.sends[index];
                    if (channelMeterRead || meterRead) {
                        const auto peak = chainEnabled && bus.enabled && !send.mute
                            ? (channelMeterRead ? channelMeterRead(!bus.preFader, index)
                                                : meterRead(!bus.preFader)) : 0.0F;
                        send.meterDb = peak > 0.0F
                            ? std::clamp(20.0 * std::log10(static_cast<double>(peak)) + send.levelDb,
                                         -72.0, 12.0) : -72.0;
                    } else {
                        const auto base = send.levelDb < -72.0 ? -72.0 : send.levelDb - 3.0;
                        const auto jitter = (QRandomGenerator::global()->bounded(61) - 30) / 10.0;
                        send.meterDb += (std::clamp(base + jitter, -72.0, 12.0) - send.meterDb) * .25;
                    }
                    meters[index].advance(send.meterDb,frameSeconds,settings.peakHoldSeconds());
                    send.meterDb=meters[index].levelDb();
                }
            }
            if (polarRead) {
                const auto* bus = selected();
                if (!polarEnabled() || polarSourceKey != sourceKey || polarTargetId != bus->id) {
                    polarCurve.clear();
                    polarSourceKey = sourceKey;
                    polarTargetId = bus ? bus->id : 0;
                    polarStaleTicks = 0;
                }
                if (polarEnabled()) {
                    Common::MixerPolarSnapshot snapshot;
                    if (polarRead(bus->id, snapshot) && polarCurve.accept(snapshot))
                        polarStaleTicks = 0;
                    else if (++polarStaleTicks > 10) polarCurve.clear();
                }
                this->owner.setProperty("auxPolarDataValid", polarCurve.contour().valid);
            }
            this->owner.update();
        });
        meterTimer.start();
    }

    std::vector<AuxBusState>& buses() const
    {
        return state->busesForSource(sourceKey, selfAuxId);
    }

    std::vector<int> visibleBusIndices() const
    {
        std::vector<int> result;
        const auto& sourceBuses = buses();
        for (int i = 0; i < static_cast<int>(sourceBuses.size()); ++i)
            if (!sourceBuses[i].selfRoute) result.push_back(i);
        return result;
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
    QRectF polarCurvesButton() const { return scaled(711, 52, 182, 34); }
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
        return std::max(0.0, visibleBusIndices().size() * 101.0 - 735.0);
    }

    QRectF listScrollbarThumb() const
    {
        const auto track = listScrollbarTrack();
        const auto contentHeight = std::max(735.0, visibleBusIndices().size() * 79.0);
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

    bool polarEnabled() const
    {
        const auto* bus = selected();
        return chainEnabled && bus && bus->enabled && !bus->selfRoute;
    }

    void clearInactivePolar()
    {
        if (polarEnabled()) return;
        polarCurve.clear();
        polarStaleTicks = 0;
        owner.setProperty("auxPolarDataValid", false);
    }

    void changed()
    {
        if (stateCommit && !stateCommit()) {
            *state = lastAcceptedState;
            const auto visible = visibleBusIndices();
            if (!visible.empty() && (selected() == nullptr || selected()->selfRoute))
                selectedId = buses()[visible.front()].id;
            publishPan();
            owner.update();
            return;
        }
        lastAcceptedState = *state;
        clearInactivePolar();
        publishPan();
        if (draggingControl()) return;
        if (stateChanged)
            stateChanged();
        owner.update();
    }

    bool draggingControl() const
    {
        return listScrollbarDragging || draggedListBusId != 0 || draggedPoint >= 0
            || draggedInfluencePoint >= 0 || draggedSend >= 0 || draggedParameter >= 0
            || draggedClassicPan;
    }

    void publishPan()
    {
        if(const auto* bus=selected()){
            owner.setProperty("auxPolarCurvesVisible",bus->showPolarCurves);
            owner.setProperty("auxInputCurveVisible",bus->showInputCurve);
            owner.setProperty("auxOutputCurveVisible",bus->showOutputCurve);
            owner.setProperty("auxPanMode",static_cast<int>(bus->panMode));
            owner.setProperty("auxClassicPan",bus->classicPan);
            owner.setProperty("auxPannerWidth",bus->width);
            const auto index=effectiveSelectedPoint();
            if(index>=0)owner.setProperty("auxPannerInfluence",pointInfluence(*bus,index));
        }
    }

    AuxSettingsWindow& owner;
    std::shared_ptr<Common::AuxMixerState> state;
    Common::AuxMixerState lastAcceptedState;
    std::function<void()> stateChanged;
    std::function<bool()> stateCommit;
    std::function<float(bool)> meterRead;
    std::function<float(bool, std::size_t)> channelMeterRead;
    std::function<bool(std::uint32_t, Common::MixerPolarSnapshot&)> polarRead;
    Common::MixerPolarCurve polarCurve;
    QString polarSourceKey;
    std::uint32_t polarTargetId {};
    int polarStaleTicks {};
    std::function<void(bool)> chainEnabledHandler;
    std::function<bool(bool)> chainCommitHandler;
    bool chainEnabled {};
    bool threeD {};
    Common::AuxChannelLayout sourceLayout { Common::AuxChannelLayout::stereo };
    QTimer meterTimer;
    QElapsedTimer meterFrameClock;
    std::unordered_map<std::uint32_t,std::array<Controls::MeterBallistics,8>> busMeters;
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
    if (auto* core = ThemeManager::installedThemeCore())
        connect(core, &ThemeCore::themeChanged, this, [this] { update(); });
}

AuxSettingsWindow::~AuxSettingsWindow() = default;

void AuxSettingsWindow::setSource(const QString& sourceKey, const QString& sourceName,
                                  std::uint32_t selfAuxId, Common::AuxChannelLayout layout,
                                  const QString& sourceNumber)
{
    impl_->sourceKey = sourceKey;
    impl_->busMeters.clear();
    impl_->sourceName = sourceName;
    impl_->sourceNumber = sourceNumber.isEmpty() ? sourceName : sourceNumber;
    impl_->selfAuxId = selfAuxId;
    impl_->sourceLayout = layout;
    const auto& buses = impl_->buses();
    for (const auto& bus : buses)
        impl_->state->setLayouts(sourceKey, bus.id, layout, bus.targetLayout, selfAuxId);
    const auto visible = impl_->visibleBusIndices();
    impl_->selectedId = visible.empty() ? 0 : buses[visible.front()].id;
    impl_->selectedPoint = 0;
    impl_->lastAcceptedState = *impl_->state;
    impl_->publishPan();
    setWindowTitle(QStringLiteral("WinJACKNexus - %1 AUX 设置").arg(sourceName));
    update();
}

void AuxSettingsWindow::selectBus(std::uint32_t id)
{
    if (const auto* bus = impl_->state->busForSource(impl_->sourceKey, id, impl_->selfAuxId);
        bus != nullptr && !bus->selfRoute) {
        impl_->selectedId = id;
        impl_->selectedPoint = 0;
        impl_->publishPan();
    }
    update();
}

void AuxSettingsWindow::addBus(Common::AuxChannelLayout layout)
{
    impl_->selectedId = impl_->state->addBus(layout).id;
    impl_->state->setLayouts(impl_->sourceKey, impl_->selectedId,
                             impl_->sourceLayout, layout, impl_->selfAuxId);
    impl_->selectedPoint = 0;
    impl_->listScroll = impl_->maximumListScroll();
    impl_->changed();
}

std::uint32_t AuxSettingsWindow::selectedBusId() const
{
    return impl_->selectedId;
}

void AuxSettingsWindow::setStateChangedCallback(std::function<void()> callback)
{
    impl_->stateChanged = std::move(callback);
}

void AuxSettingsWindow::setStateCommitCallback(std::function<bool()> callback)
{
    impl_->stateCommit = std::move(callback);
    impl_->lastAcceptedState = *impl_->state;
}

void AuxSettingsWindow::setChannelMeterReadCallback(std::function<float(bool, std::size_t)> callback)
{
    impl_->channelMeterRead = std::move(callback);
}

void AuxSettingsWindow::setMeterReadCallback(std::function<float(bool)> callback)
{
    impl_->meterRead = std::move(callback);
}

void AuxSettingsWindow::setPolarReadCallback(
    std::function<bool(std::uint32_t, Common::MixerPolarSnapshot&)> callback)
{
    impl_->polarRead = std::move(callback);
}

void AuxSettingsWindow::acceptExternalState()
{
    impl_->lastAcceptedState = *impl_->state;
    impl_->clearInactivePolar();
}

void AuxSettingsWindow::setChainEnabled(bool enabled)
{
    impl_->chainEnabled = enabled;
    impl_->clearInactivePolar();
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

void AuxSettingsWindow::setChainCommitHandler(std::function<bool(bool)> handler)
{
    impl_->chainCommitHandler = std::move(handler);
}

bool AuxSettingsWindow::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const auto visible = impl_->visibleBusIndices();
        if (impl_->listViewport().contains(help->pos())) {
            for (int index = 0; index < static_cast<int>(visible.size()); ++index) {
                const auto row = impl_->listRow(index);
                if (!row.contains(help->pos())) continue;
                QToolTip::showText(help->globalPos(),
                    mappingLabel(impl_->sourceName, impl_->buses()[visible[index]]), this, row.toAlignedRect());
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void AuxSettingsWindow::paintEvent(QPaintEvent*)
{
    const auto themed=[](const char* role,const QColor& fallback){
        return Controls::themeTokenColor(QStringLiteral("sel-aux"),QString::fromLatin1(role),fallback);
    };
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), themed("surface-empty",QColor("#0b1a2b")));
    ThemeBackground::paint(painter, rect(), QStringLiteral("sel-aux"));
    painter.setOpacity(ThemeBackground::componentOpacity(QStringLiteral("sel-aux")));
    const auto sx = impl_->scaleX();
    const auto sy = impl_->scaleY();
    const auto label = themed("text-primary",QColor("#e8f2ff"));
    const auto secondary = themed("text-muted",QColor("#aec7eb"));
    const auto cyan = themed("control-knob-indicator-normal",QColor("#26bfff"));
    const auto knobColors=Controls::paletteFor(*this,
        {themed("control-knob-background-base",QColor("#0b1a2b")),
         themed("control-knob-panel",QColor("#1b3b57")),
         themed("control-knob-text",QColor("#d9f4ff")),cyan,
         themed("control-knob-track",QColor("#254762"))});

    panel(painter, impl_->leftPanel(), themed("border-panel",QColor("#2b4662")));
    text(painter, impl_->scaled(26, 12, 172, 38), impl_->sourceName + QStringLiteral(" · AUX"), label, 18 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
    toggle(painter, impl_->masterToggle(), impl_->chainEnabled);
    Controls::drawSurface(painter,impl_->addButton(),
        themed("control-add-surface",QColor("#073158")),
        themed("control-add-border",QColor("#0b699d")),6);
    text(painter, impl_->addButton(), QStringLiteral("＋  添加 AUX"),
         themed("control-add-text",QColor("#54d8ff")), 15 * sy);
    Controls::drawSurface(painter,impl_->removeButton(),
        themed("control-remove-surface",QColor("#172d42")),
        themed("control-remove-border",QColor("#0b699d")),6);
    text(painter, impl_->removeButton(), QStringLiteral("−"), label, 21 * sy, Qt::AlignCenter, true);

    painter.save();
    painter.setClipRect(impl_->listViewport());
    const auto visibleBuses = impl_->visibleBusIndices();
    QStringList routeDescriptions;
    for (const auto busIndex : visibleBuses)
        routeDescriptions.push_back(mappingLabel(impl_->sourceName, impl_->buses()[busIndex]));
    setAccessibleDescription(routeDescriptions.join(QLatin1Char('\n')));
    for (int index = 0; index < static_cast<int>(visibleBuses.size()); ++index) {
        const auto& bus = impl_->buses()[visibleBuses[index]];
        const auto row = impl_->listRow(index);
        const auto selected = bus.id == impl_->selectedId;
        const QColor busAccent=bus.accent.isValid()?bus.accent
            : themed("control-bus-accent-default",QColor("#20d9ff"));
        Controls::drawSurface(painter,row,
            selected ? themed("control-bus-surface-selected",QColor("#0d3048"))
                     : themed("control-bus-surface-normal",QColor("#0a1d2c")),
            selected ? busAccent : (bus.accent.isValid()?busAccent.darker(180)
                : themed("control-bus-border-normal",QColor("#17374f"))),
            7,selected ? 1.5 : 1.0);
        if (selected)
            painter.fillRect(QRectF(row.left(), row.top() + 3, 5 * sx, row.height() - 6), busAccent);

        toggle(painter, impl_->listPowerToggle(index), impl_->chainEnabled && bus.enabled && !bus.selfRoute);
        text(painter, { row.left() + 64 * sx, row.top() + 10 * sy, 94 * sx, 25 * sy },
             elidedName(painter, bus.name, 15 * sy, 94 * sx, true), label, 15 * sy, Qt::AlignLeft | Qt::AlignVCenter, true);
        text(painter, { row.left() + 64 * sx, row.top() + 36 * sy, 94 * sx, 22 * sy }, channelNames(bus.targetLayout),
             themed("control-bus-text-secondary",QColor("#91b9e8")),
             13 * sy, Qt::AlignLeft | Qt::AlignVCenter);

        const auto knob = impl_->listKnob(index);
        const auto knobCenter = knob.center();
        const auto level = bus.sends.empty() ? -72.0 : bus.sends.front().levelDb;
        const auto normalized = std::clamp((level + 72.0) / 84.0, 0.0, 1.0);
        painter.save();
        painter.translate(knobCenter);
        painter.scale(sx,sy);
        auto listColors=knobColors;
        if(bus.selfRoute){listColors.accent=secondary;
            listColors.panel=themed("control-knob-panel-self-route",QColor("#385064"));}
        Controls::drawKnob(painter,{0,0},normalized,20,listColors);
        painter.restore();
        text(painter, { knobCenter.x() - 38 * sx, row.top() + 55 * sy, 76 * sx, 16 * sy },
             bus.selfRoute ? QStringLiteral("自身 禁用") : dbText(level),
             bus.selfRoute ? secondary : cyan, 10 * sy);

        const QRectF pre(row.left() + 220 * sx, row.top() + 18 * sy, 45 * sx, 36 * sy);
        const QRectF post(row.left() + 265 * sx, row.top() + 18 * sy, 48 * sx, 36 * sy);
        const auto routeActive = themed("control-route-surface-selected",QColor("#087ee4"));
        const auto routeIdle = themed("control-route-surface-normal",QColor("#172d43"));
        const auto routeBorder = themed("control-route-border",QColor("#315370"));
        Controls::drawSurface(painter,pre,bus.preFader ? routeActive : routeIdle,routeBorder,5);
        Controls::drawSurface(painter,post,!bus.preFader ? routeActive : routeIdle,routeBorder,5);
        const auto routeText = themed("control-route-text-selected",QColor("#ffffff"));
        text(painter, pre, QStringLiteral("Pre"), bus.preFader ? routeText : secondary, 12 * sy);
        text(painter, post, QStringLiteral("Post"), !bus.preFader ? routeText : secondary, 12 * sy);
        const auto gear = impl_->listGear(index);
        Controls::drawSurface(painter,gear,
            themed("control-gear-surface",QColor("#142b3e")),routeBorder,5);
        text(painter, gear, QStringLiteral("⚙"), label, 18 * sy);
        painter.setPen(QPen(themed("control-bus-divider",QColor("#24445d")),1));
        painter.drawLine(QPointF(row.left()+12*sx,row.top()+72*sy),
                         QPointF(row.right()-12*sx,row.top()+72*sy));
        text(painter,{row.left()+16*sx,row.top()+75*sy,row.width()-32*sx,17*sy},
             elidedName(painter, mappingLabel(impl_->sourceName,bus), 12 * sy, row.width() - 32 * sx),
             themed("control-bus-mapping-text",QColor("#a8c8e7")),12*sy,
             Qt::AlignCenter);
    }
    painter.restore();

    const auto scrollbarTrack = impl_->listScrollbarTrack();
    const auto scrollbarThumb = impl_->listScrollbarThumb();
    const auto listScrollable = impl_->maximumListScroll() > 0.0;
    Controls::drawSurface(painter,scrollbarTrack,
        themed("control-scrollbar-track",QColor("#10283a")),{},scrollbarTrack.width()*.5);
    Controls::drawSurface(painter,scrollbarThumb,
        !listScrollable ? themed("control-scrollbar-thumb-disabled",QColor("#29465b"))
            : impl_->listScrollbarDragging
                ? themed("control-scrollbar-thumb-dragging",QColor("#56d7ff"))
                : themed("control-scrollbar-thumb-normal",QColor("#258fc3")),
        {},scrollbarThumb.width()*.5);

    auto* bus = impl_->selected();
    if (bus == nullptr)
        return;

    painter.save();
    painter.translate(impl_->contentOrigin());
    painter.scale(sx, sy);
    const auto drawPanel=[&](QRectF rect){
        Controls::drawSurface(painter,rect,themed("surface-panel",QColor("#102235")),
            themed("border-panel",QColor("#2b4662")),7);
    };
    const auto drawText=[&](QRectF rect,const QString& value,int size=15,QColor color=QColor("#e8f2ff"),bool bold=false,int align=Qt::AlignLeft|Qt::AlignVCenter){
        auto font=painter.font();font.setPixelSize(size);font.setBold(bold);
        Controls::drawText(painter,rect,value,font,color,Qt::Alignment(align));
    };
    const auto drawButton=[&](QRectF rect,const QString& value,bool active=false){
        Controls::ButtonStyle style;
        style.activeBorder=cyan;
        style.inactiveText=secondary;
        style.fontSize=14;
        Controls::drawStyledButton(painter,rect,value,active,style);
    };
    const auto drawToggle=[&](QRectF rect,bool active){
        Controls::drawToggle(painter,rect,active,
            themed("control-toggle-indicator-on",QColor("#168dff")),
            themed("control-toggle-indicator-off",QColor("#263e53")));
    };
    const auto drawMuteSolo=[&](QRectF rect,const QString& value,bool active,QColor accent){
        Controls::ButtonStyle style;
        style.inactiveBorder=style.activeBorder=accent;
        style.inactiveFill=style.activeFill=accent;
        style.inactiveFill.setAlpha(25);
        style.activeFill.setAlpha(85);
        style.inactiveText=style.activeText=
            themed("control-mute-solo-text",QColor("#f7f8ff"));
        style.bold=true;
        Controls::drawStyledButton(painter,rect,value,active,style);
    };
    const bool canClassic=Common::auxSupportsClassicPan(bus->sourceLayout,bus->targetLayout);
    const bool classic=canClassic&&bus->panMode==Common::AuxPanMode::classic;
    drawPanel({406,11,559,544});drawPanel({406,566,559,235});drawPanel({976,11,359,790});
    drawText({420,20,250,29},QStringLiteral("声像器"),17,label,true);
    drawText({784,20,165,29},bus->name,12,secondary,false,Qt::AlignRight|Qt::AlignVCenter);
    drawButton({420,52,137,34},QStringLiteral("空间声像器"),!classic);
    painter.save();if(!canClassic)painter.setOpacity(.32);
    drawButton({567,52,137,34},QStringLiteral("经典 Pan"),classic);
    painter.restore();
    if(!classic){drawText({711,52,133,34},QStringLiteral("显示极坐标曲线"),13,secondary);drawToggle({851,56,42,24},bus->showPolarCurves);}
    drawPanel({420,95,531,448});
    const QPointF center(686,319);
    if(classic){
        Controls::Palette panColors;
        panColors.border=themed("control-pan-border",QColor("#254762"));
        panColors.accent=cyan;
        panColors.panel=themed("control-knob-panel",QColor("#1b3b57"));
        panColors.text=themed("control-knob-text",QColor("#d9f4ff"));
        Controls::drawPanKnob(painter,center,bus->classicPan,170,panColors);
        drawText({center.x()-110,105,220,35},QStringLiteral("声像"),19,label,true,Qt::AlignCenter);
        drawText({center.x()-190,505,90,25},QStringLiteral("L100%"),15,secondary,true,Qt::AlignCenter);
        drawText({center.x()-20,505,40,25},QStringLiteral("C"),17,secondary,true,Qt::AlignCenter);
        drawText({center.x()+100,505,90,25},QStringLiteral("R100%"),15,secondary,true,Qt::AlignCenter);
        const auto direction=bus->classicPan<0?QStringLiteral("L"):bus->classicPan>0?QStringLiteral("R"):QStringLiteral("C");
        drawText({center.x()-110,339,220,30},QStringLiteral("%1 %2%").arg(direction).arg(qRound(std::abs(bus->classicPan)*100)),16,label,true,Qt::AlignCenter);
    }else{
        constexpr qreal radius=190,pointRadius=159;
        drawMixerPolarGrid(painter,center,radius,QStringLiteral("sel-aux"));
        if(bus->showPolarCurves) {
            const auto contour = impl_->polarEnabled()
                ? impl_->polarCurve.contour() : Common::MixerPolarContour {};
            drawMixerPolarCurves(painter,{420,95,531,448},center,radius,contour,
                                 bus->showInputCurve,bus->showOutputCurve,QStringLiteral("sel-aux"));
            if(!contour.valid) drawText({center.x()-62,center.y()-12,124,24},
                                        QStringLiteral("无信号"),13,secondary,false,Qt::AlignCenter);
        }
        const auto selectedPoint=impl_->effectiveSelectedPoint();
        const auto outputGains=selectedPoint>=0
            ? Common::auxSpatialOutputGains(*bus,selectedPoint):std::vector<double>{};
        const auto references=Common::auxLayoutReferencePoints(bus->targetLayout);
        for(int i=0;i<static_cast<int>(references.size());++i){
            if(!Common::auxHasSpatialPoint(bus->targetLayout,i))continue;
            const auto gain=i<static_cast<int>(outputGains.size())
                ? outputGains[i]:0.0;
            drawMixerPolarReferencePoint(painter,center+references[i]*pointRadius,gain,QStringLiteral("sel-aux"));
        }
        const auto labels=Common::auxLayoutChannelLabels(bus->sourceLayout);
        for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
            if(!Common::auxHasSpatialPoint(bus->sourceLayout,i))continue;
            const auto point=center+bus->adjustablePoints[i]*pointRadius;
            const bool selected=i==selectedPoint;
            const auto outerRadius=Common::auxInfluencePixelRadius(impl_->pointInfluence(*bus,i));
            drawMixerPolarHandle(painter,point,outerRadius,selected,QStringLiteral("sel-aux"));
            if(i<labels.size())drawText({point.x()-17,point.y()+14,34,19},labels[i],12,label,true,Qt::AlignCenter);
        }
        drawMixerPolarCenter(painter,center,QStringLiteral("sel-aux"));
        drawText({center.x()-22,105,44,20},QStringLiteral("0°"),12,secondary,false,Qt::AlignCenter);
        drawText({center.x()+radius+8,309,45,20},QStringLiteral("90°"),12,secondary);
        drawText({center.x()-radius-53,309,45,20},QStringLiteral("270°"),12,secondary);
        drawText({center.x()-24,516,48,20},QStringLiteral("180°"),12,secondary,false,Qt::AlignCenter);
        if(bus->showPolarCurves){
            drawButton({420,508,70,27},QStringLiteral("输入"),bus->showInputCurve);
            drawButton({495,508,70,27},QStringLiteral("输出"),bus->showOutputCurve);
        }
    }
    drawText({420,574,300,27},QStringLiteral("空间声像参数"),17,label,true);
    painter.save();if(classic)painter.setOpacity(.32);
    for(int i=0;i<3;++i){
        const qreal x=420+i*178;
        drawPanel({x,609,168,178});
        drawText({x+10,616,148,25},i==0?QStringLiteral("宽度"):i==1?QStringLiteral("前后"):QStringLiteral("角度"),15,label,true,Qt::AlignCenter);
        const auto selectedPoint=impl_->effectiveSelectedPoint();
        const qreal fraction=i==0?bus->width:i==1&&selectedPoint>=0?(bus->adjustablePoints[selectedPoint].y()+1)/2:(bus->angleDegrees+180)/360;
        Controls::drawKnob(painter,{x+84,694},fraction,35,knobColors);
        const QString value=i==0?QStringLiteral("%1%").arg(qRound(bus->width*100)):i==1&&selectedPoint>=0?QStringLiteral("%1%").arg(qRound(bus->adjustablePoints[selectedPoint].y()*100)):QStringLiteral("%1°").arg(qRound(bus->angleDegrees));
        drawText({x+10,748,148,26},value,15,label,true,Qt::AlignCenter);
    }
    painter.restore();
    drawText({992,20,135,29},QStringLiteral("分控旋钮"),17,label,true);
    drawText({992,54,90,28},QStringLiteral("全通道链接"),13,secondary);drawToggle({1085,57,42,24},bus->sendsLinked);
    drawButton({1220,52,100,34},QStringLiteral("重置"));
    const std::array<QString,8> sendLabels{QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
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
    sendMeter.labelColor=secondary;
    sendMeter.ticksAbove=true;
    sendMeter.tickBandPixels=24;
    for(int i=0;i<8;++i){
        painter.save();if(i>=static_cast<int>(bus->sends.size()))painter.setOpacity(.32);
        const qreal x=992,y=100+i*86;
        drawPanel({x,y,327,80});
        drawText({x+7,y+23,30,28},i==0&&bus->sourceLayout==AuxChannelLayout::mono
                     ? QStringLiteral("M") : sendLabels[i],i==3?12:15,label,true,Qt::AlignCenter);
        if(i==3&&i<static_cast<int>(bus->sends.size()))
            drawText({x+3,y+48,38,16},QStringLiteral("强度"),11,secondary,false,Qt::AlignCenter);
        const auto* send=i<static_cast<int>(bus->sends.size())?&bus->sends[i]:nullptr;
        const qreal level=send?send->levelDb:0.0,meterDb=send?send->meterDb:-72.0;
        Controls::drawKnob(painter,{x+70,y+32},(level+72)/84.0,23,knobColors);
        drawText({x+35,y+57,72,18},QStringLiteral("%1 dB").arg(level,0,'f',1),11,label,true,Qt::AlignCenter);
        const auto meterState=impl_->busMeters.find(bus->id);
        const qreal peak=meterState!=impl_->busMeters.end()
            ? meterState->second[static_cast<std::size_t>(i)].peakDb():meterDb;
        Controls::drawMeter(painter,{x+112,y+3,200,33},meterDb,peak,sendMeter);
        drawMuteSolo({x+174,y+45,62,29},QStringLiteral("M"),send&&send->mute,
            themed("control-mute-accent",QColor("#ff4b62")));
        drawMuteSolo({x+244,y+45,62,29},QStringLiteral("S"),send&&send->solo,
            themed("control-solo-accent",QColor("#ffae2d")));
        painter.restore();
    }
    painter.restore();
    painter.setOpacity(1.0);
    ThemeBackground::paint(painter, rect(), QStringLiteral("sel-aux"), true);
}

void AuxSettingsWindow::mousePressEvent(QMouseEvent* event)
{
    const auto position = event->position();
    if (impl_->masterToggle().contains(position)) {
        const bool next = !impl_->chainEnabled;
        if (impl_->chainCommitHandler && !impl_->chainCommitHandler(next)) return;
        impl_->chainEnabled = next;
        impl_->clearInactivePolar();
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
                addBus(layout);
            });
        }
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (impl_->removeButton().contains(position)) {
        const auto previous = impl_->selectedId;
        if (impl_->state->removeBus(previous, static_cast<bool>(impl_->stateCommit))) {
            impl_->busMeters.erase(previous);
            const auto visible = impl_->visibleBusIndices();
            impl_->selectedId = visible.empty() ? 0 : impl_->buses()[visible.front()].id;
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
    const auto visible = impl_->visibleBusIndices();
    for (int index = 0; index < static_cast<int>(visible.size()); ++index) {
        const auto row = impl_->listRow(index);
        if (!row.contains(position))
            continue;
        auto& bus = impl_->buses()[visible[index]];
        if (impl_->selectedId != bus.id)
            impl_->selectedPoint = 0;
        impl_->selectedId = bus.id;
        if (impl_->listGear(index).contains(position)) {
            const auto id = bus.id;
            QMenu menu(this);
            auto* clone = menu.addAction(QStringLiteral("克隆"));
            auto* remove = menu.addAction(QStringLiteral("删除"));
            remove->setEnabled(impl_->buses().size() > 1 || static_cast<bool>(impl_->stateCommit));
            connect(clone, &QAction::triggered, this, [this, id] {
                if (auto* copied = impl_->state->cloneBus(id)) {
                    impl_->selectedId = copied->id;
                    impl_->selectedPoint = 0;
                    impl_->listScroll = impl_->maximumListScroll();
                    impl_->changed();
                }
            });
            connect(remove, &QAction::triggered, this, [this, id, index] {
                if (impl_->state->removeBus(id, static_cast<bool>(impl_->stateCommit))) {
                    const auto remaining = impl_->visibleBusIndices();
                    impl_->selectedId = remaining.empty() ? 0 : impl_->buses()[remaining[std::min(index, static_cast<int>(remaining.size()) - 1)]].id;
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
        const QRectF readout(impl_->listKnob(index).center().x() - 38 * impl_->scaleX(),
                             row.top() + 55 * impl_->scaleY(), 76 * impl_->scaleX(), 16 * impl_->scaleY());
        if (event->button() == Qt::LeftButton && impl_->listViewport().contains(position)
            && readout.contains(position) && !bus.selfRoute && !bus.sends.empty()) {
            const auto source = impl_->sourceKey;
            const auto id = bus.id;
            Controls::editNumericValue(this, readout.toAlignedRect(), bus.sends.front().levelDb,
                {-72, 12, 0, 1}, [this, source, id](double value) {
                    if (impl_->sourceKey != source) return;
                    auto* current = impl_->state->busForSource(source, id, impl_->selfAuxId);
                    if (!current || current->selfRoute) return;
                    for (auto& send : current->sends) send.levelDb = value;
                    impl_->changed();
                });
            return;
        }
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
    if(bus->panMode==Common::AuxPanMode::spatial) {
        for(int index=0;index<3;++index) {
            const auto readout=impl_->scaled(430+index*178,748,148,26);
            if(!readout.contains(position)) continue;
            const int selected=impl_->effectiveSelectedPoint();
            const double current=index==0?bus->width*100
                :index==1&&selected>=0?bus->adjustablePoints[selected].y()*100
                :bus->angleDegrees;
            const Controls::NumericControlSpec spec=index==0
                ? Controls::NumericControlSpec{0,100,70,0}
                :index==1?Controls::NumericControlSpec{-100,100,0,0}
                :Controls::NumericControlSpec{-180,180,0,0};
            const auto id=bus->id;
            Controls::editNumericValue(this,readout.toAlignedRect(),current,spec,
                [this,id,index](double value) {
                    auto* currentBus=impl_->selected();
                    if(!currentBus||currentBus->id!=id) return;
                    if(index==0) impl_->setLinkedWidth(*currentBus,value/100);
                    else if(index==1&&impl_->effectiveSelectedPoint()>=0)
                        currentBus->adjustablePoints[impl_->effectiveSelectedPoint()].setY(value/100);
                    else if(index==2) impl_->setAngle(*currentBus,value);
                    impl_->changed();
                });
            return;
        }
    }
    for(int index=0;index<static_cast<int>(bus->sends.size());++index) {
        const auto readout=impl_->scaled(1027,157+index*86,72,18);
        if(!readout.contains(position)) continue;
        const auto id=bus->id;
        Controls::editNumericValue(this,readout.toAlignedRect(),bus->sends[index].levelDb,
            {-72,12,-2,1},[this,id,index](double value) {
                auto* currentBus=impl_->selected();
                if(!currentBus||currentBus->id!=id||index>=currentBus->sends.size()) return;
                if(currentBus->sendsLinked)
                    for(auto& send:currentBus->sends) send.levelDb=value;
                else currentBus->sends[index].levelDb=value;
                impl_->changed();
            });
        return;
    }
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
        if (impl_->polarCurvesButton().contains(position)) {
            bus->showPolarCurves=!bus->showPolarCurves;impl_->publishPan();impl_->changed();return;
        }
        if (bus->showPolarCurves && impl_->scaled(420,508,70,27).contains(position)) {
            bus->showInputCurve=!bus->showInputCurve;impl_->publishPan();impl_->changed();return;
        }
        if (bus->showPolarCurves && impl_->scaled(495,508,70,27).contains(position)) {
            bus->showOutputCurve=!bus->showOutputCurve;impl_->publishPan();impl_->changed();return;
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

void AuxSettingsWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(event->button()!=Qt::LeftButton) return;
    const auto visible = impl_->visibleBusIndices();
    if (impl_->listViewport().contains(event->position())) {
        for (int index = 0; index < static_cast<int>(visible.size()); ++index) {
            if (!impl_->listKnob(index).contains(event->position())) continue;
            auto& listedBus = impl_->buses()[visible[index]];
            if (listedBus.selfRoute) return;
            impl_->draggedListBusId = 0;
            impl_->draggedListLevels.clear();
            Controls::resetNumericValue({-72, 12, 0, 1}, [this, &listedBus](double value) {
                for (auto& send : listedBus.sends) send.levelDb = value;
                impl_->changed();
            });
            return;
        }
    }
    auto* bus=impl_->selected();
    if(!bus) return;
    const auto point=event->position();
    if(bus->panMode==Common::AuxPanMode::spatial) {
        for(int index=0;index<3;++index) {
            if(!impl_->parameterKnob(index).contains(point)) continue;
            impl_->draggedParameter=-1;
            const auto spec=index==0?Controls::NumericControlSpec{0,100,100,0}
                :index==1?Controls::NumericControlSpec{-100,100,0,0}
                :Controls::NumericControlSpec{-180,180,0,0};
            Controls::resetNumericValue(spec,[this,bus,index](double value) {
                if(index==0) impl_->setLinkedWidth(*bus,value/100);
                else if(index==1&&impl_->effectiveSelectedPoint()>=0)
                    bus->adjustablePoints[impl_->effectiveSelectedPoint()].setY(value/100);
                else if(index==2) impl_->setAngle(*bus,value);
                impl_->changed();
            });
            return;
        }
    } else if(impl_->classicPanKnob().contains(point)) {
        impl_->draggedClassicPan=false;
        Controls::resetNumericValue({-1,1,0,2},[this,bus](double value) {
            bus->classicPan=value; impl_->changed();
        });
        return;
    }
    for(int index=0;index<static_cast<int>(bus->sends.size());++index) {
        const auto row=impl_->sendStrip(index);
        const QRectF knob(row.left()+44*impl_->scaleX(),row.top()+7*impl_->scaleY(),
                          52*impl_->scaleX(),50*impl_->scaleY());
        if(!knob.contains(point)) continue;
        impl_->draggedSend=-1;
        Controls::resetNumericValue({-72,12,-2,1},[this,bus,index](double value) {
            if(bus->sendsLinked) for(auto& send:bus->sends) send.levelDb=value;
            else bus->sends[index].levelDb=value;
            impl_->changed();
        });
        return;
    }
}

void AuxSettingsWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (impl_->listScrollbarDragging && (event->buttons() & Qt::LeftButton)) {
        impl_->setListScrollFromThumb(event->position().y() - impl_->listScrollbarDragOffset);
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
    const bool wasDragging = impl_->draggingControl();
    const bool changedState = impl_->draggedListBusId != 0 || impl_->draggedPoint >= 0
        || impl_->draggedInfluencePoint >= 0 || impl_->draggedSend >= 0
        || impl_->draggedParameter >= 0 || impl_->draggedClassicPan;
    impl_->listScrollbarDragging = false;
    impl_->draggedPoint = -1;
    impl_->draggedInfluencePoint = -1;
    impl_->draggedSend = -1;
    impl_->draggedSendLevels.clear();
    impl_->draggedListBusId = 0;
    impl_->draggedListLevels.clear();
    impl_->draggedParameter = -1;
    impl_->draggedClassicPan = false;
    if (wasDragging) {
        if (changedState && impl_->stateChanged) impl_->stateChanged();
        update();
    }
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
