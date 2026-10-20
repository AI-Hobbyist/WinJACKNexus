/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Editor.hpp"
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

namespace WinJACKNexus::SignalSwitch
{
namespace
{
QString text(const char *key, const char *english)
{
    return UI::localizedText("signalSwitch", key, english);
}

struct Resources : QObject
{
    UI::I18nDomainLease language;
    UI::ThemeNamespaceRegistry::Lease theme;
};
QPointer<Resources> resources;
} // namespace

Editor::Editor(Service &service) : service_(service)
{
    setObjectName("signalSwitchEditor");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(320, 200);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll_ = new UI::Controls::ScrollField(this);
    scroll_->setWidgetResizable(false);
    canvas_ = new QWidget;
    canvas_->setObjectName("signalSwitchCanvas");
    scroll_->setWidget(canvas_);
    layout->addWidget(scroll_);
    empty_ = new UI::Controls::TextLabel(canvas_);
    empty_->setWordWrap(true);
    menu_ = new QMenu(this);
    menu_->setObjectName("signalSwitchQuickMenu");
    auto *open = menu_->addAction(QString());
    open->setObjectName("signalSwitchOpenEditor");
    connect(open, &QAction::triggered, this, [this] { service_.showEditor(); });
    // Full lock/overlay and pointer behavior is added at the S4 checkpoint.
    auto *lock = menu_->addAction(QString());
    lock->setObjectName("signalSwitchLock");
    lock->setEnabled(false);
    auto *unlock = menu_->addAction(QString());
    unlock->setObjectName("signalSwitchUnlock");
    unlock->setEnabled(false);
    auto *through = menu_->addAction(QString());
    through->setObjectName("signalSwitchClickThrough");
    through->setCheckable(true);
    through->setEnabled(false);
    const auto retranslate = [this, open, lock, unlock, through] {
        setWindowTitle(text("title", "Signal Switch"));
        menu_->setTitle(text("title", "Signal Switch"));
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
    UI::bindRefreshTimer(maintenance_, this);
    connect(&maintenance_, &QTimer::timeout, this, [this] {
        if (service_.synchronize)
            service_.synchronize();
        service_.reconcile();
        refresh();
    });
    maintenance_.start();
    connect(scroll_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { rememberGeometry(); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { rememberGeometry(); });
    restoreGeometryFromState();
}

Editor::~Editor() = default;

void Editor::refresh()
{
    QSet<QString> live;
    QSize extent(320, 200);
    for (const auto &endpoint : service_.endpoints)
    {
        live.insert(endpoint.id);
        auto *button = buttons_.value(endpoint.id);
        if (!button)
        {
            button = new UI::Controls::ActionButton({}, canvas_);
            button->setObjectName("signalSwitchButton." + endpoint.id);
            button->setProperty("signalSwitchId", endpoint.id);
            button->setCheckable(true);
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            buttons_[endpoint.id] = button;
            connect(button, &QPushButton::clicked, this, [this, id = endpoint.id] {
                if (auto *endpoint = service_.find(id))
                    service_.setEnabled({id}, !endpoint->desired);
            });
            connect(button, &QWidget::customContextMenuRequested, this, [this, id = endpoint.id, button](QPoint pos) {
                QMenu menu(this);
                auto *action = menu.addAction(text("rename", "Rename"));
                connect(action, &QAction::triggered, this, [this, id] { rename(id); });
                menu.exec(button->mapToGlobal(pos));
            });
            button->show();
        }
        const bool pending = endpoint.desired && !(endpoint.gate->command.load(std::memory_order_acquire) & 1);
        const auto state = pending            ? text("waiting", "Waiting")
                           : endpoint.desired ? text("on", "On")
                                              : text("off", "Off");
        button->setText(endpoint.name + " · " + state);
        button->setChecked(endpoint.desired);
        button->setAccessibleName(endpoint.name + " · " + state);
        button->setToolTip(endpoint.name + "\n" + endpoint.context + "\n" + endpoint.version);
        const auto rect = service_.state.cards.value(endpoint.id).geometry;
        button->setGeometry(rect);
        extent = extent.expandedTo(QSize(rect.right() + 20, rect.bottom() + 20));
    }
    for (auto it = buttons_.begin(); it != buttons_.end();)
    {
        if (!live.contains(it.key()))
        {
            delete it.value();
            it = buttons_.erase(it);
        }
        else
            ++it;
    }
    canvas_->resize(extent);
    empty_->setVisible(live.isEmpty());
    empty_->setGeometry(20, 20, 280, 100);
}

void Editor::rename(const QString &id)
{
    const auto *endpoint = service_.find(id);
    if (!endpoint)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(text("rename", "Rename"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *field = new UI::Controls::TextField(&dialog);
    field->setText(endpoint->name);
    layout->addWidget(field);
    auto *buttons = new UI::Controls::DialogButtonBar(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || field->text().trimmed().isEmpty())
        return;
    if (auto *current = service_.find(id); current && current->rename && current->rename(field->text()))
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
    }
    setGeometry(rect);
    refresh();
    scroll_->horizontalScrollBar()->setValue(service_.state.window.scroll.x());
    scroll_->verticalScrollBar()->setValue(service_.state.window.scroll.y());
    applying_ = false;
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
                                                          {"displayName", "Signal Switch"},
                                                          {"tokens", tokens}})
                                    .toJson(QJsonDocument::Compact);
        if (!UI::ThemeNamespaceRegistry::instance().registerInternal(descriptor, "signal-switch", &resources->theme,
                                                                     error))
        {
            delete resources;
            return false;
        }
        if (auto *manager = UI::I18nManager::installed())
        {
            const QJsonObject descriptor{{"descriptorVersion", 1},
                                         {"i18nApiVersion", 1},
                                         {"moduleId", "winjacknexus-signal-switch"},
                                         {"domains", QJsonArray{"signalSwitch"}},
                                         {"locatePath", "locate"},
                                         {"catalogVersion", 1}};
            resources->language = manager->registerModule(
                descriptor, QCoreApplication::applicationDirPath() + "/signal_switch", {}, error);
            if (!resources->language)
            {
                delete resources;
                return false;
            }
        }
    }
    auto *editor = new Editor(*this);
    editor_ = editor;
    changed = [editor] { editor->refresh(); };
    UI::TrayController::setQuickAccessSwitchMenu(editor->menu());
    return true;
}

QWidget *Service::showEditor()
{
    if (!installUI())
        return nullptr;
    auto *editor = static_cast<Editor *>(editor_.data());
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
} // namespace WinJACKNexus::SignalSwitch
