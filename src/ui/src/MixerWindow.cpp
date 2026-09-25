#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>

#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QFontMetricsF>
#include <QRandomGenerator>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

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
    bool fx = false;
    bool mute = false;
    bool solo = false;
    bool postFaderMeter = false;
    std::uint32_t auxId {};
    std::uint32_t id {};
    Common::AuxChannelLayout layout { Common::AuxChannelLayout::mono };
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
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, alignment, text);
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
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, alignment, text);
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

}

class MixerWindow::Impl {
public:
    explicit Impl(MixerWindow& owner)
        : owner(owner)
    {
        channels = {
            { "Mic 1", "Analog In 1", "1", "#45bfff", .66, -5.0, -5.0 },
            { "Mic 2", "Analog In 2", "2", "#45bfff", .56, -9.0, -9.0 },
            { "Line 3", "Line In 3", "3", "#8da5d8", .70, -14.0, -14.0 },
            { "Line 4", "Line In 4", "4", "#8da5d8", .79, -8.0, -8.0 },
            { "USB 5", "USB In 5", "5", "#70c9f4", .62, -11.0, -11.0 },
            { "USB 6", "USB In 6", "6", "#70c9f4", .74, -18.0, -18.0 },
            { "Bus 7", "Bus In 7", "7", "#8da5d8", .58, -7.0, -7.0 },
            { "Bus 8", "Bus In 8", "8", "#8da5d8", .81, -23.0, -23.0 }
        };
        for (auto& channel : channels)
        {
            channel.id = nextChannelId++;
            channel.layout = Common::AuxChannelLayout::stereo;
        }
        rebuildAuxChannels();
        timer.setInterval(55);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] {
            for (auto& channel : channels) {
                if (QRandomGenerator::global()->bounded(12) == 0)
                    channel.target = QRandomGenerator::global()->bounded(-480, 111) / 10.0;
                channel.level += (channel.target - channel.level) * .17;
                channel.level = std::clamp(channel.level + (QRandomGenerator::global()->bounded(7) - 3) / 10.0, -72.0, 12.0);
            }
            this->owner.update();
            updateSel();
        });
        timer.start();
    }

    int auxChannelCount() const
    {
        return static_cast<int>(auxState->buses().size());
    }

    int masterIndex() const
    {
        return inputChannelCount + auxChannelCount();
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
                channel.fader = .68;
                channel.level = -12.0;
                channel.target = -12.0;
                channel.accent = QColor("#33b6ff");
            }
            channel.name = QStringLiteral("AUX %1").arg(static_cast<int>(rebuilt.size()) - inputChannelCount + 1);
            channel.source = bus.name;
            channel.number = channel.name;
            channel.auxId = bus.id;
            channel.id = 1000000 + bus.id;
            channel.layout = bus.targetLayout;
            rebuilt.push_back(std::move(channel));
        }

        const auto existingMaster = std::find_if(channels.cbegin(), channels.cend(), [](const auto& channel) {
            return channel.number == QStringLiteral("MAIN");
        });
        if (existingMaster != channels.cend())
            rebuilt.push_back(*existingMaster);
        else
            rebuilt.push_back({ "Main L/R", "JACK Out 1/2", "MAIN", "#50dfbd", .55, 4.0, 4.0,
                                false, false, false, false, false, 0, 2000000,
                                Common::AuxChannelLayout::stereo });
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

    void showAuxSettings(const Channel& channel)
    {
        if (auxSettingsWindow.isNull()) {
            auxSettingsWindow = new AuxSettingsWindow(auxState, &owner);
            auxSettingsWindow->setAttribute(Qt::WA_DeleteOnClose);
            auxSettingsWindow->setStateChangedCallback([this] {
                rebuildAuxChannels();
                owner.update();
            });
            auxSettingsWindow->setChainEnabledHandler([this](bool enabled) {
                const auto found = std::find_if(channels.begin(), channels.end(), [this](const Channel& item) {
                    return item.id == auxWindowChannelId;
                });
                if (found != channels.end()) { found->aux = enabled; owner.update(); }
            });
        }
        auxWindowChannelId = channel.id;
        const auto sourceKey = channel.auxId == 0
            ? QStringLiteral("input:%1").arg(channel.id)
            : QStringLiteral("aux:%1").arg(channel.auxId);
        auxSettingsWindow->setSource(sourceKey, channel.name, channel.auxId, channel.layout);
        auxSettingsWindow->setChainEnabled(channel.aux);
        if (channel.auxId != 0)
            auxSettingsWindow->selectBus(channel.auxId);
        auxSettingsWindow->show();
        auxSettingsWindow->raise();
        auxSettingsWindow->activateWindow();
    }

    void renumberInputs()
    {
        for (int index = 0; index < inputChannelCount; ++index)
            channels[index].number = QStringLiteral("CH %1").arg(index + 1);
    }

    FxSettingsWindow* fxSettingsFor(int index)
    {
        const auto id = channels[index].id;
        auto& window = fxSettingsWindows[id];
        if (window.isNull()) {
            window = new FxSettingsWindow(channels[index].name,
                fxChannels(channels[index].layout), &owner);
            window->setChainEnabledHandler([this, id](bool enabled) {
                const auto found = std::find_if(channels.begin(), channels.end(), [id](const Channel& item) {
                    return item.id == id;
                });
                if (found != channels.end()) { found->fx = enabled; owner.update(); }
            });
        }
        window->setChainEnabled(channels[index].fx);
        return window;
    }

    void showFxSettings(int index)
    {
        auto* window = fxSettingsFor(index);
        window->show();
        window->raise();
        window->activateWindow();
    }

    static QStringList fxChannels(Common::AuxChannelLayout layout)
    {
        if (layout == Common::AuxChannelLayout::mono) return { QStringLiteral("L") };
        auto labels = Common::auxLayoutChannelLabels(layout);
        labels.replaceInStrings(QStringLiteral("Lrs"), QStringLiteral("Lb"));
        labels.replaceInStrings(QStringLiteral("Rrs"), QStringLiteral("Rb"));
        return labels;
    }

    void addInput(Common::AuxChannelLayout layout)
    {
        Channel channel { QStringLiteral("Input %1").arg(nextChannelId),
                          QStringLiteral("Input %1").arg(nextChannelId),
                          QString::number(nextChannelId), QColor("#45bfff"),
                          .66, -12.0, -12.0 };
        channel.id = nextChannelId++;
        channel.layout = layout;
        channels.insert(channels.begin() + inputChannelCount, channel);
        ++inputChannelCount;
        renumberInputs();
        owner.update();
    }

    void removeChannel(int index)
    {
        if (index == masterIndex() || (index < inputChannelCount && inputChannelCount <= 1)
            || (index >= inputChannelCount && auxChannelCount() <= 1)) return;
        const auto channel = channels[index];
        if (auto found = fxSettingsWindows.find(channel.id); found != fxSettingsWindows.end()) {
            if (found->second) { found->second->close(); found->second->deleteLater(); }
            fxSettingsWindows.erase(found);
        }
        if (auxWindowChannelId == channel.id && auxSettingsWindow) {
            auxSettingsWindow->close();
            auxWindowChannelId = 0;
        }
        if (channel.auxId) {
            auxState->removeBus(channel.auxId);
            rebuildAuxChannels();
        } else {
            channels.erase(channels.begin() + index);
            --inputChannelCount;
            renumberInputs();
        }
        if (selectedChannelId == channel.id) {
            selectedChannelId = channels.empty() ? 0 : channels.front().id;
            updateSel();
        }
        owner.update();
    }

    void copyChannel(int index)
    {
        if (index == masterIndex()) return;
        const auto sourceId = channels[index].id;
        std::uint32_t copiedId {};
        if (channels[index].auxId) {
            auto* bus = auxState->cloneBus(channels[index].auxId);
            if (!bus) return;
            rebuildAuxChannels();
            const auto copied = std::find_if(channels.begin(), channels.end(), [bus](const Channel& item) {
                return item.auxId == bus->id;
            });
            if (copied != channels.end()) {
                const auto id = copied->id;
                *copied = channels[index];
                copied->id = id;
                copied->auxId = bus->id;
                copied->name = QStringLiteral("AUX %1").arg(auxChannelCount());
                copied->number = copied->name;
                copiedId = copied->id;
            }
        } else {
            Channel copied = channels[index];
            copied.id = nextChannelId++;
            copied.name += QStringLiteral(" 副本");
            copied.number = QString::number(copied.id);
            copiedId = copied.id;
            channels.insert(channels.begin() + index + 1, copied);
            ++inputChannelCount;
            renumberInputs();
        }
        if (copiedId) {
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
        owner.update();
    }

    void setChannelLayout(int index, Common::AuxChannelLayout layout)
    {
        if (index == masterIndex() || channels[index].layout == layout)
            return;
        const auto id = channels[index].id;
        if (channels[index].auxId) {
            auxState->setBusLayout(channels[index].auxId, layout);
            rebuildAuxChannels();
        } else {
            channels[index].layout = layout;
        }
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
                    auxSettingsWindow->setSource(key, found->name, found->auxId, layout);
                }
            } else {
                auxSettingsWindow->update();
            }
        }
        owner.update();
    }

    void updateSel()
    {
        if (!selLabel) return;
        const auto found = std::find_if(channels.cbegin(), channels.cend(), [this](const Channel& item) {
            return item.id == selectedChannelId;
        });
        selLabel->setText(found == channels.cend() ? QStringLiteral("未选择通道") : found->name);
    }

    void selectChannel(int index)
    {
        selectedChannelId = channels[index].id;
        updateSel();
        owner.update();
    }

    void showLargeScreen()
    {
        if (!selWindow) {
            selWindow = new QWidget(&owner, Qt::Window);
            selWindow->setAttribute(Qt::WA_DeleteOnClose);
            selWindow->setObjectName(QStringLiteral("selLargeScreen"));
            selWindow->setWindowTitle(QStringLiteral("SEL 大屏"));
            selWindow->resize(1100, 680);
            selWindow->setStyleSheet(QStringLiteral("background:#07131f; color:#e9f2ff;"));
            auto* layout = new QVBoxLayout(selWindow);
            selLabel = new QLabel(selWindow);
            selLabel->setObjectName(QStringLiteral("selSelectedChannel"));
            selLabel->setAlignment(Qt::AlignCenter);
            QFont font = selLabel->font(); font.setPixelSize(44); font.setBold(true); selLabel->setFont(font);
            layout->addWidget(selLabel);
        }
        updateSel();
        selWindow->show();
        selWindow->raise();
        selWindow->activateWindow();
    }

    QRectF channelRect(int index) const
    {
        qreal x = 0.0;
        if (index < inputChannelCount)
            x = inputViewport().left() + index * channelWidth - effectiveInputScrollOffset();
        else if (index < masterIndex())
            x = auxViewport().left() + (index - inputChannelCount) * channelWidth - effectiveAuxScrollOffset();
        else
            x = owner.width() - margin - channelWidth;
        return { x, topMargin, channelWidth, channelAreaHeight() };
    }

    QRectF inputViewport() const
    {
        const auto available = scrollableAreaWidth();
        const auto inputWidth = std::floor(available * effectiveInputRatio());
        return { margin, topMargin, inputWidth, channelAreaHeight() };
    }

    QRectF auxViewport() const
    {
        const auto input = inputViewport();
        const auto width = std::max(0.0, scrollableAreaWidth() - input.width());
        return { input.right() + groupGap, topMargin, width, channelAreaHeight() };
    }

    QRectF viewportForChannel(int index) const
    {
        return index < inputChannelCount ? inputViewport() : auxViewport();
    }

    qreal channelAreaHeight() const
    {
        return std::max(0.0, owner.height() - topMargin - margin - scrollbarHeight - scrollbarGap);
    }

    qreal scrollableAreaWidth() const
    {
        const auto masterLeft = owner.width() - margin - channelWidth;
        return std::max(0.0, masterLeft - groupGap - margin - groupGap);
    }

    qreal effectiveInputRatio() const
    {
        const auto available = scrollableAreaWidth();
        if (available <= 0.0)
            return .5;
        const auto minimumRatio = std::min(.5, channelWidth / available);
        return std::clamp(inputRatio, minimumRatio, 1.0 - minimumRatio);
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

    QRectF scrollbarTrack(bool input) const
    {
        const auto viewport = input ? inputViewport() : auxViewport();
        return { viewport.left() + 4.0, viewport.bottom() + scrollbarGap,
                 std::max(0.0, viewport.width() - 8.0), scrollbarHeight };
    }

    QRectF scrollbarThumb(bool input) const
    {
        const auto track = scrollbarTrack(input);
        const auto channelCount = input ? inputChannelCount : auxChannelCount();
        const auto contentWidth = channelCount * channelWidth;
        const auto viewportWidth = input ? inputViewport().width() : auxViewport().width();
        const auto maximum = input ? maximumInputScrollOffset() : maximumAuxScrollOffset();
        const auto offset = input ? effectiveInputScrollOffset() : effectiveAuxScrollOffset();
        const auto thumbWidth = maximum <= 0.0 ? track.width()
            : std::max(32.0, track.width() * viewportWidth / contentWidth);
        const auto travel = std::max(0.0, track.width() - thumbWidth);
        const auto x = track.left() + (maximum <= 0.0 ? 0.0 : travel * offset / maximum);
        return { x, track.top(), thumbWidth, track.height() };
    }

    void setScrollFromThumb(bool input, qreal thumbLeft)
    {
        const auto track = scrollbarTrack(input);
        const auto thumb = scrollbarThumb(input);
        const auto travel = std::max(0.0, track.width() - thumb.width());
        const auto maximum = input ? maximumInputScrollOffset() : maximumAuxScrollOffset();
        const auto normalized = travel <= 0.0 ? 0.0
            : std::clamp((thumbLeft - track.left()) / travel, 0.0, 1.0);
        if (input)
            inputScrollOffset = normalized * maximum;
        else
            auxScrollOffset = normalized * maximum;
    }

    QRectF faderTrack(int index) const
    {
        const auto card = channelRect(index);
        const auto top = card.top() + card.height() * .64;
        return { card.center().x() - 3.0, top, 6.0, card.height() * .23 };
    }

    QRectF meterRect(int index, qreal heightScale) const
    {
        const auto card = channelRect(index);
        const auto headerHeight = 42.0 * heightScale;
        const auto top = card.top() + headerHeight + 65.0 * heightScale;
        return { card.center().x() - 8.0, top, 16.0, faderTrack(index).height() };
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

    int hitChannel(const QPointF& position) const
    {
        if (inputViewport().contains(position)) {
            for (int index = 0; index < inputChannelCount; ++index) {
                if (channelRect(index).contains(position))
                    return index;
            }
        }
        if (auxViewport().contains(position)) {
            for (int index = inputChannelCount; index < masterIndex(); ++index) {
                if (channelRect(index).contains(position))
                    return index;
            }
        }
        return channelRect(masterIndex()).contains(position) ? masterIndex() : -1;
    }

    MixerWindow& owner;
    std::shared_ptr<Common::AuxMixerState> auxState { std::make_shared<Common::AuxMixerState>() };
    QPointer<AuxSettingsWindow> auxSettingsWindow;
    QPointer<QWidget> selWindow;
    QPointer<QLabel> selLabel;
    std::uint32_t selectedChannelId { 1 };
    std::map<int, QPointer<FxSettingsWindow>> fxSettingsWindows;
    std::vector<Channel> channels;
    QTimer timer;
    int dragging = -1;
    int scrollbarDragging = 0;
    bool groupDividerDragging = false;
    qreal scrollbarDragOffset = 0.0;
    qreal inputScrollOffset = 0.0;
    qreal auxScrollOffset = 0.0;
    qreal inputRatio = .6;
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
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(*this))
{
    setWindowTitle(QStringLiteral("WinJACKNexus - 调音台"));
    setMinimumSize(760, 560);
    resize(1024, 768);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    auto* menuBar = this->menuBar();
    menuBar->setObjectName(QStringLiteral("mixerMenuBar"));
    auto* channelsMenu = menuBar->addMenu(QStringLiteral("通道管理"));
    auto* inputsMenu = channelsMenu->addMenu(QStringLiteral("添加输入通道"));
    auto* auxMenu = channelsMenu->addMenu(QStringLiteral("添加AUX通道"));
    const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                               Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
    for (const auto layout : layouts) {
        connect(inputsMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->addInput(layout); });
        connect(auxMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->auxState->addBus(layout); impl_->rebuildAuxChannels(); update(); });
    }
    auto* globalMenu = menuBar->addMenu(QStringLiteral("全局"));
    connect(globalMenu->addAction(QStringLiteral("关闭所有通道独奏/静音")), &QAction::triggered, this, [this] {
        for (auto& channel : impl_->channels) { channel.solo = false; channel.mute = false; }
        update();
    });
    connect(menuBar->addAction(QStringLiteral("查看大屏")), &QAction::triggered, this,
            [this] { impl_->showLargeScreen(); });
}

