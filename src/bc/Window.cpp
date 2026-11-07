// SPDX-License-Identifier: AGPL-3.0-only
#include "Window.hpp"
#include "Node.hpp"
#include "Service.hpp"
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonDocument>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MidiToolsTheme.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace WinJACKNexus::Broadcast
{
namespace UI = WinJACKNexus::UI;
namespace C = UI::Controls;
namespace
{
constexpr auto scope = "global";
QString text(const char *key, const char *english, const QVariantMap &arguments = {})
{
    return UI::localizedText("broadcast", key, english, arguments);
}
void bind(QObject *object, const char *property, const char *key, const char *english)
{
    UI::bindLocalizedText(object, property, "broadcast", key, english);
}
C::ActionButton *button(QWidget *parent, const char *name, const char *key, const char *english)
{
    auto *result = new C::ActionButton({}, parent);
    result->setObjectName(QString::fromLatin1(name));
    bind(result, "text", key, english);
    return result;
}
C::TextLabel *label(QWidget *parent, const char *key, const char *english)
{
    auto *result = new C::TextLabel({}, parent);
    bind(result, "text", key, english);
    return result;
}
QByteArray structure(Model::State state)
{
    for (auto &stream : state.streams)
    {
        stream.gainDb = 0;
        stream.mute = false;
    }
    return QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact);
}
} // namespace

class BroadcastWindow::Impl
{
  public:
    struct Strip
    {
        QString id;
        UI::FxLevelMeterWidget *meter;
        C::ConsoleKnob *gain;
        C::ActionButton *mute, *solo, *listen;
    };

    BroadcastWindow &window;
    BroadcastNode &node;
    QTimer refreshTimer;
    C::TextField *server, *publisher, *token, *name;
    C::DragTreeWidget *tree;
    C::ChoiceField *parent, *layout, *channel, *encoding;
    C::TextLabel *status, *metrics, *diagnostic;
    C::ActionButton *publish, *remove;
    QWidget *strips;
    QHBoxLayout *stripLayout;
    std::vector<Strip> cards;
    QByteArray previousStructure;
    quint64 revision = std::numeric_limits<quint64>::max();
    QString selection;
    bool selectedCategory = false;

