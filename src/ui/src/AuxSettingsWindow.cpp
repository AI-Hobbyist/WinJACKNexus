#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>

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
    QRectF topBar() const { return scaled(406, 11, 929, 443); }
    QRectF pannerPanel() const { return scaled(420, 95, 429, 345); }
    QRectF parametersPanel() const { return scaled(855, 95, 150, 345); }
    QRectF roomPanel() const { return scaled(1018, 95, 307, 345); }
    QRectF sendsPanel() const { return scaled(406, 465, 919, 336); }
    QRectF resetButton() const { return scaled(1230, 52, 95, 34); }
    QRectF sendsModeButton() const { return scaled(925, 477, 125, 34); }
    QRectF sendsLinkButton() const { return scaled(1148, 482, 44, 24); }
    QRectF resetSendsButton() const { return scaled(1212, 477, 96, 32); }
    QRectF linkButton() const { return scaled(945, 126, 44, 24); }
    QRectF directivityButton() const { return scaled(941, 55, 42, 24); }
    QRectF widthTrack() const { return scaled(870, 199, 117, 16); }
    QRectF angleTrack() const { return scaled(870, 298, 117, 16); }
    QRectF coverageTrack() const { return scaled(870, 397, 117, 16); }

    QRectF listRow(int index) const
    {
        return scaled(14, 62 + index * 79 - listScroll, 358, 73);
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
        return std::max(0.0, buses().size() * 79.0 - 735.0);
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
        return { row.left() + 156 * scaleX(), row.center().y() - 20 * scaleY(),
                 40 * scaleX(), 40 * scaleY() };
    }

    QRectF pannerCircle() const
    {
        const auto area = pannerPanel().adjusted(50 * scaleX(), 24 * scaleY(), -50 * scaleX(), -24 * scaleY());
        const auto side = std::min(area.width(), area.height());
        return { area.center().x() - side / 2.0, area.center().y() - side / 2.0, side, side };
    }

    QPointF pointToScreen(const QPointF& point) const
    {
        const auto circle = pannerCircle();
        return circle.center() + QPointF(point.x() * circle.width() * .5, point.y() * circle.height() * .5);
    }

    QPointF screenToPoint(const QPointF& point) const
    {
        const auto circle = pannerCircle();
        auto normalized = QPointF((point.x() - circle.center().x()) / (circle.width() * .5),
                                  (point.y() - circle.center().y()) / (circle.height() * .5));
        const auto length = std::hypot(normalized.x(), normalized.y());
        if (length > 1.0)
            normalized /= length;
        return normalized;
    }

    QRectF sendStrip(int index) const
    {
        const auto count = 8;
        const auto body = sendsPanel().adjusted(10 * scaleX(), 55 * scaleY(), -10 * scaleX(), -15 * scaleY());
        const auto width = body.width() / count;
        return { body.left() + index * width + 3, body.top(), width - 6, body.height() };
    }

    QRectF sendTrack(int index) const
    {
        const auto strip = sendStrip(index);
        return { strip.right()-31*scaleX(), strip.top()+41*scaleY(),
                 7*scaleX(),139*scaleY() };
    }

    QRectF meterTrack(int index) const
    {
        const auto strip=sendStrip(index);
        return {strip.left()+32*scaleX(),strip.top()+41*scaleY(),19*scaleX(),139*scaleY()};
    }

    int effectiveSelectedPoint() const
    {
        const auto* bus = selected();
        if (bus == nullptr || bus->adjustablePoints.empty())
            return -1;
        return std::clamp(selectedPoint, 0, static_cast<int>(bus->adjustablePoints.size()) - 1);
    }

    double pointCoverage(const AuxBusState& bus, int index) const
    {
        return index >= 0 && index < static_cast<int>(bus.pointCoverageDegrees.size())
            ? bus.pointCoverageDegrees[index] : bus.coverageDegrees;
    }

    qreal pointCoverageRadius(const AuxBusState& bus, int index) const
    {
        const auto scale = std::min(scaleX(), scaleY());
        return (22.0 + std::clamp(pointCoverage(bus, index) / 360.0, 0.0, 1.0) * 38.0) * scale;
    }

    void setPointCoverage(AuxBusState& bus, int index, double degrees)
    {
        if (index < 0 || index >= static_cast<int>(bus.adjustablePoints.size()))
            return;
        if (bus.pointCoverageDegrees.size() != bus.adjustablePoints.size())
            bus.pointCoverageDegrees.resize(bus.adjustablePoints.size(), bus.coverageDegrees);
        bus.pointCoverageDegrees[index] = std::clamp(degrees, 5.0, 360.0);
    }

    void setLinkedWidth(AuxBusState& bus, double width)
    {
        bus.width = std::clamp(width, 0.0, 1.0);
        if (!bus.linked || bus.adjustablePoints.size() != 2)
            return;
        const auto center = (bus.adjustablePoints[0] + bus.adjustablePoints[1]) * .5;
        auto direction = bus.adjustablePoints[1] - bus.adjustablePoints[0];
        const auto length = std::hypot(direction.x(), direction.y());
        direction = length < .001 ? QPointF(1.0, 0.0) : direction / length;
        const auto halfDistance = .12 + bus.width * .65;
        bus.adjustablePoints[0] = center - direction * halfDistance;
        bus.adjustablePoints[1] = center + direction * halfDistance;
    }

    void setAngle(AuxBusState& bus, double angle)
    {
        const auto delta = (angle - bus.angleDegrees) * pi / 180.0;
        const auto cosine = std::cos(delta);
        const auto sine = std::sin(delta);
        for (auto& point : bus.adjustablePoints)
            point = { point.x() * cosine - point.y() * sine, point.x() * sine + point.y() * cosine };
        bus.angleDegrees = angle;
    }

    void changed()
    {
        if (stateChanged)
            stateChanged();
        owner.update();
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
    std::uint32_t selfAuxId {};
    std::uint32_t selectedId {};
    qreal listScroll {};
    bool listScrollbarDragging {};
    qreal listScrollbarDragOffset {};
    int selectedPoint {};
    int draggedPoint { -1 };
    int draggedCoveragePoint { -1 };
    int draggedSend { -1 };
    std::vector<double> draggedSendLevels;
    std::uint32_t draggedListBusId {};
    std::vector<double> draggedListLevels;
    int draggedParameter { -1 };
    bool draggingLink {};
    QPointF dragStart;
    double dragStartWidth {};
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
                                  std::uint32_t selfAuxId, Common::AuxChannelLayout layout)
{
    impl_->sourceKey = sourceKey;
    impl_->sourceName = sourceName;
    impl_->selfAuxId = selfAuxId;
    impl_->sourceLayout = layout;
    const auto& buses = impl_->buses();
    for (const auto& bus : buses)
        impl_->state->setLayouts(sourceKey, bus.id, layout, bus.targetLayout, selfAuxId);
    impl_->selectedId = buses.empty() ? 0 : buses.front().id;
    impl_->selectedPoint = 0;
    setWindowTitle(QStringLiteral("WinJACKNexus - %1 AUX 设置").arg(sourceName));
    update();
}

