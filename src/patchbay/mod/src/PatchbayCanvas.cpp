#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
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
    CanvasNode(ClientInfo client, std::vector<PortInfo> ports, std::function<void(const std::string&)> portActivated)
        : client_(std::move(client)), ports_(std::move(ports)), portActivated_(std::move(portActivated))
    {
        std::sort(ports_.begin(), ports_.end(), [](const PortInfo& left, const PortInfo& right) {
            if (left.direction != right.direction)
                return left.direction == Direction::input;
            return left.ordinal < right.ordinal;
        });
        setFlag(ItemIsSelectable);
        setFlag(ItemIsMovable);
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

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && (event->pos().x() <= 12.0 || event->pos().x() >= kNodeWidth - 12.0)) {
            const auto row = static_cast<std::ptrdiff_t>(std::floor((event->pos().y() - kHeaderHeight) / kPortHeight));
            if (row >= 0 && row < static_cast<std::ptrdiff_t>(ports_.size())) {
                const auto& port = ports_[static_cast<std::size_t>(row)];
                const bool onInputPin = event->pos().x() <= 12.0 && port.direction == Direction::input;
                const bool onOutputPin = event->pos().x() >= kNodeWidth - 12.0 && port.direction == Direction::output;
                if (onInputPin || onOutputPin) {
                    portActivated_(port.id);
                    event->accept();
                    return;
                }
            }
        }
        QGraphicsObject::mousePressEvent(event);
    }

private:
    ClientInfo client_;
    std::vector<PortInfo> ports_;
    std::function<void(const std::string&)> portActivated_;
};

QPainterPath connectionPath(const QPointF& source, const QPointF& destination)
{
    const qreal handle = std::max<qreal>(70.0, std::abs(destination.x() - source.x()) * 0.5);
    QPainterPath path(source);
    path.cubicTo(source + QPointF(handle, 0.0), destination - QPointF(handle, 0.0), destination);
    return path;
}

QSettings patchbaySettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("WinJACKNexus"), QStringLiteral("Patchbay"));
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
    Snapshot restored = snapshot;
    auto settings = patchbaySettings();
    for (auto& client : restored.clients) {
        const auto key = QString::fromStdString(client.id);
        if (settings.contains(QStringLiteral("layout/%1/x").arg(key))) {
            client.x = settings.value(QStringLiteral("layout/%1/x").arg(key)).toDouble();
            client.y = settings.value(QStringLiteral("layout/%1/y").arg(key)).toDouble();
        }
        if (settings.contains(QStringLiteral("layout/%1/split").arg(key)))
            client.split = settings.value(QStringLiteral("layout/%1/split").arg(key)).toBool();
    }
    snapshot_ = std::move(restored);
    scene()->clear();

    std::unordered_map<std::string, std::vector<PortInfo>> portsByClient;
    for (const auto& port : snapshot_.ports) {
        if (port.alive)
            portsByClient[port.clientId].push_back(port);
    }

    std::unordered_map<std::string, CanvasNode*> nodesByPort;
    for (const auto& client : snapshot_.clients) {
        if (!client.alive)
            continue;

        const auto addNode = [this, &client, &portsByClient, &nodesByPort](std::vector<PortInfo> ports, QPointF position, bool positionOwner, const QString& label) {
            ClientInfo displayClient = client;
            displayClient.alias = label.toStdString();
            auto* node = new CanvasNode(std::move(displayClient), std::move(ports), [this](const std::string& portId) {
                activatePort(portId);
            });
            node->setPos(position);
            node->setData(0, QString::fromStdString(client.id));
            node->setData(3, positionOwner);
            scene()->addItem(node);
            for (const auto& port : portsByClient[client.id]) {
                if ((positionOwner && port.direction == Direction::input) || (!positionOwner && port.direction == Direction::output))
                    nodesByPort.emplace(port.id, node);
            }
        };

        if (!client.split) {
            auto ports = portsByClient[client.id];
            auto* node = new CanvasNode(client, std::move(ports), [this](const std::string& portId) {
                activatePort(portId);
            });
            node->setPos(client.x, client.y);
            node->setData(0, QString::fromStdString(client.id));
            node->setData(3, true);
            scene()->addItem(node);
            for (const auto& port : portsByClient[client.id])
                nodesByPort.emplace(port.id, node);
            continue;
        }

        std::vector<PortInfo> inputs;
        std::vector<PortInfo> outputs;
        for (const auto& port : portsByClient[client.id]) {
            if (port.direction == Direction::input)
                inputs.push_back(port);
            else
                outputs.push_back(port);
        }
        addNode(std::move(inputs), { client.x, client.y }, true, QString::fromStdString(client.name) + QStringLiteral(" · 输入"));
        addNode(std::move(outputs), { client.x + kNodeWidth + 52.0, client.y }, false, QString::fromStdString(client.name) + QStringLiteral(" · 输出"));
    }

    std::unordered_map<std::string, const PortInfo*> ports;
    for (const auto& port : snapshot_.ports)
        ports.emplace(port.id, &port);

    for (const auto& connection : snapshot_.connections) {
        if (!connection.alive)
            continue;
        const auto sourcePort = ports.find(connection.sourcePortId);
        const auto destinationPort = ports.find(connection.destinationPortId);
        if (sourcePort == ports.end() || destinationPort == ports.end())
            continue;
        const auto sourceNode = nodesByPort.find(connection.sourcePortId);
        const auto destinationNode = nodesByPort.find(connection.destinationPortId);
        if (sourceNode == nodesByPort.end() || destinationNode == nodesByPort.end())
            continue;

        const QColor color = portColor(sourcePort->second->kind);
        auto* line = new QGraphicsPathItem(connectionPath(sourceNode->second->portPosition(connection.sourcePortId),
                                                           destinationNode->second->portPosition(connection.destinationPortId)));
        line->setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap));
        line->setZValue(-1.0);
        line->setFlag(QGraphicsItem::ItemIsSelectable);
        line->setData(1, QString::fromStdString(connection.sourcePortId));
        line->setData(2, QString::fromStdString(connection.destinationPortId));
        scene()->addItem(line);
    }

    applySearchFilter();
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

