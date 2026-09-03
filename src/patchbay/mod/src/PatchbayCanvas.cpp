#include <mod_patchbay/PatchbayCanvas.hpp>

#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>

#include <QApplication>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMouseEvent>
#include <QMimeData>
#include <QClipboard>
#include <QUrl>
#include <QSettings>
#include <QSaveFile>
#include <QTimer>
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

constexpr qreal kMinimumNodeWidth = 220.0;
constexpr qreal kHeaderHeight = 34.0;
constexpr qreal kPortHeight = 26.0;
constexpr qreal kCanvasExtent = 1'000'000.0;
constexpr auto kNodeMimeType = "application/x-winjacknexus-node";

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

QString portKindName(PortKind kind)
{
    switch (kind) {
    case PortKind::audio:
        return QStringLiteral("audio");
    case PortKind::midi:
        return QStringLiteral("midi");
    case PortKind::cv:
        return QStringLiteral("cv");
    case PortKind::osc:
        return QStringLiteral("osc");
    default:
        return QStringLiteral("unknown");
    }
}

PortKind portKindFromName(const QString& name)
{
    if (name == QStringLiteral("audio"))
        return PortKind::audio;
    if (name == QStringLiteral("midi"))
        return PortKind::midi;
    if (name == QStringLiteral("cv"))
        return PortKind::cv;
    if (name == QStringLiteral("osc"))
        return PortKind::osc;
    return PortKind::unknown;
}

QString directionName(Direction direction)
{
    return direction == Direction::input ? QStringLiteral("input")
         : direction == Direction::output ? QStringLiteral("output")
                                          : QStringLiteral("unknown");
}

Direction directionFromName(const QString& name)
{
    return name == QStringLiteral("input") ? Direction::input
         : name == QStringLiteral("output") ? Direction::output
                                             : Direction::unknown;
}

class CanvasNode final : public QGraphicsObject {
public:
    CanvasNode(ClientInfo client, std::vector<PortInfo> ports, std::function<void(const std::string&, const QPointF&)> portPressed,
               std::function<void(const QPointF&)> portDragged, std::function<void(const QPointF&)> portReleased)
        : client_(std::move(client)), ports_(std::move(ports)), portPressed_(std::move(portPressed)),
          portDragged_(std::move(portDragged)), portReleased_(std::move(portReleased))
    {
        std::sort(ports_.begin(), ports_.end(), [](const PortInfo& left, const PortInfo& right) {
            return left.ordinal < right.ordinal;
        });
        for (std::size_t index = 0; index < ports_.size(); ++index) {
            if (ports_[index].direction == Direction::input)
                inputs_.push_back(index);
            else if (ports_[index].direction == Direction::output)
                outputs_.push_back(index);
        }
            const auto widths = labelWidths(client_, ports_);
            inputLabelWidth_ = widths.input;
            outputLabelWidth_ = widths.output;
            nodeWidth_ = widths.node;
        setFlag(ItemIsSelectable);
        setFlag(ItemIsMovable);
        setToolTip(QString::fromStdString(client_.name));
    }

    QRectF boundingRect() const override
    {
        return { 0.0, 0.0, nodeWidth_, kHeaderHeight + std::max({ std::size_t { 1 }, inputs_.size(), outputs_.size() }) * kPortHeight };
    }

    static qreal preferredWidth(const ClientInfo& client, const std::vector<PortInfo>& ports)
    {
        return labelWidths(client, ports).node;
    }

    QPointF portPosition(const std::string& portId) const
    {
        const auto port = std::find_if(ports_.cbegin(), ports_.cend(), [&portId](const PortInfo& item) {
            return item.id == portId;
        });
        if (port == ports_.cend())
            return scenePos();

        const auto portIndex = static_cast<std::size_t>(std::distance(ports_.cbegin(), port));
        const auto& directionPorts = port->direction == Direction::input ? inputs_ : outputs_;
        const auto row = static_cast<qreal>(std::distance(directionPorts.cbegin(), std::find(directionPorts.cbegin(), directionPorts.cend(), portIndex)));
        const qreal x = port->direction == Direction::input ? 0.0 : nodeWidth_;
        return mapToScene({ x, kHeaderHeight + (row + 0.5) * kPortHeight });
    }