void AuxSettingsWindow::selectBus(std::uint32_t id)
{
    if (impl_->state->busForSource(impl_->sourceKey, id, impl_->selfAuxId) != nullptr) {
        impl_->selectedId = id;
        impl_->selectedPoint = 0;
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
        const QRectF arc = knob.adjusted(-2 * sx, -2 * sy, 2 * sx, 2 * sy);
        painter.setPen(QPen(QColor("#254c66"), 4 * sx, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(arc, 225 * 16, -270 * 16);
        if (!bus.selfRoute) {
            painter.setPen(QPen(cyan, 4 * sx, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(arc, 225 * 16, qRound(-270.0 * normalized * 16.0));
        }
        QRadialGradient knobGradient(knobCenter - QPointF(6 * sx, 7 * sy), knob.width() * .55);
        knobGradient.setColorAt(0, bus.selfRoute ? QColor("#385064") : QColor("#315b78"));
        knobGradient.setColorAt(1, QColor("#0b1b28"));
        painter.setPen(QPen(QColor("#43657d"), 1));
        painter.setBrush(knobGradient);
        painter.drawEllipse(knob.adjusted(5 * sx, 5 * sy, -5 * sx, -5 * sy));
        const auto indicatorAngle = (225.0 - 270.0 * normalized) * pi / 180.0;
        const auto indicatorEnd = knobCenter + QPointF(std::cos(indicatorAngle), -std::sin(indicatorAngle)) * 11 * sx;
        painter.setPen(QPen(bus.selfRoute ? secondary : cyan, 2 * sx, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(knobCenter, indicatorEnd);
        text(painter, { knobCenter.x() - 38 * sx, row.bottom() - 18 * sy, 76 * sx, 16 * sy },
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

    panel(painter, impl_->topBar(), QColor("#2b4662"));
    text(painter,impl_->scaled(420,20,250,29),QStringLiteral("空间声像器"),label,18*sy,Qt::AlignLeft|Qt::AlignVCenter,true);
    text(painter,impl_->scaled(1020,20,300,29),QStringLiteral("AUX %1 · %2").arg(bus->id).arg(bus->name),secondary,15*sy,Qt::AlignRight|Qt::AlignVCenter);
    const auto detailButton=[&](const QRectF& rect,const QString& value,bool active=false){
        painter.setPen(QPen(active?cyan:QColor("#45617e"),1));painter.setBrush(active?QColor("#123e61"):QColor("#192d43"));painter.drawRoundedRect(rect,5,5);
        text(painter,rect,value,active?QColor("#d7f7ff"):secondary,15*sy);
    };
    detailButton(impl_->scaled(420,52,80,34),QStringLiteral("2D"),!impl_->threeD);
    detailButton(impl_->scaled(511,52,80,34),QStringLiteral("3D"),impl_->threeD);
    detailButton(impl_->scaled(611,52,165,34),Common::auxLayoutName(bus->targetLayout)+QStringLiteral(" ▾"));
    text(painter,impl_->scaled(790,52,143,34),QStringLiteral("显示分布图"),secondary,15*sy);
    detailButton(impl_->scaled(1022,52,108,34),QStringLiteral("3D 视图"),impl_->threeD);
    detailButton(impl_->resetButton(),QStringLiteral("重置"));

    panel(painter, impl_->pannerPanel(), QColor("#2b4662"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(bus->showDirectivity ? blue : QColor("#263e53"));
        painter.drawRoundedRect(impl_->directivityButton(), 11, 11);
        painter.setBrush(QColor("#ffffff"));
        painter.drawEllipse(QPointF(bus->showDirectivity ? impl_->directivityButton().right() - 10 * sx
                                                : impl_->directivityButton().left() + 10 * sx,
                              impl_->directivityButton().center().y()), 7 * sx, 7 * sy);
    const auto circle = impl_->pannerCircle();
    painter.setBrush(Qt::NoBrush);
    for (int ring = 1; ring <= 5; ++ring) {
        const auto diameter = circle.width() * ring / 5.0;
        painter.setPen(QPen(QColor("#244a66"), 1));
        painter.drawEllipse(QRectF(circle.center().x() - diameter / 2.0, circle.center().y() - diameter / 2.0, diameter, diameter));
    }
    for (int angle = 0; angle < 360; angle += 30) {
        const auto radians = angle * pi / 180.0;
        painter.setPen(QPen(angle % 90 == 0 ? QColor("#426884") : QColor("#1b3d56"), 1));
        painter.drawLine(circle.center(), circle.center() + QPointF(std::sin(radians), -std::cos(radians)) * circle.width() * .5);
    }
    const auto selectedPoint = impl_->effectiveSelectedPoint();
    const auto selectedCoverage = impl_->pointCoverage(*bus, selectedPoint);
    if (bus->showDirectivity && selectedPoint >= 0) {
        const auto startAngle = 90.0 - (bus->angleDegrees + selectedCoverage * .5);
        QPainterPath coverage;
        coverage.moveTo(circle.center());
        coverage.arcTo(circle.adjusted(circle.width() * .08, circle.height() * .08,
                                       -circle.width() * .08, -circle.height() * .08),
                       startAngle, selectedCoverage);
        coverage.closeSubpath();
        painter.setPen(QPen(alpha(cyan, 180), 2));
        painter.setBrush(alpha(QColor("#158fd0"), 55));
        painter.drawPath(coverage);
    }
    text(painter, { circle.center().x() - 20, circle.top() - 20 * sy, 40, 18 * sy }, QStringLiteral("90°"), secondary, 11 * sy);
    text(painter, { circle.right() + 4, circle.center().y() - 9, 35, 18 }, QStringLiteral("0°"), secondary, 11 * sy, Qt::AlignLeft | Qt::AlignVCenter);
    text(painter, { circle.left() - 40, circle.center().y() - 9, 35, 18 }, QStringLiteral("180°"), secondary, 11 * sy, Qt::AlignRight | Qt::AlignVCenter);
    text(painter, { circle.center().x() - 20, circle.bottom() + 2, 40, 18 }, QStringLiteral("270°"), secondary, 11 * sy);

    for (const auto& reference : Common::auxLayoutReferencePoints(bus->targetLayout)) {
        const auto point = impl_->pointToScreen(reference);
        painter.setPen(QPen(alpha(QColor("#8aa9d6"), 110), 2));
        painter.setBrush(alpha(QColor("#6d8db8"), 75));
        painter.drawEllipse(point, 10 * sx, 10 * sy);
    }
    if (bus->linked && bus->adjustablePoints.size() == 2) {
        const auto first = impl_->pointToScreen(bus->adjustablePoints[0]);
        const auto second = impl_->pointToScreen(bus->adjustablePoints[1]);
        painter.setPen(QPen(QColor("#5bd6ff"), 3));
        painter.drawLine(first, second);
    }
    for (int index = 0; index < static_cast<int>(bus->adjustablePoints.size()); ++index) {
        const auto point = impl_->pointToScreen(bus->adjustablePoints[index]);
        const auto radius = impl_->pointCoverageRadius(*bus, index);
        const auto isSelected = index == selectedPoint;
        painter.setPen(QPen(isSelected ? QColor("#79e3ff") : alpha(QColor("#55bde9"), 115),
                            isSelected ? 2.5 : 1.2));
        painter.setBrush(alpha(QColor("#168dff"), isSelected ? 28 : 9));
        painter.drawEllipse(point, radius, radius);
        if (isSelected) {
            painter.setPen(QPen(QColor("#d8f7ff"), 2));
            painter.setBrush(QColor("#168dff"));
            painter.drawEllipse(QPointF(point.x() + radius, point.y()), 4.5 * sx, 4.5 * sy);
            painter.setPen(QPen(alpha(QColor("#7ee8ff"), 100), 6));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(point, 13 * sx, 13 * sy);
        }
        painter.setPen(QPen(isSelected ? QColor("#e8fbff") : QColor("#9dc3ff"), isSelected ? 3.5 : 2.5));
        painter.setBrush(isSelected ? QColor("#42a8ff") : QColor("#2e9dff"));
        painter.drawEllipse(point, isSelected ? 10 * sx : 9 * sx, isSelected ? 10 * sy : 9 * sy);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ffffff"));
    painter.drawEllipse(circle.center(), 4, 4);

    panel(painter, impl_->parametersPanel(), QColor("#2b4662"));
    text(painter, impl_->scaled(870, 101, 120, 25), QStringLiteral("参数设置"), label, 15 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
    text(painter, impl_->scaled(870, 127, 65, 24), QStringLiteral("Link"), secondary, 13 * sy, Qt::AlignLeft | Qt::AlignVCenter);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bus->linked ? blue : QColor("#263e53"));
    painter.drawRoundedRect(impl_->linkButton(), 12, 12);
    painter.setBrush(QColor("#ffffff"));
    painter.drawEllipse(QPointF(bus->linked ? impl_->linkButton().right() - 11 * sx : impl_->linkButton().left() + 11 * sx,
                                impl_->linkButton().center().y()), 8 * sx, 8 * sy);

    const auto drawParameter = [&](const QString& name, const QRectF& track, double normalized, const QString& value) {
        text(painter, { track.left(), track.top() - 28 * sy, track.width(), 24 * sy }, name, secondary, 13 * sy,
              Qt::AlignLeft | Qt::AlignVCenter);
        painter.setPen(QPen(QColor("#203e55"), 7 * sy, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.left(), track.center().y(), track.right(), track.center().y());
        painter.setPen(QPen(cyan, 7 * sy, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.left(), track.center().y(), track.left() + track.width() * normalized, track.center().y());
        painter.setPen(QPen(QColor("#68a7ff"), 4 * sx));
        painter.drawLine(track.left() + track.width() * normalized, track.top(), track.left() + track.width() * normalized, track.bottom());
        text(painter, { track.left(), track.bottom()+2*sy, track.width(), 20*sy }, value, QColor("#9bc4ff"), 12 * sy);
    };
    drawParameter(QStringLiteral("宽度"), impl_->widthTrack(), bus->width, QStringLiteral("%1%").arg(qRound(bus->width * 100.0)));
    drawParameter(QStringLiteral("角度"), impl_->angleTrack(), (bus->angleDegrees + 180.0) / 360.0, QStringLiteral("%1°").arg(qRound(bus->angleDegrees)));
    drawParameter(selectedPoint >= 0 ? QStringLiteral("点%1覆盖").arg(selectedPoint + 1) : QStringLiteral("覆盖范围"),
                  impl_->coverageTrack(), selectedCoverage / 360.0,
                  QStringLiteral("%1°").arg(qRound(selectedCoverage)));
    panel(painter, impl_->roomPanel(), QColor("#2b4662"));
    const auto roomPoint=[&](qreal x,qreal y){return impl_->scaled(x,y,0,0).topLeft();};
    painter.setPen(QPen(QColor("#25445d"),sx));
    for(int i=0;i<7;++i)painter.drawLine(roomPoint(1030+i*46,438),roomPoint(1172+(i-3)*13,254));
    for(int i=0;i<6;++i)painter.drawLine(roomPoint(1026,270+i*30),roomPoint(1316,270+i*30));
    QPainterPath room;room.moveTo(roomPoint(1172,135));room.lineTo(roomPoint(1280,190));room.lineTo(roomPoint(1244,262));room.lineTo(roomPoint(1100,262));room.lineTo(roomPoint(1064,190));room.closeSubpath();
    painter.setPen(Qt::NoPen);painter.setBrush(QColor(18,113,207,95));painter.drawPath(room);
    const std::array<QPointF,8> speakers {roomPoint(1080,188),roomPoint(1264,188),roomPoint(1172,148),roomPoint(1172,392),roomPoint(1064,360),roomPoint(1280,360),roomPoint(1100,400),roomPoint(1244,400)};
    const std::array<QString,8> speakerLabels {QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
    const auto speakerCount=Common::auxLayoutChannelLabels(bus->targetLayout).size();
    for(int i=0;i<speakerCount;++i){const auto point=speakers[i];painter.setPen(QPen(QColor("#90b5df"),1));painter.setBrush(QColor("#314b68"));painter.drawRoundedRect(QRectF(point.x()-10*sx,point.y()-10*sy,20*sx,24*sy),3,3);text(painter,QRectF(point.x()-21*sx,point.y()-33*sy,42*sx,19*sy),speakerLabels[i],label,12*sy);}
    painter.setPen(Qt::NoPen);painter.setBrush(QColor("#a2bed7"));painter.drawEllipse(impl_->scaled(1157,230,30,45));

    panel(painter, impl_->sendsPanel(), QColor("#2b4662"));
    text(painter, impl_->scaled(420, 476, 260, 32), QStringLiteral("分控推子"), label, 18 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
    text(painter,impl_->scaled(848,478,75,28),QStringLiteral("通道模式"),secondary,14*sy,Qt::AlignLeft|Qt::AlignVCenter);
    detailButton(impl_->sendsModeButton(),Common::auxLayoutName(bus->targetLayout)+QStringLiteral(" ▾"));
    text(painter,impl_->scaled(1055,477,87,34),QStringLiteral("全通道链接"),secondary,13*sy,Qt::AlignLeft|Qt::AlignVCenter);
    toggle(painter,impl_->sendsLinkButton(),bus->sendsLinked);
    painter.setPen(QPen(QColor("#355f8e"), 1));
    painter.setBrush(QColor("#172d43"));
    painter.drawRoundedRect(impl_->resetSendsButton(), 5, 5);
    text(painter, impl_->resetSendsButton(), QStringLiteral("重置所有"), label, 11 * sy);
    const std::array<QString,8> labels {QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
    for (int index = 0; index < 8; ++index) {
        const bool enabled=index<static_cast<int>(bus->sends.size());
        painter.save();if(!enabled)painter.setOpacity(.32);
        const auto* send=enabled?&bus->sends[index]:nullptr;
        const auto strip = impl_->sendStrip(index);
        painter.setPen(QPen(QColor("#2b4662"), 1));
        painter.setBrush(QColor("#102235"));
        painter.drawRoundedRect(strip, 5, 5);
        text(painter, { strip.left() + 5 * sx, strip.top() + 6 * sy, strip.width() - 10 * sx, 24 * sy },
               labels[index], label, 16 * sy,Qt::AlignCenter,true);
        const auto track = impl_->sendTrack(index);
        painter.setPen(QPen(QColor("#7198bd"), 2 * sx));
        painter.drawLine(track.center().x(), track.top(), track.center().x(), track.bottom());
        const auto y = track.top() + track.height() * positionFromDb(send?send->levelDb:-2.0);
        const auto ticks = std::array { 12.0, 0.0, -24.0, -48.0, -72.0 };
        for (const auto tick : ticks) {
            const auto tickY = track.top() + track.height() * positionFromDb(tick);
            text(painter, { strip.left() + 2 * sx, tickY - 7 * sy, 25*sx, 14 * sy },
                  tick > 0.0 ? QStringLiteral("+12") : QString::number(qRound(tick)), secondary, 8 * sy,
                  Qt::AlignRight | Qt::AlignVCenter);
        }
        const auto meter = impl_->meterTrack(index);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor("#06131f"));painter.drawRoundedRect(meter,2,2);
        const auto meterDb=send?send->meterDb:-72.0;
        const auto litTop=meter.bottom()-meter.height()*std::clamp((meterDb+72.0)/84.0,0.0,1.0);
        const auto meterSegment=[&](double low,double high,QColor color){
            const auto top=std::max(litTop,meter.bottom()-meter.height()*(high+72.0)/84.0);
            const auto bottom=meter.bottom()-meter.height()*(low+72.0)/84.0;
            if(bottom>top){painter.setBrush(color);painter.drawRect(QRectF(meter.left()+3*sx,top,meter.width()-6*sx,bottom-top));}
        };
        meterSegment(-72,-12,QColor("#17d675"));meterSegment(-12,0,QColor("#f2df48"));meterSegment(0,12,QColor("#f2484d"));
        painter.setPen(QPen(QColor("#b6d6f1"), 1));
        painter.setBrush(index<2?QColor("#64c8fa"):QColor("#b2c3e8"));
        const QRectF thumb(track.center().x() - 12 * sx, y - 17 * sy, 24 * sx, 34 * sy);
        painter.drawRoundedRect(thumb, 4, 4);
        painter.setPen(QPen(QColor("#315d75"), 2 * sy));
        painter.drawLine(thumb.left() + 3 * sx, thumb.center().y(), thumb.right() - 3 * sx, thumb.center().y());
        text(painter,{strip.left()+4*sx,strip.bottom()-69*sy,strip.width()-8*sx,27*sy},dbText(send?send->levelDb:-2.0),label,11*sy);
        const QRectF mute(strip.left()+7*sx,strip.bottom()-38*sy,(strip.width()-21*sx)/2,30*sy);
        const QRectF solo(mute.right()+7*sx,strip.bottom()-38*sy,(strip.width()-21*sx)/2,30*sy);
        const QColor muteAccent("#ff4b62"),soloAccent("#ffae2d");
        auto muteFill=muteAccent;muteFill.setAlpha(send&&send->mute?85:25);
        auto soloFill=soloAccent;soloFill.setAlpha(send&&send->solo?85:25);
        painter.setPen(QPen(muteAccent,1));painter.setBrush(muteFill);
        painter.drawRoundedRect(mute, 4, 4);
        painter.setPen(QPen(soloAccent,1));painter.setBrush(soloFill);
        painter.drawRoundedRect(solo, 4, 4);
        text(painter, mute, QStringLiteral("M"), QColor("#f7f8ff"), 14 * sy, Qt::AlignCenter, true);
        text(painter, solo, QStringLiteral("S"), QColor("#f7f8ff"), 14 * sy, Qt::AlignCenter, true);
        painter.restore();
    }
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
    if (impl_->scaled(420,52,80,34).contains(position)) {impl_->threeD=false;update();return;}
    if (impl_->scaled(511,52,80,34).contains(position)||impl_->scaled(1022,52,108,34).contains(position)) {impl_->threeD=true;update();return;}
    if (impl_->scaled(611,52,165,34).contains(position)
        || impl_->sendsModeButton().contains(position)) {
        QMenu menu(this);
        const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                   Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
        for(const auto layout:layouts){auto* action=menu.addAction(Common::auxLayoutName(layout));action->setCheckable(true);action->setChecked(bus->targetLayout==layout);
            connect(action,&QAction::triggered,this,[this,id=bus->id,layout]{if(impl_->state->setBusLayout(id,layout)){if(impl_->selfAuxId==id)impl_->sourceLayout=layout;impl_->changed();}});}
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (impl_->resetButton().contains(position)) {
        impl_->state->resetBus(impl_->sourceKey, bus->id, impl_->selfAuxId);
        impl_->changed();
        return;
    }
    if (impl_->resetSendsButton().contains(position)) {
        for (int index = 0; index < static_cast<int>(bus->sends.size()); ++index)
            bus->sends[index].levelDb = index < 2 ? 0.0 : -6.0;
        impl_->changed();
        return;
    }
    if (impl_->sendsLinkButton().contains(position)) {
        bus->sendsLinked = !bus->sendsLinked;
        impl_->changed();
        return;
    }
    if (impl_->linkButton().contains(position)) {
        bus->linked = !bus->linked;
        impl_->changed();
        return;
    }
    if (impl_->directivityButton().contains(position)) {
        bus->showDirectivity = !bus->showDirectivity;
        impl_->changed();
        return;
    }
    const auto parameterTracks = std::array { impl_->widthTrack(), impl_->angleTrack(), impl_->coverageTrack() };
    for (int index = 0; index < static_cast<int>(parameterTracks.size()); ++index) {
        if (parameterTracks[index].adjusted(-5, -8, 5, 8).contains(position)) {
            impl_->draggedParameter = index;
            mouseMoveEvent(event);
            return;
        }
    }
    for (int index = 0; index < static_cast<int>(bus->adjustablePoints.size()); ++index) {
        if (QLineF(position, impl_->pointToScreen(bus->adjustablePoints[index])).length() <= 15.0) {
            impl_->selectedPoint = index;
            impl_->draggedPoint = index;
            impl_->changed();
            return;
        }
    }
    for (int index = 0; index < static_cast<int>(bus->adjustablePoints.size()); ++index) {
        const auto point = impl_->pointToScreen(bus->adjustablePoints[index]);
        const auto distance = QLineF(position, point).length();
        if (std::abs(distance - impl_->pointCoverageRadius(*bus, index)) <= 7.0) {
            impl_->selectedPoint = index;
            impl_->draggedCoveragePoint = index;
            impl_->changed();
            return;
        }
    }
    if (bus->linked && bus->adjustablePoints.size() == 2) {
        const QLineF link(impl_->pointToScreen(bus->adjustablePoints[0]), impl_->pointToScreen(bus->adjustablePoints[1]));
        const auto denominator = link.length() * link.length();
        const auto t = denominator <= 0.0 ? 0.0 : std::clamp(QPointF::dotProduct(position - link.p1(), link.p2() - link.p1()) / denominator, 0.0, 1.0);
        if (QLineF(position, link.pointAt(t)).length() <= 12.0) {
            impl_->draggingLink = true;
            impl_->dragStart = position;
            impl_->dragStartWidth = bus->width;
            return;
        }
    }
    for (int index = 0; index < static_cast<int>(bus->sends.size()); ++index) {
        const auto strip = impl_->sendStrip(index);
        const auto track = impl_->sendTrack(index).adjusted(-14, -10, 14, 10);
        const QRectF mute(strip.left()+7*impl_->scaleX(),strip.bottom()-38*impl_->scaleY(),(strip.width()-21*impl_->scaleX())/2,30*impl_->scaleY());
        const QRectF solo(mute.right()+7*impl_->scaleX(),strip.bottom()-38*impl_->scaleY(),(strip.width()-21*impl_->scaleX())/2,30*impl_->scaleY());
        if (mute.contains(position)) {
            bus->sends[index].mute = !bus->sends[index].mute;
            impl_->changed();
            return;
        }
        if (solo.contains(position)) {
            bus->sends[index].solo = !bus->sends[index].solo;
            impl_->changed();
            return;
        }
        if (track.contains(position)) {
            impl_->draggedSend = index;
            impl_->draggedSendLevels.clear();
            impl_->draggedSendLevels.reserve(bus->sends.size());
            for (const auto& send : bus->sends)
                impl_->draggedSendLevels.push_back(send.levelDb);
            mouseMoveEvent(event);
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
    if (impl_->draggedPoint >= 0 && impl_->draggedPoint < static_cast<int>(bus->adjustablePoints.size())) {
        const auto old = bus->adjustablePoints[impl_->draggedPoint];
        const auto next = impl_->screenToPoint(event->position());
        if (bus->linked && bus->adjustablePoints.size() == 2) {
            const auto delta = next - old;
            for (auto& point : bus->adjustablePoints)
                point = impl_->screenToPoint(impl_->pointToScreen(point + delta));
        } else {
            bus->adjustablePoints[impl_->draggedPoint] = next;
        }
        impl_->changed();
        return;
    }
    if (impl_->draggedCoveragePoint >= 0
        && impl_->draggedCoveragePoint < static_cast<int>(bus->adjustablePoints.size())) {
        const auto point = impl_->pointToScreen(bus->adjustablePoints[impl_->draggedCoveragePoint]);
        const auto scale = std::min(impl_->scaleX(), impl_->scaleY());
        const auto radius = QLineF(point, event->position()).length() / scale;
        impl_->setPointCoverage(*bus, impl_->draggedCoveragePoint, (radius - 22.0) / 38.0 * 360.0);
        impl_->changed();
        return;
    }
    if (impl_->draggingLink && bus->adjustablePoints.size() == 2) {
        impl_->setLinkedWidth(*bus, impl_->dragStartWidth
            + (event->position().x() - impl_->dragStart.x()) / (impl_->pannerCircle().width() * .5));
        impl_->changed();
        return;
    }
    if (impl_->draggedSend >= 0 && impl_->draggedSend < static_cast<int>(bus->sends.size())) {
        const auto track = impl_->sendTrack(impl_->draggedSend);
        const auto position = std::clamp((event->position().y() - track.top()) / track.height(), 0.0, 1.0);
        const auto targetLevel = std::max(-72.01, dbFromPosition(position));
        if (bus->sendsLinked
            && impl_->draggedSendLevels.size() == bus->sends.size()) {
            const auto deltaDb = targetLevel - impl_->draggedSendLevels[impl_->draggedSend];
            for (int index = 0; index < static_cast<int>(bus->sends.size()); ++index) {
                bus->sends[index].levelDb = std::clamp(impl_->draggedSendLevels[index] + deltaDb,
                                                       -72.01, 12.0);
            }
        } else {
            bus->sends[impl_->draggedSend].levelDb = targetLevel;
        }
        impl_->changed();
        return;
    }
    if (impl_->draggedParameter >= 0) {
        const auto tracks = std::array { impl_->widthTrack(), impl_->angleTrack(), impl_->coverageTrack() };
        const auto track = tracks[impl_->draggedParameter];
        const auto normalized = std::clamp((event->position().x() - track.left()) / track.width(), 0.0, 1.0);
        if (impl_->draggedParameter == 0)
            impl_->setLinkedWidth(*bus, normalized);
        else if (impl_->draggedParameter == 1)
            impl_->setAngle(*bus, normalized * 360.0 - 180.0);
        else
            impl_->setPointCoverage(*bus, impl_->effectiveSelectedPoint(), normalized * 360.0);
        impl_->changed();
    }
}

void AuxSettingsWindow::mouseReleaseEvent(QMouseEvent*)
{
    impl_->listScrollbarDragging = false;
    impl_->draggedPoint = -1;
    impl_->draggedCoveragePoint = -1;
    impl_->draggedSend = -1;
    impl_->draggedSendLevels.clear();
    impl_->draggedListBusId = 0;
    impl_->draggedListLevels.clear();
    impl_->draggedParameter = -1;
    impl_->draggingLink = false;
}

void AuxSettingsWindow::wheelEvent(QWheelEvent* event)
{
    if (auto* bus = impl_->selected(); bus != nullptr && impl_->pannerPanel().contains(event->position())) {
        for (int index = 0; index < static_cast<int>(bus->adjustablePoints.size()); ++index) {
            const auto point = impl_->pointToScreen(bus->adjustablePoints[index]);
            const auto distance = QLineF(event->position(), point).length();
            const auto radius = impl_->pointCoverageRadius(*bus, index);
            if (distance <= 16.0 || std::abs(distance - radius) <= 7.0) {
                impl_->selectedPoint = index;
                const auto steps = event->angleDelta().y() / 120.0;
                impl_->setPointCoverage(*bus, index, impl_->pointCoverage(*bus, index) + steps * 5.0);
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