MixerWindow::~MixerWindow() = default;

void MixerWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#07131f"));

    const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
    const auto headerHeight = 42.0 * heightScale;
    const auto label = QColor("#dce9ff");
    const auto secondary = QColor("#a9bbd7");
    const auto groups = std::array<std::tuple<int, QString, QColor>, 3> {{
        { 0, QStringLiteral("♩  Inputs"), QColor("#1977d3") },
        { impl_->inputChannelCount, QStringLiteral("◇  AUX Sends"), QColor("#654dd4") },
        { impl_->masterIndex(), QStringLiteral("◖  Main Output"), QColor("#1ca47f") }
    }};

    for (const auto& [start, title, color] : groups) {
        painter.save();
        const auto group = start == impl_->masterIndex()
            ? impl_->channelRect(start)
            : impl_->viewportForChannel(start);
        QPainterPath outline;
        outline.addRoundedRect(group, 7.0, 7.0);
        painter.setPen(QPen(withAlpha(color, 210), 1.2));
        painter.setBrush(withAlpha(color, 22));
        painter.drawPath(outline);
        const QRectF header(group.left(), group.top(), group.width(), headerHeight);
        painter.fillRect(header.adjusted(1, 1, -1, 0), withAlpha(color, 75));
        drawAdaptiveText(painter, header.adjusted(4, 0, -4, 0), title, label,
                 18 * heightScale, 10 * heightScale, Qt::AlignCenter, true);
        painter.restore();
    }

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
        if (index != 0 && index != impl_->inputChannelCount && index != impl_->masterIndex()) {
            painter.setPen(QPen(QColor("#243c57"), 1));
            painter.drawLine(QPointF(card.left(), card.top() + headerHeight), QPointF(card.left(), card.bottom()));
        }

        const auto top = card.top() + headerHeight + 2 * heightScale;
        drawAdaptiveText(painter, { card.left(), top, card.width(), 20 * heightScale }, channel.name,
                 label, 16 * heightScale, 10 * heightScale, Qt::AlignCenter, true);
        drawAdaptiveText(painter, { card.left(), top + 26 * heightScale, card.width(), 14 * heightScale }, channel.source,
                 secondary, 12 * heightScale, 8 * heightScale);

        const auto meter = impl_->meterRect(index, heightScale);
        const auto meterMode = impl_->meterModeRect(index, heightScale);
        const auto meterLevel = channel.postFaderMeter
            ? std::clamp(channel.level + faderDbValue(channel.fader), -72.0, 12.0)
            : channel.level;
        painter.setPen(QPen(channel.postFaderMeter ? QColor("#33b6ff") : QColor("#6f849a"), 1.0));
        painter.setBrush(channel.postFaderMeter ? QColor("#163a51") : QColor("#182735"));
        painter.drawRoundedRect(meterMode, 4, 4);
        drawText(painter, meterMode, channel.postFaderMeter ? QStringLiteral("POST") : QStringLiteral("IN"),
                 channel.postFaderMeter ? QColor("#8edcff") : secondary, 9 * heightScale, Qt::AlignCenter, true);
        painter.setPen(QPen(QColor("#0a0f14"), 4));
        painter.setBrush(QColor("#101a22"));
        painter.drawRoundedRect(meter, 3, 3);
        const auto innerMeter = meter.adjusted(2.5, 2.5, -2.5, -2.5);
        const auto yForDb = [&](qreal db) {
            const auto normalized = std::clamp((db + 72.0) / 84.0, 0.0, 1.0);
            return innerMeter.bottom() - normalized * innerMeter.height();
        };
        const auto segments = std::array<std::tuple<qreal, qreal, QColor>, 3> {{
            { -72.0, -12.0, QColor("#26dc68") },
            { -12.0, 0.0, QColor("#ffe15a") },
            { 0.0, 12.0, QColor("#ff5353") }
        }};
        for (const auto& [minimum, maximum, color] : segments) {
            const QRectF segment(innerMeter.left(), yForDb(maximum), innerMeter.width(), yForDb(minimum) - yForDb(maximum));
            painter.fillRect(segment, withAlpha(color, 38));
            const auto activeMaximum = std::clamp(meterLevel, minimum, maximum);
            if (activeMaximum > minimum) {
                const QRectF active(innerMeter.left(), yForDb(activeMaximum), innerMeter.width(), yForDb(minimum) - yForDb(activeMaximum));
                painter.fillRect(active, color);
            }
        }
        painter.setPen(QPen(QColor("#07131f"), 1));
        painter.drawLine(QPointF(innerMeter.left(), yForDb(-12.0)), QPointF(innerMeter.right(), yForDb(-12.0)));
        painter.drawLine(QPointF(innerMeter.left(), yForDb(0.0)), QPointF(innerMeter.right(), yForDb(0.0)));
        const auto meterTicks = std::array<int, 8> { 12, 0, -12, -24, -36, -48, -60, -72 };
        for (const auto tick : meterTicks) {
            const auto y = yForDb(tick);
            const auto text = tick > 0 ? QStringLiteral("+%1").arg(tick) : QString::number(tick);
            drawText(painter, { card.left() + 2, y - 8, card.width() * .34, 16 }, text, secondary, 9 * heightScale, Qt::AlignRight | Qt::AlignVCenter);
        }

        const auto buttonTop = meter.bottom() + 20 * heightScale;
        const auto buttonHeight = 28 * heightScale;
        const auto side = 6.0;
        const auto halfWidth = (card.width() - side * 3) / 2.0;
        const auto drawButton = [&](const QRectF& button, const QString& text, const QColor& color, bool active) {
            painter.setPen(QPen(active ? color : withAlpha(color, 190), 1.1));
            painter.setBrush(active ? withAlpha(color, 65) : QColor("#16283a"));
            painter.drawRoundedRect(button, 5, 5);
            drawText(painter, button, text, active ? QColor("#ffffff") : label, 12 * heightScale, Qt::AlignCenter, true);
        };
        if (index == impl_->masterIndex()) {
            drawButton({ card.left() + side, buttonTop + buttonHeight, halfWidth, buttonHeight }, "FX", QColor("#9b61ff"), channel.fx);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop + buttonHeight, halfWidth, buttonHeight }, "⚙", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 2.8, card.width() - side * 2, buttonHeight }, "SEL", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 4.3, card.width() - side * 2, buttonHeight }, "M", QColor("#ff4f55"), channel.mute);
        } else {
            drawButton({ card.left() + side, buttonTop, halfWidth, buttonHeight }, "AUX", QColor("#33b6ff"), channel.aux);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop, halfWidth, buttonHeight }, "⚙", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 1.35, halfWidth, buttonHeight }, "FX", QColor("#9b61ff"), channel.fx);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 1.35, halfWidth, buttonHeight }, "⚙", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 2.85, card.width() - side * 2, buttonHeight }, "SEL", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 4.35, halfWidth, buttonHeight }, "M", QColor("#ff4f55"), channel.mute);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 4.35, halfWidth, buttonHeight }, "S", QColor("#f7aa2f"), channel.solo);
        }

        const auto faderColor = index < impl_->inputChannelCount
            ? QColor("#9aa8b7")
            : index < impl_->masterIndex() ? QColor("#33b6ff") : QColor("#ff5353");
        const auto track = impl_->faderTrack(index);
        painter.setPen(QPen(QColor("#0a0f15"), 7, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.center().x(), track.top(), track.center().x(), track.bottom());
        painter.setPen(QPen(faderColor, 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.center().x(), track.bottom(), track.center().x(), track.top() + track.height() * channel.fader);
        const auto thumbY = track.top() + track.height() * channel.fader;
        QRectF thumb(card.center().x() - card.width() * .13, thumbY - 20 * heightScale, card.width() * .26, 40 * heightScale);
        QLinearGradient thumbGradient(thumb.topLeft(), thumb.bottomRight());
        thumbGradient.setColorAt(0, faderColor.lighter(130));
        thumbGradient.setColorAt(1, faderColor.darker(125));
        painter.setPen(QPen(faderColor.lighter(135)));
        painter.setBrush(thumbGradient);
        painter.drawRoundedRect(thumb, 5, 5);
        painter.setPen(QPen(withAlpha(QColor("#07131f"), 130), 1));
        painter.drawLine(thumb.left() + 3, thumb.center().y(), thumb.right() - 3, thumb.center().y());

        const auto db = faderDbText(channel.fader);
        drawText(painter, { card.left(), track.bottom() + 10 * heightScale, card.width(), 27 * heightScale }, db, label, 12 * heightScale);
        drawText(painter, { card.left(), card.bottom() - 35 * heightScale, card.width(), 28 * heightScale }, channel.number, channel.accent.lighter(125), 18 * heightScale, Qt::AlignCenter, true);
        painter.restore();
    }

    const auto drawScrollbar = [&](bool input, const QColor& color) {
        const auto track = impl_->scrollbarTrack(input);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#101c29"));
        painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
        painter.setBrush(withAlpha(color, 210));
        const auto thumb = impl_->scrollbarThumb(input).adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(thumb, thumb.height() / 2.0, thumb.height() / 2.0);
    };
    drawScrollbar(true, QColor("#45bfff"));
    drawScrollbar(false, QColor("#8f78ff"));

    const auto divider = impl_->groupDividerRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#243c57"));
    painter.drawRoundedRect(QRectF(divider.center().x() - 1.0, divider.top() + headerHeight * .25,
                                   2.0, headerHeight * .5), 1.0, 1.0);
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
        auto* fx = menu.addAction(QStringLiteral("打开FX设置"));
        auto* aux = menu.addAction(QStringLiteral("打开AUX设置"));
        aux->setEnabled(index != impl_->masterIndex());
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
        connect(fx, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->showFxSettings(current); });
        });
        connect(aux, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->showAuxSettings(impl_->channels[current]); });
        });
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
        update();
        return;
    }

    for (const auto input : { true, false }) {
        const auto thumb = impl_->scrollbarThumb(input).adjusted(-2, -3, 2, 3);
        const auto track = impl_->scrollbarTrack(input).adjusted(0, -3, 0, 3);
        if (thumb.contains(event->position())) {
            impl_->scrollbarDragging = input ? 1 : 2;
            impl_->scrollbarDragOffset = event->position().x() - thumb.left();
            return;
        }
        if (track.contains(event->position())) {
            impl_->setScrollFromThumb(input, event->position().x() - impl_->scrollbarThumb(input).width() / 2.0);
            update();
            return;
        }
    }

    const auto index = impl_->hitChannel(event->position());
    if (index < 0)
        return;

    const auto card = impl_->channelRect(index);
    const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
    if (impl_->meterHitRect(index, heightScale).contains(event->position())) {
        impl_->channels[index].postFaderMeter = !impl_->channels[index].postFaderMeter;
        update();
        return;
    }
    const auto track = impl_->faderTrack(index).adjusted(-card.width() * .18, -20, card.width() * .18, 20);
    if (track.contains(event->position())) {
        impl_->dragging = index;
        mouseMoveEvent(event);
        return;
    }

    auto& channel = impl_->channels[index];
    const auto headerHeight = 42.0 * heightScale;
    const auto buttonTop = impl_->meterRect(index, heightScale).bottom() + 20 * heightScale;
    const auto buttonHeight = 28 * heightScale;
    const auto side = 6.0;
    const auto halfWidth = (card.width() - side * 3) / 2.0;
    const auto leftButton = [&](qreal row) {
        return QRectF(card.left() + side, buttonTop + buttonHeight * row, halfWidth, buttonHeight);
    };
    if (index == impl_->masterIndex()) {
        const QRectF fxButton(card.left() + side, buttonTop + buttonHeight, halfWidth, buttonHeight);
        const QRectF settingsButton(card.left() + side * 2 + halfWidth, buttonTop + buttonHeight,
                                    halfWidth, buttonHeight);
        if (fxButton.contains(event->position())) {
            channel.fx = !channel.fx;
            if (auto window = impl_->fxSettingsWindows[channel.id]) window->setChainEnabled(channel.fx);
        }
        else if (settingsButton.contains(event->position()))
            impl_->showFxSettings(index);
        else if (QRectF(card.left() + side, buttonTop + buttonHeight * 2.8,
                        card.width() - side * 2, buttonHeight).contains(event->position()))
            impl_->selectChannel(index);
        else if (QRectF(card.left() + side, buttonTop + buttonHeight * 4.3,
                        card.width() - side * 2, buttonHeight).contains(event->position()))
            channel.mute = !channel.mute;
    } else if (QRectF(card.left() + side * 2 + halfWidth, buttonTop,
                      halfWidth, buttonHeight).contains(event->position())) {
        impl_->showAuxSettings(channel);
    } else if (QRectF(card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 1.35,
                      halfWidth, buttonHeight).contains(event->position())) {
        impl_->showFxSettings(index);
    } else if (leftButton(0).contains(event->position())) {
        channel.aux = !channel.aux;
        if (impl_->auxSettingsWindow && impl_->auxWindowChannelId == channel.id)
            impl_->auxSettingsWindow->setChainEnabled(channel.aux);
    } else if (leftButton(1.35).contains(event->position())) {
        channel.fx = !channel.fx;
        if (auto window = impl_->fxSettingsWindows[channel.id]) window->setChainEnabled(channel.fx);
    } else if (QRectF(card.left() + side, buttonTop + buttonHeight * 2.85,
                      card.width() - side * 2, buttonHeight).contains(event->position())) {
        impl_->selectChannel(index);
    } else if (leftButton(4.35).contains(event->position())) {
        channel.mute = !channel.mute;
    } else if (QRectF(card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 4.35,
                      halfWidth, buttonHeight).contains(event->position())) {
        channel.solo = !channel.solo;
    }
    update();
}

void MixerWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (impl_->groupDividerDragging && (event->buttons() & Qt::LeftButton)) {
        impl_->setInputRatioFromPosition(event->position().x() - Impl::groupGap / 2.0);
        update();
        return;
    }
    if (impl_->scrollbarDragging != 0 && (event->buttons() & Qt::LeftButton)) {
        const auto input = impl_->scrollbarDragging == 1;
        impl_->setScrollFromThumb(input, event->position().x() - impl_->scrollbarDragOffset);
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        if (impl_->groupDividerRect().adjusted(-3, 0, 3, 0).contains(event->position())) {
            setCursor(Qt::SplitHCursor);
        } else {
            const auto index = impl_->hitChannel(event->position());
            const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
            setCursor(index >= 0 && impl_->meterHitRect(index, heightScale).contains(event->position())
                ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        return;
    }
    if (impl_->dragging < 0)
        return;
    const auto track = impl_->faderTrack(impl_->dragging);
    impl_->channels[impl_->dragging].fader = std::clamp((event->position().y() - track.top()) / track.height(), 0.0, 1.0);
    update();
}

void MixerWindow::mouseReleaseEvent(QMouseEvent*)
{
    impl_->dragging = -1;
    impl_->scrollbarDragging = 0;
    impl_->groupDividerDragging = false;
}

void MixerWindow::wheelEvent(QWheelEvent* event)
{
    const auto input = impl_->inputViewport().contains(event->position());
    const auto aux = impl_->auxViewport().contains(event->position());
    if (!input && !aux) {
        QWidget::wheelEvent(event);
        return;
    }

    const auto angle = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
    if (input)
        impl_->inputScrollOffset = std::clamp(impl_->effectiveInputScrollOffset() - angle / 120.0 * 54.0,
                                             0.0, impl_->maximumInputScrollOffset());
    else
        impl_->auxScrollOffset = std::clamp(impl_->effectiveAuxScrollOffset() - angle / 120.0 * 54.0,
                                           0.0, impl_->maximumAuxScrollOffset());
    update();
    event->accept();
}

}