    std::string portIdAt(const QPointF& scenePosition) const
    {
        const auto position = mapFromScene(scenePosition);
        if (position.y() < kHeaderHeight)
            return {};
        const auto row = static_cast<std::ptrdiff_t>(std::floor((position.y() - kHeaderHeight) / kPortHeight));
        if (row < 0)
            return {};
        if (position.x() <= 12.0 && row < static_cast<std::ptrdiff_t>(inputs_.size()))
            return ports_[inputs_[static_cast<std::size_t>(row)]].id;
        if (position.x() >= nodeWidth_ - 12.0 && row < static_cast<std::ptrdiff_t>(outputs_.size()))
            return ports_[outputs_[static_cast<std::size_t>(row)]].id;
        return {};
    }

    bool containsPort(const std::string& portId) const
    {
        return std::any_of(ports_.cbegin(), ports_.cend(), [&portId](const PortInfo& port) {
            return port.id == portId;
        });
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
        painter->drawRoundedRect(QRectF(0.0, 0.0, nodeWidth_, kHeaderHeight), 6.0, 6.0);
        painter->drawRect(0.0, kHeaderHeight - 6.0, nodeWidth_, 6.0);

        QFont titleFont = QApplication::font();
        titleFont.setPointSize(10);
        titleFont.setWeight(QFont::DemiBold);
        painter->setPen(theme.text);
        painter->setFont(titleFont);
        const QString title = QString::fromStdString(client_.alias.empty() ? client_.name : client_.alias);
        painter->drawText(QRectF(12.0, 0.0, nodeWidth_ - 24.0, kHeaderHeight), Qt::AlignVCenter | Qt::AlignLeft, title);

        QFont portFont = QApplication::font();
        portFont.setPointSize(9);
        painter->setFont(portFont);
        const auto drawPort = [this, painter, &theme](const PortInfo& port, qreal row, bool input) {
            const QColor color = portColor(port.kind);
            painter->setPen(QPen(theme.separator, 1.0));
            painter->drawLine(QPointF(8.0, row), QPointF(nodeWidth_ - 8.0, row));
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            const qreal pinX = input ? 0.0 : nodeWidth_;
            painter->drawEllipse(QPointF(pinX, row + kPortHeight / 2.0), 5.5, 5.5);
            const QString name = QString::fromStdString(port.alias.empty() ? port.name : port.alias);
            const QRectF labelBounds = input ? QRectF(12.0, row + 3.0, inputLabelWidth_, kPortHeight - 6.0)
                                             : QRectF(nodeWidth_ - outputLabelWidth_ - 12.0, row + 3.0, outputLabelWidth_, kPortHeight - 6.0);
            painter->setBrush(color.darker(150));
            painter->drawRoundedRect(labelBounds, 3.0, 3.0);
            painter->setPen(theme.text);
            const QRectF textBounds = labelBounds.adjusted(5.0, 0.0, -5.0, 0.0);
            painter->drawText(textBounds, Qt::AlignVCenter | (input ? Qt::AlignLeft : Qt::AlignRight), name);
        };
        const auto rowCount = std::max(inputs_.size(), outputs_.size());
        for (std::size_t index = 0; index < rowCount; ++index) {
            const qreal row = kHeaderHeight + static_cast<qreal>(index) * kPortHeight;
            if (index < inputs_.size())
                drawPort(ports_[inputs_[index]], row, true);
            if (index < outputs_.size())
                drawPort(ports_[outputs_[index]], row, false);
        }
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && (event->pos().x() <= 12.0 || event->pos().x() >= nodeWidth_ - 12.0)) {
            const auto row = static_cast<std::ptrdiff_t>(std::floor((event->pos().y() - kHeaderHeight) / kPortHeight));
            if (row >= 0) {
                const bool onInputPin = event->pos().x() <= 12.0 && row < static_cast<std::ptrdiff_t>(inputs_.size());
                const bool onOutputPin = event->pos().x() >= nodeWidth_ - 12.0 && row < static_cast<std::ptrdiff_t>(outputs_.size());
                if (onInputPin || onOutputPin) {
                    const auto& port = ports_[onInputPin ? inputs_[static_cast<std::size_t>(row)] : outputs_[static_cast<std::size_t>(row)]];
                    pressedPortId_ = port.id;
                    portPressed_(port.id, mapToScene(event->pos()));
                    event->accept();
                    return;
                }
            }
        }
        QGraphicsObject::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton && !pressedPortId_.empty()) {
            portDragged_(mapToScene(event->pos()));
            event->accept();
            return;
        }
        QGraphicsObject::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && !pressedPortId_.empty()) {
            portReleased_(mapToScene(event->pos()));
            pressedPortId_.clear();
            event->accept();
            return;
        }
        QGraphicsObject::mouseReleaseEvent(event);
    }