void PatchbayCanvas::setSearchQuery(const QString& query)
{
    searchQuery_ = query.trimmed();
    applySearchFilter();
}

void PatchbayCanvas::zoomIn()
{
    scale(1.2, 1.2);
}

void PatchbayCanvas::zoomOut()
{
    scale(1.0 / 1.2, 1.0 / 1.2);
}

void PatchbayCanvas::zoomReset()
{
    resetTransform();
}

void PatchbayCanvas::zoomFit()
{
    const auto bounds = scene()->itemsBoundingRect();
    if (!bounds.isEmpty())
        fitInView(bounds.adjusted(-36.0, -36.0, 36.0, 36.0), Qt::KeepAspectRatio);
}

void PatchbayCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panOrigin_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void PatchbayCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (panning_) {
        const QPoint offset = event->pos() - panOrigin_;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - offset.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - offset.y());
        panOrigin_ = event->pos();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void PatchbayCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
    updateClientPositions();
}

void PatchbayCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    const auto* item = itemAt(event->pos());
    if (item != nullptr && item->data(1).isValid()) {
        const auto source = item->data(1).toString().toStdString();
        const auto destination = item->data(2).toString().toStdString();
        auto* disconnect = menu.addAction(QStringLiteral("断开连接"));
        if (menu.exec(event->globalPos()) == disconnect) {
            std::erase_if(snapshot_.connections, [&source, &destination](const ConnectionInfo& connection) {
                return connection.sourcePortId == source && connection.destinationPortId == destination;
            });
            setSnapshot(snapshot_);
        }
        return;
    }

    if (item != nullptr && item->data(0).isValid()) {
        auto* center = menu.addAction(QStringLiteral("居中显示节点"));
        auto* rename = menu.addAction(QStringLiteral("重命名节点"));
        auto* split = menu.addAction(QStringLiteral("Split/Join Group"));
        const auto* selected = menu.exec(event->globalPos());
        if (selected == center) {
            centerOn(item);
        } else if (selected == rename) {
            const auto id = item->data(0).toString().toStdString();
            const auto client = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&id](const ClientInfo& value) {
                return value.id == id;
            });
            if (client != snapshot_.clients.end()) {
                bool accepted = false;
                const auto alias = QInputDialog::getText(this, QStringLiteral("重命名节点"), QStringLiteral("显示名称"),
                                                          QLineEdit::Normal, QString::fromStdString(client->alias), &accepted);
                if (accepted && !alias.trimmed().isEmpty()) {
                    client->alias = alias.trimmed().toStdString();
                    setSnapshot(snapshot_);
                }
            }
        } else if (selected == split) {
            const auto id = item->data(0).toString().toStdString();
            const auto client = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&id](const ClientInfo& value) {
                return value.id == id;
            });
            if (client != snapshot_.clients.end()) {
                client->split = !client->split;
                auto settings = patchbaySettings();
                settings.setValue(QStringLiteral("layout/%1/split").arg(QString::fromStdString(id)), client->split);
                setSnapshot(snapshot_);
            }
        }
        return;
    }

    auto* fit = menu.addAction(QStringLiteral("适配全部节点"));
    auto* reset = menu.addAction(QStringLiteral("重置缩放"));
    const auto* selected = menu.exec(event->globalPos());
    if (selected == fit)
        zoomFit();
    else if (selected == reset)
        zoomReset();
}

