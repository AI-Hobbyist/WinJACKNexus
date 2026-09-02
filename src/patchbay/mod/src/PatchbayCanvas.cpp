#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QApplication>
#include <QFile>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace {

using namespace WinJACKNexus::Patchbay::Core;

constexpr qreal kNodeWidth = 220.0;
constexpr qreal kHeaderHeight = 34.0;
constexpr qreal kPortHeight = 26.0;

struct CanvasTheme final {
    QColor background;
    QColor nodeBackground;
    QColor nodeHeader;
    QColor nodeBorder;
    QColor nodeSelectedBorder;
    QColor text;
    QColor separator;
    QColor audio;
    QColor midi;
    QColor cv;
    QColor osc;
    QColor unknown;
};

QString themeValue(const QString& stylesheet, const QString& name)
{
    const QRegularExpression expression(QStringLiteral(R"(/\*\s*%1\s*:\s*(#[0-9a-fA-F]{6})\s*\*/)").arg(name));
    const auto match = expression.match(stylesheet);
    return match.hasMatch() ? match.captured(1) : QString();
}

CanvasTheme loadCanvasTheme(const QString& stylesheet, bool light)
{
    const QString prefix = light ? QStringLiteral("light") : QStringLiteral("dark");
    const auto color = [&stylesheet, &prefix](const QString& name) {
        return QColor(themeValue(stylesheet, prefix + QLatin1Char('.') + name));
    };
    return { color(QStringLiteral("canvas")), color(QStringLiteral("node")), color(QStringLiteral("header")),
             color(QStringLiteral("border")), color(QStringLiteral("selected-border")), color(QStringLiteral("text")),
             color(QStringLiteral("separator")), color(QStringLiteral("audio")), color(QStringLiteral("midi")),
             color(QStringLiteral("cv")), color(QStringLiteral("osc")), color(QStringLiteral("unknown")) };
}

CanvasTheme& activeCanvasTheme()
{
    static CanvasTheme theme;
    return theme;
}

QColor portColor(PortKind kind)
{
    const auto& theme = activeCanvasTheme();
    switch (kind) {
    case PortKind::audio:
        return theme.audio;
    case PortKind::midi:
        return theme.midi;
    case PortKind::cv:
        return theme.cv;
    case PortKind::osc:
        return theme.osc;
    default:
        return theme.unknown;
    }
}

class CanvasNode final : public QGraphicsObject {
public:
    CanvasNode(ClientInfo client, std::vector<PortInfo> ports)
        : client_(std::move(client)), ports_(std::move(ports))
    {
        std::sort(ports_.begin(), ports_.end(), [](const PortInfo& left, const PortInfo& right) {
            if (left.direction != right.direction)
                return left.direction == Direction::input;
            return left.ordinal < right.ordinal;
        });
        setFlag(ItemIsSelectable);
        setToolTip(QString::fromStdString(client_.name));
    }

    QRectF boundingRect() const override
    {
        return { 0.0, 0.0, kNodeWidth, kHeaderHeight + std::max<std::size_t>(1, ports_.size()) * kPortHeight };
    }

    QPointF portPosition(const std::string& portId) const
    {
        const auto port = std::find_if(ports_.cbegin(), ports_.cend(), [&portId](const PortInfo& item) {
            return item.id == portId;
        });
        if (port == ports_.cend())
            return scenePos();

        const auto index = static_cast<qreal>(std::distance(ports_.cbegin(), port));
        const qreal x = port->direction == Direction::input ? 0.0 : kNodeWidth;
        return mapToScene({ x, kHeaderHeight + (index + 0.5) * kPortHeight });
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const auto& theme = activeCanvasTheme();
        const auto bounds = boundingRect();
        const bool selected = isSelected();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(selected ? theme.nodeSelectedBorder : theme.nodeBorder, selected ? 2.0 : 1.0));
        painter->setBrush(theme.nodeBackground);
        painter->drawRoundedRect(bounds, 6.0, 6.0);

        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.nodeHeader);
        painter->drawRoundedRect(QRectF(0.0, 0.0, kNodeWidth, kHeaderHeight), 6.0, 6.0);
        painter->drawRect(0.0, kHeaderHeight - 6.0, kNodeWidth, 6.0);

        QFont titleFont = QApplication::font();
        titleFont.setPointSize(10);
        titleFont.setWeight(QFont::DemiBold);
        painter->setPen(theme.text);
        painter->setFont(titleFont);
        const QString title = QString::fromStdString(client_.alias.empty() ? client_.name : client_.alias);
        painter->drawText(QRectF(12.0, 0.0, kNodeWidth - 24.0, kHeaderHeight), Qt::AlignVCenter | Qt::AlignLeft, title);

        QFont portFont = QApplication::font();
        portFont.setPointSize(9);
        painter->setFont(portFont);
        for (std::size_t index = 0; index < ports_.size(); ++index) {
            const auto& port = ports_[index];
            const qreal row = kHeaderHeight + static_cast<qreal>(index) * kPortHeight;
            const bool input = port.direction == Direction::input;
            const QColor color = portColor(port.kind);
            painter->setPen(QPen(theme.separator, 1.0));
            painter->drawLine(QPointF(8.0, row), QPointF(kNodeWidth - 8.0, row));
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            const qreal pinX = input ? 0.0 : kNodeWidth;
            painter->drawEllipse(QPointF(pinX, row + kPortHeight / 2.0), 5.5, 5.5);
            painter->setPen(theme.text);
            const QString name = QString::fromStdString(port.alias.empty() ? port.name : port.alias);
            const QRectF textBounds = input ? QRectF(14.0, row, kNodeWidth - 28.0, kPortHeight)
                                            : QRectF(14.0, row, kNodeWidth - 28.0, kPortHeight);
            painter->drawText(textBounds, Qt::AlignVCenter | (input ? Qt::AlignLeft : Qt::AlignRight), name);
        }
    }

