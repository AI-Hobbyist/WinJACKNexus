/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Editor.hpp"
#include "Sound.hpp"
#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QVBoxLayout>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MidiToolsTheme.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include <algorithm>

namespace WinJACKNexus::ControlDeck
{
namespace
{
QString text(const char *key, const char *english)
{
    return UI::localizedText("signalSwitch", key, english);
}

struct Resources : QObject
{
    UI::ThemeNamespaceRegistry::Lease theme;
};
QPointer<Resources> resources;
} // namespace

Editor::Editor(Service &service) : service_(service)
{
    setObjectName("controlDeckEditor");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(320, 200);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll_ = new UI::Controls::ScrollField(this);
    scroll_->setWidgetResizable(false);
    canvas_ = new QWidget;
    canvas_->setObjectName("controlDeckCanvas");
    canvas_->installEventFilter(this);
    canvas_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(canvas_, &QWidget::customContextMenuRequested, this,
            [this](QPoint point) { showCanvasMenu(canvas_->mapToGlobal(point)); });
    scroll_->setWidget(canvas_);
    layout->addWidget(scroll_);
    empty_ = new UI::Controls::TextLabel(canvas_);
    empty_->setWordWrap(true);
    status_ = new UI::Controls::TextLabel(this);
    status_->setWordWrap(true);
    status_->hide();
    layout->addWidget(status_);
    rubber_ = new QRubberBand(QRubberBand::Rectangle, canvas_);
    menu_ = new QMenu(this);
    menu_->setObjectName("controlDeckQuickMenu");
    auto *open = menu_->addAction(QString());
    open->setObjectName("controlDeckOpenEditor");
    connect(open, &QAction::triggered, this, [this] { service_.showEditor(); });
    auto *lock = menu_->addAction(QString());
    lockAction_ = lock;
    lock->setObjectName("controlDeckLock");
    connect(lock, &QAction::triggered, this, [this] { setLocked(true); });
    auto *unlock = menu_->addAction(QString());
    unlockAction_ = unlock;
    unlock->setObjectName("controlDeckUnlock");
    connect(unlock, &QAction::triggered, this, [this] { service_.showEditor(); });
    auto *through = menu_->addAction(QString());
    throughAction_ = through;
    through->setObjectName("controlDeckClickThrough");
    through->setCheckable(true);
    connect(through, &QAction::triggered, this, [this](bool enabled) {
        service_.state.window.clickThrough = enabled;
        refreshPresentation();
        service_.notify(true);
    });
    const auto retranslate = [this, open, lock, unlock, through] {
        setWindowTitle(text("title", "Control Deck"));
        menu_->setTitle(text("title", "Control Deck"));
        open->setText(text("openEditor", "Open editor"));
        lock->setText(text("lock", "Lock layout"));
        unlock->setText(text("unlock", "Unlock layout"));
        through->setText(text("clickThrough", "Mouse click-through"));
        empty_->setText(text("empty", "Add Signal Switch nodes in the graph to create buttons here."));
        refresh();
    };
    if (auto *manager = UI::I18nManager::installed())
        manager->bind(this, retranslate);
    else
        retranslate();
    UI::bindMidiToolsTheme(this, "signal-switch");
    if (auto *core = UI::ThemeManager::installedThemeCore())
        connect(core, &UI::ThemeCore::themeChanged, this,
                [this] { QTimer::singleShot(0, this, [this] { refresh(); }); });
    UI::bindRefreshTimer(maintenance_, this);
    connect(&maintenance_, &QTimer::timeout, this, [this] {
        if (service_.synchronize)
            service_.synchronize();
        service_.reconcile();
        service_.pollSounds();
        refresh();
    });
    maintenance_.start();
    connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { rememberGeometry(); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { rememberGeometry(); });
    restoreGeometryFromState();
}

Editor::~Editor()
{
    releaseMouseHook();
    for (auto *overlay : overlays_)
        delete overlay;
}

void Editor::refresh()
{
    service_.updateShortcuts();
    QSet<QString> live;
    QSize extent(320, 200);
    for (const auto &endpoint : service_.endpoints)
    {
        live.insert(endpoint.id);
        auto *button = buttons_.value(endpoint.id);
        if (!button)
        {
            button = new UI::Controls::ActionButton({}, canvas_);
            button->setObjectName("controlDeckButton." + endpoint.id);
            button->setProperty("controlDeckId", endpoint.id);
            button->setCheckable(!endpoint.sound && !endpoint.peak && !endpoint.gainLinear);
            button->installEventFilter(this);
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            buttons_[endpoint.id] = button;
            connect(button, &QPushButton::clicked, this, [this, id = endpoint.id] { service_.activate(id); });
            connect(button, &QWidget::customContextMenuRequested, this, [this, id = endpoint.id, button](QPoint pos) {
                if (!service_.state.locked)
                    showCardMenu(id, button->mapToGlobal(pos));
            });
            button->show();
        }
        const bool pending =
            endpoint.desired ? !(endpoint.gate->command.load(std::memory_order_acquire) & 1) : !endpoint.gate->quiet();
        const auto state = endpoint.sound     ? (endpoint.sound->loading()   ? text("loading", "Loading")
                                                 : endpoint.sound->playing() ? text("playing", "Playing")
                                                 : endpoint.sound->ready()   ? text("trigger", "Play")
                                                                             : text("unloaded", "No sound loaded"))
                           : pending          ? text("waiting", "Waiting")
                           : endpoint.desired ? text("on", "On")
                                              : text("off", "Off");
        const auto name = service_.label(endpoint);
        const auto rect = service_.state.cards.value(endpoint.id).geometry;
        const auto suffix = QStringLiteral(" · ") + state;
        const QFontMetrics metrics(button->font());
        const int margin = style()->pixelMetric(QStyle::PM_ButtonMargin);
        const int available = std::max(0, rect.width() - margin - metrics.horizontalAdvance(suffix));
        button->setText(metrics.elidedText(name, Qt::ElideRight, available) + suffix);
        button->setChecked(endpoint.desired);
        button->setAccessibleName(name + " · " + state);
        button->setToolTip(name + "\n" + endpoint.context + "\n" + endpoint.version);
        if (endpoint.sound)
        {
            button->setToolTip(button->toolTip() + "\n" + endpoint.sound->path() + "\n" + endpoint.shortcut);
            const auto error = endpoint.sound->error().isEmpty() ? endpoint.shortcutError : endpoint.sound->error();
            if (!error.isEmpty())
                button->setToolTip(button->toolTip() + "\n" +
                                   UI::localizedText("signalSwitch", "soundError",
                                                     "Sound or shortcut unavailable: {detail}", {{"detail", error}}));
        }
        refreshControl(button, endpoint);
        if (!service_.state.locked)
            button->setGeometry(rect);
        styleButton(button, endpoint.id);
        extent = extent.expandedTo(QSize(rect.right() + 20, rect.bottom() + 20));
    }
    for (auto it = buttons_.begin(); it != buttons_.end();)
    {
        if (!live.contains(it.key()))
        {
            delete it.value();
            delete overlays_.take(it.key());
            selected_.remove(it.key());
            it = buttons_.erase(it);
        }
        else
            ++it;
    }
    canvas_->resize(extent);
    empty_->setVisible(live.isEmpty());
    empty_->setGeometry(20, 20, 280, 100);
    refreshGroups();
    refreshPresentation();
}

void Editor::rename(const QString &id)
{
    const auto *endpoint = service_.find(id);
    if (!endpoint)
        return;
    auto name = service_.label(*endpoint);
    if (!editText("rename", "Rename", &name))
        return;
    if (auto *current = service_.find(id); current && current->rename && current->rename(name))
        service_.notify(true);
}

void Editor::rememberGeometry()
{
    if (applying_ || !isVisible())
        return;
    service_.state.window.geometry = geometry();
    service_.state.window.scroll = {scroll_->horizontalScrollBar()->value(), scroll_->verticalScrollBar()->value()};
    service_.state.window.screen = screen() ? screen()->name() : QString();
    service_.notify(true);
}

void Editor::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    rememberGeometry();
}