private:
    struct LabelWidths final {
        qreal input = 0.0;
        qreal output = 0.0;
        qreal node = kMinimumNodeWidth;
    };

    static LabelWidths labelWidths(const ClientInfo& client, const std::vector<PortInfo>& ports)
    {
        QFont portFont = QApplication::font();
        portFont.setPointSize(9);
        const QFontMetrics portMetrics(portFont);
        LabelWidths widths;
        for (const auto& port : ports) {
            const auto name = QString::fromStdString(port.alias.empty() ? port.name : port.alias);
            const qreal width = portMetrics.horizontalAdvance(name) + 10.0;
            if (port.direction == Direction::input)
                widths.input = std::max(widths.input, width);
            else if (port.direction == Direction::output)
                widths.output = std::max(widths.output, width);
        }

        QFont titleFont = QApplication::font();
        titleFont.setPointSize(10);
        titleFont.setWeight(QFont::DemiBold);
        const auto title = QString::fromStdString(client.alias.empty() ? client.name : client.alias);
        widths.node = std::max({ kMinimumNodeWidth, widths.input + widths.output + 48.0,
                                 static_cast<qreal>(QFontMetrics(titleFont).horizontalAdvance(title) + 24) });
        return widths;
    }

    ClientInfo client_;
    std::vector<PortInfo> ports_;
    std::vector<std::size_t> inputs_;
    std::vector<std::size_t> outputs_;
    std::function<void(const std::string&, const QPointF&)> portPressed_;
    std::function<void(const QPointF&)> portDragged_;
    std::function<void(const QPointF&)> portReleased_;
    std::string pressedPortId_;
    qreal inputLabelWidth_ = 0.0;
    qreal outputLabelWidth_ = 0.0;
    qreal nodeWidth_ = kMinimumNodeWidth;
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

}

PatchbayCanvas::PatchbayCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
    auto* canvasScene = new QGraphicsScene(this);
    canvasScene->setSceneRect(-kCanvasExtent, -kCanvasExtent, kCanvasExtent * 2.0, kCanvasExtent * 2.0);
    setScene(canvasScene);
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setBackgroundBrush(activeCanvasTheme().background);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(canvasScene, &QGraphicsScene::selectionChanged, this, [this] {
        notifySelection();
    });
}

const Snapshot& PatchbayCanvas::snapshot() const noexcept
{
    return snapshot_;
}

bool PatchbayCanvas::saveProject(const QString& filePath) const
{
    const auto settings = patchbaySettings();
    QJsonArray clients;
    for (const auto& client : snapshot_.clients) {
        clients.append(QJsonObject {
            { QStringLiteral("id"), QString::fromStdString(client.id) },
            { QStringLiteral("name"), QString::fromStdString(client.name) },
            { QStringLiteral("alias"), QString::fromStdString(client.alias) },
            { QStringLiteral("icon"), QString::fromStdString(client.iconName) },
            { QStringLiteral("uri"), QString::fromStdString(client.uri) },
            { QStringLiteral("x"), client.x },
            { QStringLiteral("y"), client.y },
            { QStringLiteral("split"), client.split },
            { QStringLiteral("alive"), client.alive }
        });
    }

    QJsonArray ports;
    for (const auto& port : snapshot_.ports) {
        ports.append(QJsonObject {
            { QStringLiteral("id"), QString::fromStdString(port.id) },
            { QStringLiteral("client"), QString::fromStdString(port.clientId) },
            { QStringLiteral("name"), QString::fromStdString(port.name) },
            { QStringLiteral("alias"), QString::fromStdString(port.alias) },
            { QStringLiteral("kind"), portKindName(port.kind) },
            { QStringLiteral("direction"), directionName(port.direction) },
            { QStringLiteral("ordinal"), static_cast<int>(port.ordinal) },
            { QStringLiteral("alive"), port.alive }
        });
    }

    QJsonArray connections;
    for (const auto& connection : snapshot_.connections) {
        connections.append(QJsonObject {
            { QStringLiteral("source"), QString::fromStdString(connection.sourcePortId) },
            { QStringLiteral("destination"), QString::fromStdString(connection.destinationPortId) },
            { QStringLiteral("alive"), connection.alive }
        });
    }

    QJsonArray layouts;
    for (const auto& client : snapshot_.clients) {
        const auto addLayout = [&layouts, &settings](const QString& key, QPointF fallback) {
            const auto xKey = QStringLiteral("layout/%1/x").arg(key);
            const auto yKey = QStringLiteral("layout/%1/y").arg(key);
            layouts.append(QJsonObject { { QStringLiteral("key"), key },
                                         { QStringLiteral("x"), settings.value(xKey, fallback.x()).toDouble() },
                                         { QStringLiteral("y"), settings.value(yKey, fallback.y()).toDouble() } });
        };
        const auto clientKey = QString::fromStdString(client.id);
        addLayout(clientKey, { client.x, client.y });
        if (client.split) {
            addLayout(clientKey + QStringLiteral("/input"), { client.x, client.y });
            addLayout(clientKey + QStringLiteral("/output"), { client.x + kMinimumNodeWidth + 52.0, client.y });
        }
    }

    const QJsonDocument document(QJsonObject {
        { QStringLiteral("version"), 1 },
        { QStringLiteral("app"), QStringLiteral("WinJACKNexus") },
        { QStringLiteral("clients"), clients },
        { QStringLiteral("ports"), ports },
        { QStringLiteral("connections"), connections },
        { QStringLiteral("layouts"), layouts },
        { QStringLiteral("mixers"), QJsonArray {} },
        { QStringLiteral("switches"), QJsonArray {} }
    });

    QSaveFile file(filePath);
    return file.open(QIODevice::WriteOnly)
        && file.write(document.toJson(QJsonDocument::Indented)) >= 0
        && file.commit();
}