private:
    ClientInfo client_;
    std::vector<PortInfo> ports_;
};

QPainterPath connectionPath(const QPointF& source, const QPointF& destination)
{
    const qreal handle = std::max<qreal>(70.0, std::abs(destination.x() - source.x()) * 0.5);
    QPainterPath path(source);
    path.cubicTo(source + QPointF(handle, 0.0), destination - QPointF(handle, 0.0), destination);
    return path;
}

Snapshot demoSnapshot()
{
    Snapshot snapshot;
    snapshot.clients = {
        { "system", "system", {}, "computer", {}, 70.0, 160.0 },
        { "gain", "Internal Gain", {}, "settings", {}, 390.0, 210.0 },
        { "monitor", "Output Monitor", {}, "audio", {}, 720.0, 160.0 }
    };
    snapshot.ports = {
        { "system:capture_1", "system", "capture_1", {}, PortKind::audio, Direction::output, PortGroupHint::mainOutput, 0 },
        { "system:capture_2", "system", "capture_2", {}, PortKind::audio, Direction::output, PortGroupHint::mainOutput, 1 },
        { "gain:in_l", "gain", "in_l", {}, PortKind::audio, Direction::input, PortGroupHint::mainInput, 0 },
        { "gain:in_r", "gain", "in_r", {}, PortKind::audio, Direction::input, PortGroupHint::mainInput, 1 },
        { "gain:out_l", "gain", "out_l", {}, PortKind::audio, Direction::output, PortGroupHint::mainOutput, 2 },
        { "gain:out_r", "gain", "out_r", {}, PortKind::audio, Direction::output, PortGroupHint::mainOutput, 3 },
        { "monitor:in_l", "monitor", "in_l", {}, PortKind::audio, Direction::input, PortGroupHint::mainInput, 0 },
        { "monitor:in_r", "monitor", "in_r", {}, PortKind::audio, Direction::input, PortGroupHint::mainInput, 1 }
    };
    snapshot.connections = {
        { "system:capture_1", "gain:in_l" },
        { "system:capture_2", "gain:in_r" },
        { "gain:out_l", "monitor:in_l" },
        { "gain:out_r", "monitor:in_r" }
    };
    return snapshot;
}

}

PatchbayCanvas::PatchbayCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
    auto* canvasScene = new QGraphicsScene(this);
    canvasScene->setSceneRect(0.0, 0.0, 1120.0, 680.0);
    setScene(canvasScene);
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setBackgroundBrush(activeCanvasTheme().background);
    setFrameShape(QFrame::NoFrame);
}

void PatchbayCanvas::setSnapshot(const Snapshot& snapshot)
{
    scene()->clear();

    std::unordered_map<std::string, std::vector<PortInfo>> portsByClient;
    for (const auto& port : snapshot.ports) {
        if (port.alive)
            portsByClient[port.clientId].push_back(port);
    }

    std::unordered_map<std::string, CanvasNode*> nodes;
    for (const auto& client : snapshot.clients) {
        if (!client.alive)
            continue;
        auto* node = new CanvasNode(client, portsByClient[client.id]);
        node->setPos(client.x, client.y);
        scene()->addItem(node);
        nodes.emplace(client.id, node);
    }

    std::unordered_map<std::string, const PortInfo*> ports;
    for (const auto& port : snapshot.ports)
        ports.emplace(port.id, &port);

    for (const auto& connection : snapshot.connections) {
        if (!connection.alive)
            continue;
        const auto sourcePort = ports.find(connection.sourcePortId);
        const auto destinationPort = ports.find(connection.destinationPortId);
        if (sourcePort == ports.end() || destinationPort == ports.end())
            continue;
        const auto sourceNode = nodes.find(sourcePort->second->clientId);
        const auto destinationNode = nodes.find(destinationPort->second->clientId);
        if (sourceNode == nodes.end() || destinationNode == nodes.end())
            continue;

        const QColor color = portColor(sourcePort->second->kind);
        auto* line = new QGraphicsPathItem(connectionPath(sourceNode->second->portPosition(connection.sourcePortId),
                                                           destinationNode->second->portPosition(connection.destinationPortId)));
        line->setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap));
        line->setZValue(-1.0);
        scene()->addItem(line);
    }
}

void PatchbayCanvas::loadDemoGraph()
{
    setSnapshot(demoSnapshot());
}

void PatchbayCanvas::applyTheme(bool light)
{
    QFile stylesheet(QStringLiteral(":/patchbay/theme/style.css"));
    if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QString content = QString::fromUtf8(stylesheet.readAll());
    activeCanvasTheme() = loadCanvasTheme(content, light);
    const QString prefix = light ? QStringLiteral("light") : QStringLiteral("dark");
    QString applicationStyle = content;
    for (const auto& name : { QStringLiteral("window"), QStringLiteral("toolbar"), QStringLiteral("text"), QStringLiteral("button-hover"), QStringLiteral("toolbar-border"), QStringLiteral("button-border") })
        applicationStyle.replace(QLatin1Char('$') + name.toUpper().replace(QLatin1Char('-'), QLatin1Char('_')),
                                 themeValue(content, prefix + QLatin1Char('.') + name));
    qApp->setStyleSheet(applicationStyle);

    for (const auto* widget : qApp->topLevelWidgets()) {
        if (auto* canvas = widget->findChild<QGraphicsView*>()) {
            canvas->setBackgroundBrush(activeCanvasTheme().background);
            canvas->scene()->update();
        }
    }
}