void Editor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rememberGeometry();
}

void Editor::restoreGeometryFromState()
{
    applying_ = true;
    QRect rect = service_.state.window.geometry;
    auto *target = QGuiApplication::primaryScreen();
    for (auto *screen : QGuiApplication::screens())
    {
        if (screen->name() == service_.state.window.screen)
            target = screen;
    }
    if (target)
    {
        const auto available = target->availableGeometry();
        rect.setSize(rect.size().boundedTo(available.size()));
        rect.moveLeft(std::clamp(rect.left(), available.left(), available.right() - rect.width() + 1));
        rect.moveTop(std::clamp(rect.top(), available.top(), available.bottom() - rect.height() + 1));
        if (service_.state.locked && !service_.state.cards.isEmpty())
        {
            QRect bounds;
            for (const auto &card : service_.state.cards)
                bounds = bounds.united(card.geometry.translated(service_.state.window.overlayOrigin));
            bool onScreen = false;
            for (auto *screen : QGuiApplication::screens())
                onScreen |= screen->availableGeometry().intersects(bounds);
            if (!onScreen)
                service_.state.window.overlayOrigin += available.topLeft() - bounds.topLeft();
        }
    }
    setGeometry(rect);
    refresh();
    scroll_->horizontalScrollBar()->setValue(service_.state.window.scroll.x());
    scroll_->verticalScrollBar()->setValue(service_.state.window.scroll.y());
    applying_ = false;
    refreshPresentation();
}