bool PatchbayCanvas::loadProject(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("version")).toInt() != 1) {
        return false;
    }

    Snapshot project;
    const auto root = document.object();
    for (const auto& value : root.value(QStringLiteral("clients")).toArray()) {
        const auto item = value.toObject();
        ClientInfo client;
        client.id = item.value(QStringLiteral("id")).toString().toStdString();
        client.name = item.value(QStringLiteral("name")).toString().toStdString();
        if (client.id.empty() || client.name.empty())
            return false;
        client.alias = item.value(QStringLiteral("alias")).toString().toStdString();
        client.iconName = item.value(QStringLiteral("icon")).toString().toStdString();
        client.uri = item.value(QStringLiteral("uri")).toString().toStdString();
        client.x = item.value(QStringLiteral("x")).toDouble();
        client.y = item.value(QStringLiteral("y")).toDouble();
        client.split = item.value(QStringLiteral("split")).toBool();
        client.alive = item.value(QStringLiteral("alive")).toBool(true);
        project.clients.push_back(std::move(client));
    }

    for (const auto& value : root.value(QStringLiteral("ports")).toArray()) {
        const auto item = value.toObject();
        PortInfo port;
        port.id = item.value(QStringLiteral("id")).toString().toStdString();
        port.clientId = item.value(QStringLiteral("client")).toString().toStdString();
        port.name = item.value(QStringLiteral("name")).toString().toStdString();
        if (port.id.empty() || port.clientId.empty() || port.name.empty())
            return false;
        port.alias = item.value(QStringLiteral("alias")).toString().toStdString();
        port.kind = portKindFromName(item.value(QStringLiteral("kind")).toString());
        port.direction = directionFromName(item.value(QStringLiteral("direction")).toString());
        port.ordinal = static_cast<std::uint32_t>(item.value(QStringLiteral("ordinal")).toInt());
        port.alive = item.value(QStringLiteral("alive")).toBool(true);
        project.ports.push_back(std::move(port));
    }

    for (const auto& value : root.value(QStringLiteral("connections")).toArray()) {
        const auto item = value.toObject();
        ConnectionInfo connection;
        connection.sourcePortId = item.value(QStringLiteral("source")).toString().toStdString();
        connection.destinationPortId = item.value(QStringLiteral("destination")).toString().toStdString();
        if (connection.sourcePortId.empty() || connection.destinationPortId.empty())
            return false;
        connection.alive = item.value(QStringLiteral("alive")).toBool(true);
        project.connections.push_back(std::move(connection));
    }

    auto settings = patchbaySettings();
    for (const auto& client : project.clients) {
        const auto key = QString::fromStdString(client.id);
        settings.setValue(QStringLiteral("layout/%1/x").arg(key), client.x);
        settings.setValue(QStringLiteral("layout/%1/y").arg(key), client.y);
        settings.setValue(QStringLiteral("layout/%1/split").arg(key), client.split);
    }
    for (const auto& value : root.value(QStringLiteral("layouts")).toArray()) {
        const auto item = value.toObject();
        const auto key = item.value(QStringLiteral("key")).toString();
        if (key.isEmpty())
            return false;
        settings.setValue(QStringLiteral("layout/%1/x").arg(key), item.value(QStringLiteral("x")).toDouble());
        settings.setValue(QStringLiteral("layout/%1/y").arg(key), item.value(QStringLiteral("y")).toDouble());
    }
    setSnapshot(project);
    projectDirty_ = false;
    return true;
}