void PatchbayCanvas::wheelEvent(QWheelEvent* event)
{
    const qreal currentScale = transform().m11();
    const qreal factor = event->angleDelta().y() >= 0 ? 1.15 : 1.0 / 1.15;
    if ((factor > 1.0 && currentScale >= 3.0) || (factor < 1.0 && currentScale <= 0.2)) {
        event->accept();
        return;
    }
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    event->accept();
}

void PatchbayCanvas::activatePort(const std::string& portId)
{
    const auto findPort = [this](const std::string& id) {
        return std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(), [&id](const PortInfo& port) {
            return port.id == id;
        });
    };
    const auto port = findPort(portId);
    if (port == snapshot_.ports.cend())
        return;
    if (pendingPortId_.empty()) {
        pendingPortId_ = portId;
        return;
    }

    const auto pending = findPort(pendingPortId_);
    if (pending == snapshot_.ports.cend() || pending->direction == port->direction || pending->kind != port->kind) {
        pendingPortId_ = portId;
        return;
    }

    const auto& source = pending->direction == Direction::output ? *pending : *port;
    const auto& destination = pending->direction == Direction::input ? *pending : *port;
    const bool exists = std::any_of(snapshot_.connections.cbegin(), snapshot_.connections.cend(), [&source, &destination](const ConnectionInfo& connection) {
        return connection.sourcePortId == source.id && connection.destinationPortId == destination.id;
    });
    if (!exists)
        snapshot_.connections.push_back({ source.id, destination.id });
    pendingPortId_.clear();
    setSnapshot(snapshot_);
}

void PatchbayCanvas::applySearchFilter()
{
    const auto query = searchQuery_.toLower();
    for (auto* item : scene()->items()) {
        if (!item->data(0).isValid())
            continue;
        if (item->data(3).isValid() && !item->data(3).toBool())
            continue;
        const auto id = item->data(0).toString().toStdString();
        const auto client = std::find_if(snapshot_.clients.cbegin(), snapshot_.clients.cend(), [&id](const ClientInfo& value) {
            return value.id == id;
        });
        QString searchable = client == snapshot_.clients.cend() ? QString() : QString::fromStdString(client->name + " " + client->alias).toLower();
        for (const auto& port : snapshot_.ports) {
            if (port.clientId == id)
                searchable += QLatin1Char(' ') + QString::fromStdString(port.name + " " + port.alias).toLower();
        }
        const bool match = query.isEmpty() || searchable.contains(query);
        item->setVisible(match);
        item->setOpacity(match ? 1.0 : 0.2);
    }
}

void PatchbayCanvas::updateClientPositions()
{
    bool changed = false;
    for (auto* item : scene()->items()) {
        if (!item->data(0).isValid())
            continue;
        if (item->data(3).isValid() && !item->data(3).toBool())
            continue;
        const auto id = item->data(0).toString().toStdString();
        const auto client = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&id](const ClientInfo& value) {
            return value.id == id;
        });
        if (client != snapshot_.clients.end() && (client->x != item->pos().x() || client->y != item->pos().y())) {
            client->x = item->pos().x();
            client->y = item->pos().y();
            auto settings = patchbaySettings();
            const auto key = QString::fromStdString(id);
            settings.setValue(QStringLiteral("layout/%1/x").arg(key), client->x);
            settings.setValue(QStringLiteral("layout/%1/y").arg(key), client->y);
            changed = true;
        }
    }
    if (changed)
        setSnapshot(snapshot_);
}