    Impl(BroadcastWindow &owner, BroadcastNode &model) : window(owner), node(model)
    {
        window.setObjectName(QStringLiteral("broadcastWindow"));
        window.resize(1280, 800);
        window.setMinimumSize(980, 680);
        bind(&window, "windowTitle", "name", "Broadcast Sender");
        auto *root = new QVBoxLayout(&window);
        auto *connection = new QHBoxLayout;
        server = new C::TextField(&window);
        server->setObjectName(QStringLiteral("bcServer"));
        publisher = new C::TextField(&window);
        publisher->setObjectName(QStringLiteral("bcPublisher"));
        token = new C::TextField(&window);
        token->setObjectName(QStringLiteral("bcToken"));
        token->setEchoMode(QLineEdit::Password);
        bind(token, "placeholderText", "token.placeholder", "New token (blank keeps saved credential)");
        connection->addWidget(label(&window, "server", "Server"));
        connection->addWidget(server, 3);
        connection->addWidget(label(&window, "publisher", "Publisher"));
        connection->addWidget(publisher, 1);
        connection->addWidget(token, 2);
        auto *save = button(&window, "bcSaveServer", "server.save", "Save server");
        connection->addWidget(save);
        publish = button(&window, "bcPublish", "publish.start", "Start publishing");
        connection->addWidget(publish);
        root->addLayout(connection);
        status = new C::TextLabel({}, &window);
        status->setObjectName(QStringLiteral("bcStatus"));
        root->addWidget(status);

        auto *body = new QHBoxLayout;
        auto *management = new C::GroupPanel({}, &window);
        management->setFixedWidth(290);
        bind(management, "title", "streams", "Stream management");
        auto *sidebar = new QVBoxLayout(management);
        tree = new C::DragTreeWidget(management);
        tree->setObjectName(QStringLiteral("bcTree"));
        tree->setHeaderHidden(true);
        tree->setDragEnabled(false);
        tree->setDragDropMode(QAbstractItemView::NoDragDrop);
        sidebar->addWidget(tree, 1);
        name = new C::TextField(management);
        name->setObjectName(QStringLiteral("bcName"));
        bind(name, "placeholderText", "item.name", "Stream or category name");
        sidebar->addWidget(name);
        parent = new C::ChoiceField(management);
        parent->setObjectName(QStringLiteral("bcParent"));
        sidebar->addWidget(label(management, "category", "Category / parent"));
        sidebar->addWidget(parent);
        layout = new C::ChoiceField(management);
        layout->setObjectName(QStringLiteral("bcLayout"));
        for (const auto value : {Model::Layout::mono, Model::Layout::stereo, Model::Layout::surround51,
                                 Model::Layout::surround71})
            layout->addItem(Model::layoutName(value), unsigned(value));
        layout->setCurrentIndex(1);
        sidebar->addWidget(label(management, "layout", "Audio layout"));
        sidebar->addWidget(layout);
        auto *addCategory = button(management, "bcAddCategory", "category.add", "Add category");
        auto *addStream = button(management, "bcAddStream", "stream.add", "Add stream");
        auto *apply = button(management, "bcApplyItem", "item.apply", "Apply name / category / layout");
        remove = button(management, "bcRemoveItem", "item.remove", "Delete selected item");
        for (auto *action : {addCategory, addStream, apply, remove})
            sidebar->addWidget(action);
        body->addWidget(management);

        auto *right = new QVBoxLayout;
        auto *scroll = new C::ScrollField(&window);
        scroll->setWidgetResizable(true);
        strips = new QWidget(scroll);
        stripLayout = new QHBoxLayout(strips);
        stripLayout->setAlignment(Qt::AlignLeft);
        scroll->setWidget(strips);
        right->addWidget(scroll, 1);
        auto *subscription = new QHBoxLayout;
        channel = new C::ChoiceField(&window);
        channel->setObjectName(QStringLiteral("bcListenChannel"));
        encoding = new C::ChoiceField(&window);
        encoding->setObjectName(QStringLiteral("bcListenFormat"));
        encoding->addItem(QStringLiteral("Opus"), QStringLiteral("opus"));
        encoding->addItem(QStringLiteral("WAV / PCM16"), QStringLiteral("wav"));
        subscription->addWidget(label(&window, "listen.address", "Selected stream listening URL"));
        subscription->addWidget(channel);
        subscription->addWidget(encoding);
        auto *copy = button(&window, "bcCopyUrl", "listen.copy", "Copy URL");
        subscription->addWidget(copy);
        right->addLayout(subscription);
        body->addLayout(right, 1);
        root->addLayout(body, 1);
        metrics = new C::TextLabel({}, &window);
        metrics->setObjectName(QStringLiteral("bcMetrics"));
        root->addWidget(metrics);
        diagnostic = new C::TextLabel({}, &window);
        diagnostic->setObjectName(QStringLiteral("bcDiagnostic"));
        diagnostic->setWordWrap(true);
        root->addWidget(diagnostic);

        auto &service = Service::instance();
        QObject::connect(
            save, &QPushButton::clicked, &window,
            [this, &service]
            {
                auto settings = service.connection();
                settings.serverBaseUrl = QUrl(server->text().trimmed());
                settings.publisherId = publisher->text().trimmed();
                QString error;
                if (!service.configure(settings, token->text().toUtf8(), &error))
                    diagnostic->setText(error);
                else
                {
                    token->clear();
                    diagnostic->setText(
                        text("server.saved", "Server saved for all broadcast windows; publishing stopped."));
                }
            });
        QObject::connect(publish, &QPushButton::clicked, &window,
                         [this, &service]
                         {
                             if (service.status().state != QStringLiteral("stopped") &&
                                 service.status().state != QStringLiteral("error") &&
                                 service.status().state != QStringLiteral("authentication-error") &&
                                 service.status().state != QStringLiteral("identity-conflict"))
                                 service.stop();
                             else
                             {
                                 QString error;
                                 if (!service.start(&error))
                                     diagnostic->setText(error);
                             }
                         });
        QObject::connect(&service, &Service::configurationChanged, &window, [this] { connectionFields(); });
        QObject::connect(tree, &QTreeWidget::currentItemChanged, &window,
                         [this](QTreeWidgetItem *item)
                         {
                             selection = item ? item->data(0, Qt::UserRole).toString() : QString();
                             selectedCategory = item && item->data(0, Qt::UserRole + 1).toBool();
                             selectItem();
                         });
        QObject::connect(addCategory, &QPushButton::clicked, &window,
                         [this]
                         {
                             auto state = node.state();
                             selection = state.addCategory(name->text().trimmed().isEmpty()
                                                               ? text("category.new", "New category")
                                                               : name->text().trimmed(),
                                                           parent->currentData().toString());
                             selectedCategory = true;
                             commit(std::move(state));
                         });
        QObject::connect(addStream, &QPushButton::clicked, &window,
                         [this]
                         {
                             auto state = node.state();
                             selection = state.addStream(name->text().trimmed().isEmpty()
                                                             ? text("stream.new", "New stream")
                                                             : name->text().trimmed(),
                                                         Model::Layout(layout->currentData().toUInt()),
                                                         parent->currentData().toString());
                             selectedCategory = false;
                             commit(std::move(state));
                         });
        QObject::connect(apply, &QPushButton::clicked, &window,
                         [this]
                         {
                             auto state = node.state();
                             if (selectedCategory)
                             {
                                 for (auto &category : state.categories)
                                     if (category.id == selection)
                                     {
                                         category.name = name->text().trimmed();
                                         category.parent = parent->currentData().toString();
                                     }
                             }
                             else
                                 for (auto &stream : state.streams)
                                     if (stream.id == selection)
                                     {
                                         stream.name = name->text().trimmed();
                                         stream.category = parent->currentData().toString();
                                         stream.layout = Model::Layout(layout->currentData().toUInt());
                                     }
                             commit(std::move(state));
                         });
        QObject::connect(
            remove, &QPushButton::clicked, &window,
            [this]
            {
                auto state = node.state();
                if (selectedCategory)
                {
                    if (std::any_of(state.categories.begin(), state.categories.end(),
                                    [this](const auto &item) { return item.parent == selection; }) ||
                        std::any_of(state.streams.begin(), state.streams.end(),
                                    [this](const auto &item) { return item.category == selection; }))
                    {
                        diagnostic->setText(
                            text("category.notEmpty", "Move or delete category contents first."));
                        return;
                    }
                    std::erase_if(state.categories,
                                  [this](const auto &item) { return item.id == selection; });
                }
                else
                {
                    if (state.streams.size() <= 1)
                        return;
                    std::erase_if(state.streams, [this](const auto &item) { return item.id == selection; });
                }
                selection.clear();
                commit(std::move(state));
            });
        QObject::connect(
            copy, &QPushButton::clicked, &window,
            [this]
            {
                if (selectedCategory || selection.isEmpty())
                    return;
                auto url = Service::instance().connection().serverBaseUrl;
                url.setPath(QStringLiteral("/bc/listen"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("publisher"), Service::instance().connection().publisherId);
                query.addQueryItem(QStringLiteral("stream"), selection);
                query.addQueryItem(QStringLiteral("channel"), channel->currentData().toString());
                query.addQueryItem(QStringLiteral("format"), encoding->currentData().toString());
                url.setQuery(query);
                QApplication::clipboard()->setText(url.toString(QUrl::FullyEncoded));
                diagnostic->setText(text("listen.copied", "Listening URL copied (no credential)."));
            });
        refreshTimer.setParent(&window);
        UI::bindRefreshTimer(refreshTimer, &window);
        refreshTimer.setObjectName(QStringLiteral("bcRefreshTimer"));
        QObject::connect(&refreshTimer, &QTimer::timeout, &window, [this] { refresh(); });
        UI::bindMidiToolsTheme(&window, QString::fromLatin1(scope));
        const auto language = [this]
        {
            layout->setItemText(0, text("layout.mono", "Mono"));
            layout->setItemText(1, text("layout.stereo", "Stereo"));
            if (parent->count())
                parent->setItemText(0, text("category.root", "Root"));
        };
        if (auto *manager = UI::I18nManager::installed())
            manager->bind(&window, language);
        else
            language();
        connectionFields();
        refresh();
    }

    void connectionFields()
    {
        const auto settings = Service::instance().connection();
        server->setText(settings.serverBaseUrl.toString());
        publisher->setText(settings.publisherId);
    }

    void commit(Model::State candidate)
    {
        QString error;
        if (!candidate.validate(&error))
            diagnostic->setText(error);
        else if (!node.apply(std::move(candidate)))
            diagnostic->setText(text("item.failed", "Could not apply stream settings."));
        else
            diagnostic->clear();
        refresh();
    }

    template <typename Change> void changeStream(const QString &id, Change change)
    {
        auto state = node.state();
        for (auto &stream : state.streams)
            if (stream.id == id)
                change(stream);
        commit(std::move(state));
    }

    void selectItem()
    {
        remove->setEnabled(!selection.isEmpty() && (selectedCategory || node.state().streams.size() > 1));
        const QSignalBlocker block(parent);
        channel->clear();
        channel->addItem(QStringLiteral("all"), QStringLiteral("all"));
        for (const auto &category : node.state().categories)
            if (selectedCategory && category.id == selection)
            {
                name->setText(category.name);
                parent->setCurrentIndex(parent->findData(category.parent));
            }
        for (const auto &stream : node.state().streams)
            if (!selectedCategory && stream.id == selection)
            {
                name->setText(stream.name);
                parent->setCurrentIndex(parent->findData(stream.category));
                layout->setCurrentIndex(layout->findData(unsigned(stream.layout)));
                for (const auto &role : Model::channelNames(stream.layout))
                    channel->addItem(role, role);
            }
    }

    void rebuild()
    {
        const QSignalBlocker block(tree);
        tree->clear();
        parent->clear();
        parent->addItem(text("category.root", "Root"), QString());
        const auto &state = node.state();
        std::map<QString, QTreeWidgetItem *> categories;
        std::function<void(const QString &, QTreeWidgetItem *)> append =
            [&](const QString &id, QTreeWidgetItem *owner)
        {
            for (const auto &category : state.categories)
                if (category.parent == id)
                {
                    auto *item = new QTreeWidgetItem(QStringList{category.name});
                    (owner ? owner->addChild(item) : tree->addTopLevelItem(item));
                    item->setData(0, Qt::UserRole, category.id);
                    item->setData(0, Qt::UserRole + 1, true);
                    categories[category.id] = item;
                    parent->addItem(state.categoryPath(category.id).join(QStringLiteral(" / ")), category.id);
                    append(category.id, item);
                }
        };
        append({}, nullptr);
        for (const auto &stream : state.streams)
        {
            auto *item = new QTreeWidgetItem(QStringList{stream.name});
            if (stream.category.isEmpty())
                tree->addTopLevelItem(item);
            else
                categories.at(stream.category)->addChild(item);
            item->setData(0, Qt::UserRole, stream.id);
            item->setData(0, Qt::UserRole + 1, false);
        }
        tree->expandAll();
        for (QTreeWidgetItemIterator item(tree); *item; ++item)
            if ((*item)->data(0, Qt::UserRole).toString() == selection)
                tree->setCurrentItem(*item);
        cards.clear();
        while (auto *item = stripLayout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
        for (const auto &stream : state.streams)
        {
            auto *panel = new C::GroupPanel({}, strips);
            panel->setObjectName(QStringLiteral("bcStrip_") + stream.id);
            panel->setFixedWidth(124);
            auto *column = new QVBoxLayout(panel);
            auto *title = new C::TextLabel(stream.name, panel);
            title->setObjectName(QStringLiteral("bcStreamName_") + stream.id);
            title->setAlignment(Qt::AlignCenter);
            title->setToolTip(stream.name);
            column->addWidget(title);
            auto *path =
                new C::TextLabel(state.categoryPath(stream.category).join(QStringLiteral(" / ")), panel);
            path->setObjectName(QStringLiteral("bcCategoryPath_") + stream.id);
            path->setAlignment(Qt::AlignCenter);
            path->setToolTip(path->text());
            column->addWidget(path);
            auto *layoutLabel = new C::TextLabel(Model::layoutName(stream.layout), panel);
            layoutLabel->setAlignment(Qt::AlignCenter);
            if (stream.layout == Model::Layout::mono)
                bind(layoutLabel, "text", "layout.mono", "Mono");
            else if (stream.layout == Model::Layout::stereo)
                bind(layoutLabel, "text", "layout.stereo", "Stereo");
            column->addWidget(layoutLabel);
            auto *meter = new UI::FxLevelMeterWidget(panel);
            meter->setSingleChannel(true, QStringLiteral("Peak"));
            if (auto *manager = UI::I18nManager::installed())
                manager->bind(meter, [meter] { meter->setSingleChannel(true, text("meter.peak", "Peak")); });
            meter->setVertical(true);
            meter->setSurfaceVisible(false);
            meter->setReadoutBelow(true);
            meter->setProperty("midiToolsPassiveSurface", true);
            meter->setFixedWidth(64);
            meter->setMinimumHeight(240);
            column->addWidget(meter, 1, Qt::AlignHCenter);
            auto *switches = new QHBoxLayout;
            auto *mute = new C::ActionButton(QStringLiteral("M"), panel);
            auto *solo = new C::ActionButton(QStringLiteral("S"), panel);
            mute->setCheckable(true);
            solo->setCheckable(true);
            mute->setObjectName(QStringLiteral("bcMute_") + stream.id);
            solo->setObjectName(QStringLiteral("bcSolo_") + stream.id);
            bind(mute, "toolTip", "mute.help", "Mute broadcast and local monitor");
            bind(solo, "toolTip", "solo.help", "Solo this node's local monitor only");
            switches->addWidget(mute);
            switches->addWidget(solo);
            column->addLayout(switches);
            auto *listen = new C::ActionButton({}, panel);
            listen->setCheckable(true);
            listen->setObjectName(QStringLiteral("bcMonitor_") + stream.id);
            bind(listen, "text", "monitor", "Live monitor");
            column->addWidget(listen);
            auto *gain = new C::ConsoleKnob(QString::fromLatin1(scope), {-96, 12, 0, 1}, false, panel);
            gain->setProperty("midiToolsPassiveSurface", true);
            gain->setObjectName(QStringLiteral("bcGain_") + stream.id);
            gain->setMinimumIsSilence(false);
            gain->setUnit(QStringLiteral("dB"));
            column->addWidget(gain, 0, Qt::AlignHCenter);
            cards.push_back({stream.id, meter, gain, mute, solo, listen});
            QObject::connect(mute, &QPushButton::toggled, &window, [this, id = stream.id](bool value)
                             { changeStream(id, [value](auto &item) { item.mute = value; }); });
            QObject::connect(solo, &QPushButton::toggled, &window, [this, id = stream.id](bool value)
                             { changeStream(id, [value](auto &item) { item.solo = value; }); });
            QObject::connect(listen, &QPushButton::toggled, &window, [this, id = stream.id](bool value)
                             { changeStream(id, [value](auto &item) { item.listen = value; }); });
            gain->onChanged([this, id = stream.id](double value)
                            { changeStream(id, [value](auto &item) { item.gainDb = value; }); });
            stripLayout->addWidget(panel);
            UI::bindMidiToolsTheme(panel, QString::fromLatin1(scope));
        }
        selectItem();
    }

    void refresh()
    {
        if (revision != node.stateRevision())
        {
            revision = node.stateRevision();
            const auto current = structure(node.state());
            if (previousStructure != current)
            {
                previousStructure = current;
                rebuild();
            }
        }
        const auto &state = node.state();
        for (std::size_t index = 0; index < cards.size(); ++index)
        {
            auto &card = cards[index];
            const auto &stream = state.streams[index];
            const QSignalBlocker mute(card.mute), solo(card.solo), listen(card.listen);
            card.mute->setChecked(stream.mute);
            card.solo->setChecked(stream.solo);
            card.listen->setChecked(stream.listen);
            card.gain->setValue(stream.gainDb);
            const auto peak = node.takePeak(unsigned(index));
            card.meter->setLevels(peak > 0 ? 20 * std::log10(peak) : -std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity());
        }
        const auto currentStatus = Service::instance().status();
        status->setText(text("status", "Publishing: {state} | streams: {count}",
                             {{"state", text(("state." + currentStatus.state.toLatin1()).constData(),
                                             currentStatus.state.toUtf8().constData())},
                              {"count", currentStatus.activeStreams}}));
        const bool stopped = currentStatus.state == QStringLiteral("stopped") ||
                             currentStatus.state == QStringLiteral("error") ||
                             currentStatus.state == QStringLiteral("authentication-error") ||
                             currentStatus.state == QStringLiteral("identity-conflict");
        publish->setText(stopped ? text("publish.start", "Start publishing")
                                 : text("publish.stop", "Stop publishing"));
        metrics->setText(text("metrics",
                              "UDP sent: {bytes} bytes / {packets} packets | source drops: {drops} | stale: "
                              "{stale} | control RTT: {rtt} ms (not audio latency)",
                              {{"bytes", currentStatus.sentBytes},
                               {"packets", currentStatus.sentDatagrams},
                               {"drops", currentStatus.droppedFrames},
                               {"stale", currentStatus.staleFrames},
                               {"rtt", currentStatus.controlRttMilliseconds}}));
        if (!currentStatus.error.isEmpty())
            diagnostic->setText(currentStatus.error);
    }
};

BroadcastWindow::BroadcastWindow(BroadcastNode &node)
    : QWidget(nullptr, Qt::Window), impl_(std::make_unique<Impl>(*this, node))
{
}
BroadcastWindow::~BroadcastWindow() = default;
void BroadcastWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    impl_->refresh();
    impl_->refreshTimer.start();
}
void BroadcastWindow::hideEvent(QHideEvent *event)
{
    impl_->refreshTimer.stop();
    QWidget::hideEvent(event);
}
} // namespace WinJACKNexus::Broadcast