bool PatchbayCanvas::restoreJackConnections(WinJACKNexus::Audio::JackClient& client)
{
    if (!client.isOpen())
        return false;

    auto& api = WinJACKNexus::Audio::JackApi::instance();
    bool restored = false;
    for (const auto& connection : snapshot_.connections) {
        if (!connection.alive)
            continue;

        const auto* source = api.portByName(client.nativeHandle(), connection.sourcePortId.c_str());
        const auto* destination = api.portByName(client.nativeHandle(), connection.destinationPortId.c_str());
        if (source == nullptr || destination == nullptr)
            continue;

        bool alreadyConnected = false;
        if (const auto** names = api.portGetConnections(source); names != nullptr) {
            for (const auto** name = names; *name != nullptr; ++name)
                alreadyConnected = alreadyConnected || connection.destinationPortId == *name;
            api.freeMemory(const_cast<void*>(static_cast<const void*>(names)));
        }

        if (!alreadyConnected
            && api.connect(client.nativeHandle(), connection.sourcePortId.c_str(), connection.destinationPortId.c_str()) == 0) {
            restored = true;
        }
    }
    return restored;
}

void PatchbayCanvas::setSnapshot(const Snapshot& snapshot)
{
    std::unordered_set<std::string> selectedClientIds;
    for (const auto* item : scene()->selectedItems()) {
        if (item->data(0).isValid())
            selectedClientIds.insert(item->data(0).toString().toStdString());
    }

    Snapshot restored = snapshot;
    auto settings = patchbaySettings();
    for (auto& client : restored.clients) {
        if (client.id == "system")
            client.alive = true;
        const auto key = QString::fromStdString(client.id);
        if (settings.contains(QStringLiteral("layout/%1/x").arg(key))) {
            client.x = settings.value(QStringLiteral("layout/%1/x").arg(key)).toDouble();
            client.y = settings.value(QStringLiteral("layout/%1/y").arg(key)).toDouble();
        }
        if (settings.contains(QStringLiteral("layout/%1/split").arg(key)))
            client.split = settings.value(QStringLiteral("layout/%1/split").arg(key)).toBool();
        else if (client.id == "system")
            client.split = true;
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

        const auto addNode = [this, &client, &portsByClient, &nodesByPort, &selectedClientIds](std::vector<PortInfo> ports, QPointF position, Direction portDirection, const QString& layoutKey, const QString& label) {
            ClientInfo displayClient = client;
            displayClient.alias = label.toStdString();
            auto* node = new CanvasNode(std::move(displayClient), std::move(ports),
                [this](const std::string& portId, const QPointF& position) { beginPortDrag(portId, position); },
                [this](const QPointF& position) { updatePortDrag(position); },
                [this](const QPointF& position) { finishPortDrag(position); });
            node->setPos(position);
            node->setData(0, QString::fromStdString(client.id));
            node->setData(3, layoutKey);
            node->setData(4, position);
            node->setSelected(selectedClientIds.contains(client.id));
            scene()->addItem(node);
            for (const auto& port : portsByClient[client.id]) {
                if (port.direction == portDirection)
                    nodesByPort.emplace(port.id, node);
            }
        };

        if (!client.split) {
            auto ports = portsByClient[client.id];
            auto* node = new CanvasNode(client, std::move(ports),
                [this](const std::string& portId, const QPointF& position) { beginPortDrag(portId, position); },
                [this](const QPointF& position) { updatePortDrag(position); },
                [this](const QPointF& position) { finishPortDrag(position); });
            node->setPos(client.x, client.y);
            node->setData(0, QString::fromStdString(client.id));
            node->setData(3, QString::fromStdString(client.id));
            node->setData(4, QPointF(client.x, client.y));
            node->setSelected(selectedClientIds.contains(client.id));
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
        const auto inputLayoutKey = QString::fromStdString(client.id) + QStringLiteral("/input");
        const auto outputLayoutKey = QString::fromStdString(client.id) + QStringLiteral("/output");
        const auto positionFor = [&settings](const QString& layoutKey, QPointF fallback) {
            const auto xKey = QStringLiteral("layout/%1/x").arg(layoutKey);
            const auto yKey = QStringLiteral("layout/%1/y").arg(layoutKey);
            return settings.contains(xKey) ? QPointF(settings.value(xKey).toDouble(), settings.value(yKey).toDouble()) : fallback;
        };
        const bool isSystem = client.id == "system";
        addNode(std::move(inputs), positionFor(inputLayoutKey, { client.x, client.y }), Direction::input, inputLayoutKey,
            isSystem ? QStringLiteral("system playback") : QString::fromStdString(client.name) + QStringLiteral(" · 输入"));
        const auto inputWidth = CanvasNode::preferredWidth(client, inputs);
        addNode(std::move(outputs), positionFor(outputLayoutKey, { client.x + inputWidth + 52.0, client.y }), Direction::output, outputLayoutKey,
            isSystem ? QStringLiteral("system capture") : QString::fromStdString(client.name) + QStringLiteral(" · 输出"));
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

bool PatchbayCanvas::isProjectDirty() const noexcept
{
    return projectDirty_;
}

void PatchbayCanvas::markProjectSaved() noexcept
{
    projectDirty_ = false;
}

void PatchbayCanvas::newProject()
{
    snapshot_ = {};
    pendingPortId_.clear();
    setSnapshot(snapshot_);
    projectDirty_ = false;
}

void PatchbayCanvas::setClientAlias(const std::string& clientId, const QString& alias)
{
    const auto client = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&clientId](const ClientInfo& value) {
        return value.id == clientId;
    });
    if (client == snapshot_.clients.end())
        return;
    const auto newAlias = alias.trimmed().toStdString();
    if (client->alias == newAlias)
        return;
    client->alias = newAlias;
    setSnapshot(snapshot_);
    projectDirty_ = true;
}

void PatchbayCanvas::setClientSplit(const std::string& clientId, bool split)
{
    const auto client = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&clientId](const ClientInfo& value) {
        return value.id == clientId;
    });
    if (client == snapshot_.clients.end() || client->split == split)
        return;
    client->split = split;
    auto settings = patchbaySettings();
    settings.setValue(QStringLiteral("layout/%1/split").arg(QString::fromStdString(clientId)), split);
    setSnapshot(snapshot_);
    projectDirty_ = true;
}