bool Service::installUI(QString *error)
{
    if (editor_)
        return true;
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()))
        return false;
    if (!resources)
    {
        resources = new Resources;
        resources->setParent(QCoreApplication::instance());
        QJsonArray tokens;
        for (const auto *role : {"surface-window", "surface-panel", "text-primary", "accent-primary", "border-normal",
                                 "control-button-surface-normal", "control-button-border-normal",
                                 "control-button-text-normal", "control-button-border-selected"})
        {
            tokens.append(QJsonObject{{"name", QString("--wjn-theme-signal-switch-") + role},
                                      {"type", "color"},
                                      {"default", QString("var(--wjn-theme-global-") + role + ')'},
                                      {"description", QString("Signal Switch ") + role}});
        }
        const auto descriptor = QJsonDocument(QJsonObject{{"descriptorVersion", 1},
                                                          {"themeApiVersion", 1},
                                                          {"moduleId", "signal-switch"},
                                                          {"namespace", "--wjn-theme-signal-switch-"},
                                                          {"displayName", "Control Deck"},
                                                          {"tokens", tokens}})
                                    .toJson(QJsonDocument::Compact);
        if (!UI::ThemeNamespaceRegistry::instance().registerInternal(descriptor, "signal-switch", &resources->theme,
                                                                     error))
        {
            delete resources;
            return false;
        }
        // The main panel's "signalSwitch" domain ships in the host catalog (locate/).
    }
    auto *editor = new Editor(*this);
    editor_ = editor;
    changed = [editor] { editor->refresh(); };
    UI::TrayController::setQuickAccessSwitchMenu(editor->menu());
    return true;
}

QString Service::label(const Endpoint &endpoint) const
{
    return endpoint.userNamed || endpoint.domain.isEmpty()
               ? endpoint.name
               : UI::localizedText(endpoint.domain.toUtf8().constData(), "name", endpoint.version.toUtf8().constData());
}

QWidget *Service::showEditor()
{
    if (!installUI())
        return nullptr;
    auto *editor = static_cast<Editor *>(editor_.data());
    if (state.locked)
        editor->setLocked(false, false);
    editor->restoreGeometryFromState();
    editor->show();
    editor->raise();
    editor->activateWindow();
    return editor;
}

QMenu *Service::quickMenu()
{
    return installUI() ? static_cast<Editor *>(editor_.data())->menu() : nullptr;
}

void Service::refreshUIState()
{
    if (editor_)
        static_cast<Editor *>(editor_.data())->restoreGeometryFromState();
}

void Service::shutdownUI()
{
    UI::TrayController::setQuickAccessSwitchMenu(nullptr);
    changed = {};
    edited = {};
    synchronize = {};
    delete editor_.data();
    delete resources;
}
} // namespace WinJACKNexus::ControlDeck
