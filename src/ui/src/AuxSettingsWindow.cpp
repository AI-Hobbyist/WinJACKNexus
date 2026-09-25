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
    painter.setBrush(QColor("#0c2030"));
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
    if (value < -72.0)
        return 1.0;
    return std::clamp((12.0 - value) / 84.0 * .95, 0.0, .95);
}

double dbFromPosition(double position)
{
    if (position > .97)
        return -100.0;
    return 12.0 - std::clamp(position / .95, 0.0, 1.0) * 84.0;
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
        return std::min(owner.width() / 1360.0, owner.height() / 768.0);
    }

    qreal scaleX() const { return contentScale(); }
    qreal scaleY() const { return contentScale(); }
    QPointF contentOrigin() const
    {
        const auto scale = contentScale();
        return { (owner.width() - 1360.0 * scale) * .5,
                 (owner.height() - 768.0 * scale) * .5 };
    }

    QRectF scaled(qreal x, qreal y, qreal width, qreal height) const
    {
        const auto scale = contentScale();
        const auto origin = contentOrigin();
        return { origin.x() + x * scale, origin.y() + y * scale,
                 width * scale, height * scale };
    }

    QRectF leftPanel() const { return scaled(5, 5, 390, 758); }
    QRectF addButton() const { return scaled(248, 12, 102, 38); }
    QRectF masterToggle() const { return scaled(202, 18, 40, 24); }
    QRectF removeButton() const { return scaled(356, 12, 31, 38); }
    QRectF listViewport() const { return scaled(10, 58, 380, 698); }
    QRectF listScrollbarTrack() const { return scaled(376, 62, 8, 690); }
    QRectF topBar() const { return scaled(406, 5, 929, 47); }
    QRectF pannerPanel() const { return scaled(406, 60, 510, 381); }
    QRectF parametersPanel() const { return scaled(928, 60, 397, 381); }
    QRectF sendsPanel() const { return scaled(406, 450, 919, 294); }
    QRectF resetButton() const { return scaled(1282, 12, 45, 34); }
    QRectF sendsLinkButton() const { return scaled(802, 456, 72, 32); }
    QRectF sendsSplitButton() const { return scaled(876, 456, 72, 32); }
    QRectF resetSendsButton() const { return scaled(1212, 456, 96, 32); }
    QRectF linkButton() const { return scaled(1034, 151, 44, 24); }
    QRectF directivityButton() const { return scaled(772, 70, 42, 22); }
    QRectF widthTrack() const { return scaled(1035, 202, 172, 16); }
    QRectF angleTrack() const { return scaled(1035, 239, 172, 16); }
    QRectF coverageTrack() const { return scaled(1035, 276, 172, 16); }

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
        return std::max(0.0, buses().size() * 79.0 - 698.0);
    }

    QRectF listScrollbarThumb() const
    {
        const auto track = listScrollbarTrack();
        const auto contentHeight = std::max(698.0, buses().size() * 79.0);
        const auto thumbHeight = std::max(44.0 * scaleY(), track.height() * 698.0 / contentHeight);
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
        const auto area = pannerPanel().adjusted(48 * scaleX(), 42 * scaleY(), -48 * scaleX(), -20 * scaleY());
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
        const auto* bus = selected();
        const auto count = bus == nullptr ? 1 : std::max(1, static_cast<int>(bus->sends.size()));
        const auto body = sendsPanel().adjusted(10 * scaleX(), 43 * scaleY(), -10 * scaleX(), -10 * scaleY());
        const auto width = body.width() / count;
        return { body.left() + index * width + 3, body.top(), width - 6, body.height() };
    }

    QRectF sendTrack(int index) const
    {
        const auto strip = sendStrip(index);
        return { strip.center().x() - 13 * scaleX(), strip.top() + 73 * scaleY(),
                 7 * scaleX(), strip.height() - 90 * scaleY() };
    }

    QRectF meterTrack(int index) const
    {
        const auto fader = sendTrack(index);
        return { fader.right() + 8 * scaleX(), fader.top(), 5 * scaleX(), fader.height() };
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
    resize(1360, 768);
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
    painter.fillRect(rect(), QColor("#061725"));
    const auto sx = impl_->scaleX();
    const auto sy = impl_->scaleY();
    const auto label = QColor("#d7e8ff");
    const auto secondary = QColor("#8fa9c6");
    const auto cyan = QColor("#2fc5ff");
    const auto blue = QColor("#168dff");

    panel(painter, impl_->leftPanel(), QColor("#087ab5"));
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

    panel(painter, impl_->topBar());
    text(painter, impl_->scaled(420, 8, 45, 40), QStringLiteral("⏻"), cyan, 27 * sy, Qt::AlignCenter, true);
    text(painter, impl_->scaled(465, 8, 270, 40), QStringLiteral("AUX %1 · %2   ✎").arg(bus->id).arg(bus->name),
         label, 18 * sy, Qt::AlignLeft | Qt::AlignVCenter, true);
    text(painter, impl_->scaled(740, 12, 530, 34),
         QStringLiteral("源：%1 (%2) → 目标：AUX %3 (%4)")
             .arg(impl_->sourceName, Common::auxLayoutName(impl_->sourceLayout))
             .arg(bus->id).arg(Common::auxLayoutName(bus->targetLayout)),
         secondary, 11 * sy, Qt::AlignRight | Qt::AlignVCenter);
    painter.setBrush(QColor("#172d43"));
    painter.drawRoundedRect(impl_->resetButton(), 5, 5);
    text(painter, impl_->resetButton(), QStringLiteral("重置"), label, 11 * sy);

    panel(painter, impl_->pannerPanel());
    text(painter, impl_->scaled(420, 66, 200, 30), QStringLiteral("空间声像器"), label, 18 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
        text(painter, impl_->scaled(665, 68, 101, 26), QStringLiteral("显示指向性图"), secondary, 12 * sy,
            Qt::AlignRight | Qt::AlignVCenter);
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

    panel(painter, impl_->parametersPanel());
    text(painter, impl_->scaled(945, 68, 180, 30), QStringLiteral("参数设置"), label, 18 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
    text(painter, impl_->scaled(945, 107, 85, 28), QStringLiteral("目标布局"), secondary, 13 * sy, Qt::AlignLeft | Qt::AlignVCenter);
    text(painter, impl_->scaled(1032, 103, 275, 38), Common::auxLayoutName(bus->targetLayout), label,
         13 * sy, Qt::AlignLeft | Qt::AlignVCenter);
    text(painter, impl_->scaled(945, 151, 80, 24), QStringLiteral("Link"), secondary, 13 * sy, Qt::AlignLeft | Qt::AlignVCenter);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bus->linked ? blue : QColor("#263e53"));
    painter.drawRoundedRect(impl_->linkButton(), 12, 12);
    painter.setBrush(QColor("#ffffff"));
    painter.drawEllipse(QPointF(bus->linked ? impl_->linkButton().right() - 11 * sx : impl_->linkButton().left() + 11 * sx,
                                impl_->linkButton().center().y()), 8 * sx, 8 * sy);
    text(painter, impl_->scaled(1182, 146, 55, 30), QStringLiteral("🔗"), cyan, 20 * sy);

    const auto drawParameter = [&](const QString& name, const QRectF& track, double normalized, const QString& value) {
        text(painter, { track.left() - 90 * sx, track.top() - 7 * sy, 80 * sx, 30 * sy }, name, secondary, 13 * sy,
             Qt::AlignLeft | Qt::AlignVCenter);
        painter.setPen(QPen(QColor("#203e55"), 7 * sy, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.left(), track.center().y(), track.right(), track.center().y());
        painter.setPen(QPen(cyan, 7 * sy, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.left(), track.center().y(), track.left() + track.width() * normalized, track.center().y());
        painter.setPen(QPen(QColor("#68a7ff"), 4 * sx));
        painter.drawLine(track.left() + track.width() * normalized, track.top(), track.left() + track.width() * normalized, track.bottom());
        text(painter, { track.right() + 16 * sx, track.top() - 7 * sy, 70 * sx, 30 * sy }, value, QColor("#9bc4ff"), 13 * sy,
             Qt::AlignLeft | Qt::AlignVCenter);
    };
    drawParameter(QStringLiteral("宽度"), impl_->widthTrack(), bus->width, QStringLiteral("%1%").arg(qRound(bus->width * 100.0)));
    drawParameter(QStringLiteral("角度"), impl_->angleTrack(), (bus->angleDegrees + 180.0) / 360.0, QStringLiteral("%1°").arg(qRound(bus->angleDegrees)));
    drawParameter(selectedPoint >= 0 ? QStringLiteral("点%1覆盖").arg(selectedPoint + 1) : QStringLiteral("覆盖范围"),
                  impl_->coverageTrack(), selectedCoverage / 360.0,
                  QStringLiteral("%1°").arg(qRound(selectedCoverage)));
    panel(painter, impl_->scaled(944, 304, 365, 119), QColor("#12466a"));
    text(painter, impl_->scaled(958, 313, 32, 28), QStringLiteral("ⓘ"), cyan, 20 * sy);
    text(painter, impl_->scaled(997, 310, 294, 103),
         QStringLiteral("• 点击实心点选择当前点\n• 拖动点移动，拖动外圈调覆盖\n• 点或外圈上滚轮调覆盖范围\n• 半透明点为目标参考点"),
         QColor("#8fb5e2"), 12 * sy, Qt::AlignLeft | Qt::AlignVCenter);

    panel(painter, impl_->sendsPanel());
    text(painter, impl_->scaled(420, 456, 260, 32), QStringLiteral("分声道发送强度"), label, 18 * sy,
         Qt::AlignLeft | Qt::AlignVCenter, true);
        painter.setPen(QPen(QColor("#355f8e"), 1));
        painter.setBrush(bus->sendsLinked ? QColor("#087ee4") : QColor("#172d43"));
        painter.drawRoundedRect(impl_->sendsLinkButton(), 5, 5);
        painter.setBrush(!bus->sendsLinked ? QColor("#087ee4") : QColor("#172d43"));
        painter.drawRoundedRect(impl_->sendsSplitButton(), 5, 5);
        text(painter, impl_->sendsLinkButton(), QStringLiteral("Link"),
            bus->sendsLinked ? QColor("#ffffff") : secondary, 11 * sy, Qt::AlignCenter, true);
        text(painter, impl_->sendsSplitButton(), QStringLiteral("Split"),
            !bus->sendsLinked ? QColor("#ffffff") : secondary, 11 * sy, Qt::AlignCenter, true);
    text(painter, impl_->scaled(1020, 456, 182, 32), QStringLiteral("范围：-72 ~ +12 dB"), secondary, 12 * sy,
         Qt::AlignRight | Qt::AlignVCenter);
    painter.setPen(QPen(QColor("#355f8e"), 1));
    painter.setBrush(QColor("#172d43"));
    painter.drawRoundedRect(impl_->resetSendsButton(), 5, 5);
    text(painter, impl_->resetSendsButton(), QStringLiteral("重置所有"), label, 11 * sy);
    for (int index = 0; index < static_cast<int>(bus->sends.size()); ++index) {
        const auto& send = bus->sends[index];
        const auto strip = impl_->sendStrip(index);
        painter.setPen(QPen(QColor("#183a53"), 1));
           painter.setBrush(QColor("#0c2940"));
        painter.drawRoundedRect(strip, 5, 5);
        text(painter, { strip.left() + 8 * sx, strip.top() + 7 * sy, strip.width() - 16 * sx, 20 * sy },
               send.name, cyan, 12 * sy,
             Qt::AlignLeft | Qt::AlignVCenter, true);
        text(painter, { strip.left() + 10 * sx, strip.top() + 35 * sy, strip.width() - 20 * sx, 24 * sy },
             dbText(send.levelDb), label, 11 * sy);
        const auto track = impl_->sendTrack(index);
        painter.setPen(QPen(QColor("#020b11"), 7 * sx, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.center().x(), track.top(), track.center().x(), track.bottom());
        const auto y = track.top() + track.height() * positionFromDb(send.levelDb);
        painter.setPen(QPen(cyan, 2 * sx));
        painter.drawLine(track.center().x(), track.bottom(), track.center().x(), y);
        const auto ticks = std::array { 12.0, 0.0, -24.0, -48.0, -72.0 };
        for (const auto tick : ticks) {
            const auto tickY = track.top() + track.height() * positionFromDb(tick);
            painter.setPen(QPen(QColor("#31516b"), 1));
            painter.drawLine(track.left() - 3 * sx, tickY, track.left() + 2 * sx, tickY);
            text(painter, { strip.left() + 2 * sx, tickY - 7 * sy, std::max(18.0, track.left() - strip.left() - 7 * sx), 14 * sy },
                 tick > 0.0 ? QStringLiteral("+12") : QString::number(qRound(tick)), secondary, 8 * sy,
                 Qt::AlignRight | Qt::AlignVCenter);
        }
        painter.setPen(QPen(QColor("#79dfff"), 1));
        painter.setBrush(QColor("#45c9f6"));
        const QRectF thumb(track.center().x() - 10 * sx, y - 17 * sy, 20 * sx, 34 * sy);
        painter.drawRoundedRect(thumb, 4, 4);
        painter.setPen(QPen(QColor("#0b6387"), 1.5 * sy));
        painter.drawLine(thumb.left() + 3 * sx, thumb.center().y(), thumb.right() - 3 * sx, thumb.center().y());
        const auto meter = impl_->meterTrack(index);
        painter.setPen(QPen(QColor("#061019"), 5 * sx, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(meter.center().x(), meter.top(), meter.center().x(), meter.bottom());
        const auto meterY = meter.bottom() - std::clamp((send.meterDb + 72.0) / 84.0, 0.0, 1.0) * meter.height();
        painter.setPen(QPen(send.meterDb > 0.0 ? QColor("#ff5757") : send.meterDb > -12.0 ? QColor("#ffe05b") : QColor("#25dc70"),
                            3 * sx, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(meter.center().x(), meter.bottom(), meter.center().x(), meterY);
        const QRectF mute(strip.right() - 35 * sx, strip.top() + 93 * sy, 27 * sx, 32 * sy);
        const QRectF solo(strip.right() - 35 * sx, strip.top() + 134 * sy, 27 * sx, 32 * sy);
        painter.setPen(QPen(QColor("#31516b"), 1));
        painter.setBrush(send.mute ? QColor("#8b2834") : QColor("#172d40"));
        painter.drawRoundedRect(mute, 4, 4);
        painter.setBrush(send.solo ? QColor("#875f18") : QColor("#172d40"));
        painter.drawRoundedRect(solo, 4, 4);
        text(painter, mute, QStringLiteral("M"), label, 12 * sy, Qt::AlignCenter, true);
        text(painter, solo, QStringLiteral("S"), label, 12 * sy, Qt::AlignCenter, true);
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
    if (impl_->scaled(1032, 103, 275, 38).contains(position)) {
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
        bus->sendsLinked = true;
        impl_->changed();
        return;
    }
    if (impl_->sendsSplitButton().contains(position)) {
        bus->sendsLinked = false;
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
        const QRectF mute(strip.right() - 35 * impl_->scaleX(), strip.top() + 93 * impl_->scaleY(), 27 * impl_->scaleX(), 32 * impl_->scaleY());
        const QRectF solo(strip.right() - 35 * impl_->scaleX(), strip.top() + 134 * impl_->scaleY(), 27 * impl_->scaleX(), 32 * impl_->scaleY());
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