void PatchbayCanvas::setNodeSelectionHandler(std::function<void(const std::string&)> handler)
{
    nodeSelectionHandler_ = std::move(handler);
    notifySelection();
}

void PatchbayCanvas::setConnectionDisconnectHandler(std::function<void(const std::string&, const std::string&)> handler)
{
    connectionDisconnectHandler_ = std::move(handler);
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
    const bool blankCanvas = itemAt(event->pos()) == nullptr;
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && blankCanvas)) {
        panning_ = true;
        panningButton_ = event->button();
        panOrigin_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    nodeDragActive_ = event->button() == Qt::LeftButton && itemAt(event->pos()) != nullptr && itemAt(event->pos())->data(0).isValid();
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
    updateConnectionPaths();
}

void PatchbayCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (panning_ && event->button() == panningButton_) {
        panning_ = false;
        panningButton_ = Qt::NoButton;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton)
        nodeDragActive_ = false;
    updateClientPositions();
    updateConnectionPaths();
}

bool PatchbayCanvas::isNodeDragActive() const noexcept
{
    return nodeDragActive_;
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
            const auto removed = std::erase_if(snapshot_.connections, [&source, &destination](const ConnectionInfo& connection) {
                return connection.sourcePortId == source && connection.destinationPortId == destination;
            });
            if (removed != 0) {
                if (connectionDisconnectHandler_)
                    connectionDisconnectHandler_(source, destination);
                setSnapshot(snapshot_);
                projectDirty_ = true;
            }
        }
        return;
    }

    if (item != nullptr && item->data(0).isValid()) {
        const auto id = item->data(0).toString().toStdString();
        const auto client = std::find_if(snapshot_.clients.cbegin(), snapshot_.clients.cend(), [&id](const ClientInfo& value) {
            return value.id == id;
        });
        auto* center = menu.addAction(QStringLiteral("居中显示节点"));
        auto* rename = menu.addAction(QStringLiteral("重命名节点"));
        auto* split = menu.addAction(QStringLiteral("Split/Join Group"));
        menu.addSeparator();
        auto* disconnectAll = menu.addAction(QStringLiteral("断开全部连接"));
        auto* showGui = menu.addAction(QStringLiteral("显示自带 GUI"));
        showGui->setEnabled(client != snapshot_.clients.cend() && !client->uri.empty());
        const auto* selected = menu.exec(event->globalPos());
        if (selected == center) {
            centerOn(item);
        } else if (selected == rename) {
            const auto mutableClient = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&id](const ClientInfo& value) {
                return value.id == id;
            });
            if (mutableClient != snapshot_.clients.end()) {
                bool accepted = false;
                const auto alias = QInputDialog::getText(this, QStringLiteral("重命名节点"), QStringLiteral("显示名称"),
                                                          QLineEdit::Normal, QString::fromStdString(mutableClient->alias), &accepted);
                if (accepted && !alias.trimmed().isEmpty()) {
                    mutableClient->alias = alias.trimmed().toStdString();
                    setSnapshot(snapshot_);
                    projectDirty_ = true;
                }
            }
        } else if (selected == split) {
            const auto mutableClient = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&id](const ClientInfo& value) {
                return value.id == id;
            });
            if (mutableClient != snapshot_.clients.end()) {
                mutableClient->split = !mutableClient->split;
                auto settings = patchbaySettings();
                settings.setValue(QStringLiteral("layout/%1/split").arg(QString::fromStdString(id)), mutableClient->split);
                setSnapshot(snapshot_);
                projectDirty_ = true;
            }
        } else if (selected == disconnectAll) {
            std::vector<ConnectionInfo> disconnected;
            const auto removed = std::erase_if(snapshot_.connections, [this, &id, &disconnected](const ConnectionInfo& connection) {
                const auto hasClient = [this, &id](const std::string& portId) {
                    const auto port = std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(), [&portId](const PortInfo& port) {
                        return port.id == portId;
                    });
                    return port != snapshot_.ports.cend() && port->clientId == id;
                };
                const bool matches = hasClient(connection.sourcePortId) || hasClient(connection.destinationPortId);
                if (matches)
                    disconnected.push_back(connection);
                return matches;
            });
            if (removed != 0) {
                if (connectionDisconnectHandler_) {
                    for (const auto& connection : disconnected)
                        connectionDisconnectHandler_(connection.sourcePortId, connection.destinationPortId);
                }
                setSnapshot(snapshot_);
                projectDirty_ = true;
            }
        } else if (selected == showGui && client != snapshot_.clients.cend()) {
            QDesktopServices::openUrl(QUrl(QString::fromStdString(client->uri)));
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
    if (!exists) {
        snapshot_.connections.push_back({ source.id, destination.id });
        projectDirty_ = true;
    }
    pendingPortId_.clear();
    scheduleSnapshotRefresh();
}

void PatchbayCanvas::beginPortDrag(const std::string& portId, const QPointF& position)
{
    draggedPortId_ = portId;
    if (portDragPreview_ != nullptr)
        scene()->removeItem(portDragPreview_);
    portDragPreview_ = new QGraphicsPathItem(connectionPath(position, position));
    portDragPreview_->setPen(QPen(portColor(std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(), [&portId](const PortInfo& port) {
                                      return port.id == portId;
                                  })->kind),
                                  2.5, Qt::DashLine, Qt::RoundCap));
    portDragPreview_->setZValue(-0.5);
    scene()->addItem(portDragPreview_);
}

void PatchbayCanvas::updatePortDrag(const QPointF& position)
{
    if (draggedPortId_.empty() || portDragPreview_ == nullptr)
        return;
    const auto source = std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(), [this](const PortInfo& port) {
        return port.id == draggedPortId_;
    });
    if (source == snapshot_.ports.cend())
        return;
    const auto* node = dynamic_cast<CanvasNode*>(scene()->itemAt(position, QTransform()));
    const auto targetId = node == nullptr ? std::string() : node->portIdAt(position);
    const auto target = std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(), [&targetId](const PortInfo& port) {
        return port.id == targetId;
    });
    const bool compatible = target != snapshot_.ports.cend() && target->direction != source->direction && target->kind == source->kind;
    CanvasNode* sourceNode = nullptr;
    for (auto* item : scene()->items()) {
        if (auto* candidate = dynamic_cast<CanvasNode*>(item); candidate != nullptr && candidate->containsPort(source->id)) {
            sourceNode = candidate;
            break;
        }
    }
    portDragPreview_->setPen(QPen(compatible ? portColor(source->kind) : portColor(source->kind).darker(170), 2.5,
                                  compatible ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap));
    portDragPreview_->setPath(connectionPath(sourceNode == nullptr ? position : sourceNode->portPosition(source->id), position));
}

void PatchbayCanvas::finishPortDrag(const QPointF& position)
{
    if (draggedPortId_.empty())
        return;
    const auto sourceId = std::exchange(draggedPortId_, {});
    if (portDragPreview_ != nullptr) {
        scene()->removeItem(portDragPreview_);
        delete portDragPreview_;
        portDragPreview_ = nullptr;
    }
    const auto* node = dynamic_cast<CanvasNode*>(scene()->itemAt(position, QTransform()));
    const auto targetId = node == nullptr ? std::string() : node->portIdAt(position);
    if (targetId.empty() || targetId == sourceId) {
        activatePort(sourceId);
        return;
    }

    const auto findPort = [this](const std::string& id) {
        return std::find_if(snapshot_.ports.cbegin(), snapshot_.ports.cend(), [&id](const PortInfo& port) {
            return port.id == id;
        });
    };
    const auto source = findPort(sourceId);
    const auto target = findPort(targetId);
    if (source == snapshot_.ports.cend() || target == snapshot_.ports.cend() || source->direction == target->direction || source->kind != target->kind)
        return;
    const auto& output = source->direction == Direction::output ? *source : *target;
    const auto& input = source->direction == Direction::input ? *source : *target;
    const bool exists = std::any_of(snapshot_.connections.cbegin(), snapshot_.connections.cend(), [&output, &input](const ConnectionInfo& connection) {
        return connection.sourcePortId == output.id && connection.destinationPortId == input.id;
    });
    if (!exists) {
        snapshot_.connections.push_back({ output.id, input.id });
        projectDirty_ = true;
        scheduleSnapshotRefresh();
    }
}

void PatchbayCanvas::updateConnectionPaths()
{
    const auto findNode = [this](const std::string& portId) {
        for (auto* item : scene()->items()) {
            if (auto* node = dynamic_cast<CanvasNode*>(item); node != nullptr && node->containsPort(portId))
                return node;
        }
        return static_cast<CanvasNode*>(nullptr);
    };
    for (auto* item : scene()->items()) {
        if (!item->data(1).isValid())
            continue;
        auto* line = qgraphicsitem_cast<QGraphicsPathItem*>(item);
        if (line == nullptr)
            continue;
        const auto sourceId = item->data(1).toString().toStdString();
        const auto destinationId = item->data(2).toString().toStdString();
        const auto* source = findNode(sourceId);
        const auto* destination = findNode(destinationId);
        if (source != nullptr && destination != nullptr)
            line->setPath(connectionPath(source->portPosition(sourceId), destination->portPosition(destinationId)));
    }
}

void PatchbayCanvas::scheduleSnapshotRefresh()
{
    if (snapshotRefreshPending_)
        return;
    snapshotRefreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        snapshotRefreshPending_ = false;
        setSnapshot(snapshot_);
    });
}

void PatchbayCanvas::applySearchFilter()
{
    const auto query = searchQuery_.toLower();
    for (auto* item : scene()->items()) {
        if (!item->data(0).isValid())
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
        const auto id = item->data(0).toString().toStdString();
        if (item->data(4).isValid() && item->data(4).toPointF() == item->pos())
            continue;
        const auto client = std::find_if(snapshot_.clients.begin(), snapshot_.clients.end(), [&id](const ClientInfo& value) {
            return value.id == id;
        });
        if (client != snapshot_.clients.end()) {
            auto settings = patchbaySettings();
            const auto layoutKey = item->data(3).toString();
            settings.setValue(QStringLiteral("layout/%1/x").arg(layoutKey), item->pos().x());
            settings.setValue(QStringLiteral("layout/%1/y").arg(layoutKey), item->pos().y());
            if (layoutKey == QString::fromStdString(id)) {
                client->x = item->pos().x();
                client->y = item->pos().y();
            }
            changed = true;
        }
    }
    if (changed) {
        setSnapshot(snapshot_);
        projectDirty_ = true;
    }
}

void PatchbayCanvas::notifySelection()
{
    if (!nodeSelectionHandler_)
        return;

    for (auto* item : scene()->selectedItems()) {
        if (item->data(0).isValid()) {
            nodeSelectionHandler_(item->data(0).toString().toStdString());
            return;
        }
    }
    nodeSelectionHandler_({});
}