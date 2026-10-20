/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/UI/TrayController.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/IconManager.hpp>

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace WinJACKNexus::UI
{

namespace
{

std::vector<TrayController *> trayControllers;
std::vector<TrayNodeGuiEntry> nodeGuiEntries;
QHash<QString, QString> nodeGuiDisplayNames;
QString entryDisplayName(const TrayNodeGuiEntry &entry)
{
    return nodeGuiDisplayNames.value(entry.id, entry.name);
}
QString quickAccessStatus;
QPointer<QMenu> quickAccessFileMenu;
QPointer<QMenu> quickAccessSwitchMenu;
QMetaObject::Connection quickAccessFileMenuDestroyed;
constexpr int quickAccessSideRegionWidth = 128;
constexpr int quickAccessTopEdgeHeight = 2;
int quickAccessOpacityPercent = 50;

Controls::IconActionButton *iconButton(const QString &name, QWidget *parent)
{
    auto *button = new Controls::IconActionButton(parent);
    button->setFixedSize(Controls::sidebarControlHeight, Controls::sidebarControlHeight);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    IconManager::bindThemedIcon(button, name);
    return button;
}

class QuickAccessBar final : public QFrame
{
    using EntryList = std::vector<const TrayNodeGuiEntry *>;

    enum class SystemDeviceType
    {
        none,
        wasapiInput,
        wasapiOutput,
        midiInput,
        midiOutput
    };

  public:
    explicit QuickAccessBar(QWidget *parent = nullptr) : QFrame(parent)
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                       Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setObjectName(QStringLiteral("quickAccessBar"));
        setAutoFillBackground(true);

        auto *outerLayout = new QVBoxLayout(this);
        outerLayout->setContentsMargins(10, 7, 10, 3);
        outerLayout->setSpacing(2);
        auto *layout = new QHBoxLayout;
        outerLayout->addLayout(layout);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *title = new Controls::TextLabel(QStringLiteral("WinJACKNexus"), this);
        title->setFixedWidth(quickAccessSideRegionWidth);
        title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(title);
        layout->addStretch();

        fileButton_ = new Controls::IconActionButton(this);
        fileButton_->setObjectName(QStringLiteral("quickAccessFileButton"));
        fileButton_->setText(hostText("menu.file", "File"));
        fileButton_->setToolTip(hostText("quick.file.help", "Create, open and save projects"));
        fileButton_->setPopupMode(QToolButton::InstantPopup);
        fileButton_->setEnabled(false);
        layout->addWidget(fileButton_);
        settingsButton_ = new Controls::IconActionButton(this);
        settingsButton_->setObjectName(QStringLiteral("quickAccessSettingsButton"));
        settingsButton_->setEnabled(false);
        layout->addWidget(settingsButton_);

        patchbayButton_ = new Controls::IconActionButton(this);
        patchbayButton_->setText(hostText("view.patchbay", "Patchbay"));
        patchbayButton_->setToolTip(hostText("quick.patchbay.help", "Open patchbay"));
        layout->addWidget(patchbayButton_);

        meterbridgeButton_ = new Controls::IconActionButton(this);
        meterbridgeButton_->setText(hostText("view.meterbridge", "Meter bridge"));
        meterbridgeButton_->setToolTip(hostText("quick.meterbridge.help", "Open meter bridge"));
        layout->addWidget(meterbridgeButton_);

        switchButton_ = new Controls::IconActionButton(this);
        switchButton_->setObjectName(QStringLiteral("quickAccessSignalSwitchButton"));
        switchButton_->setPopupMode(QToolButton::InstantPopup);
        layout->addWidget(switchButton_);
        setSwitchMenu(quickAccessSwitchMenu);

        mixerButton_ = new Controls::IconActionButton(this);
        mixerButton_->setObjectName(QStringLiteral("quickAccessMixerButton"));
        mixerButton_->setPopupMode(QToolButton::InstantPopup);
        mixerButton_->setText(hostText("view.mixer", "Mixer"));
        mixerButton_->setToolTip(hostText("quick.mixer.help", "Open mixer"));
        layout->addWidget(mixerButton_);

        systemDevicesButton_ = new Controls::IconActionButton(this);
        systemDevicesButton_->setText(hostText("quick.devices", "System devices"));
        systemDevicesButton_->setToolTip(
            hostText("quick.devices.help", "Open added WASAPI and MIDI device nodes"));
        systemDevicesButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        systemDevicesButton_->setPopupMode(QToolButton::InstantPopup);
        systemDevicesMenu_ = new QMenu(systemDevicesButton_);
        systemDevicesButton_->setMenu(systemDevicesMenu_);
        layout->addWidget(systemDevicesButton_);

        contentLayout_ = new QHBoxLayout;
        contentLayout_->setSpacing(6);
        layout->addLayout(contentLayout_);

        pinButton_ = iconButton(QStringLiteral("pin"), this);
        pinButton_->setObjectName(QStringLiteral("quickAccessPinButton"));
        pinButton_->setCheckable(true);
        pinButton_->setChecked(true);
        pinButton_->setToolTip(
            hostText("quick.pin.help", "Keep the quick access bar above other windows"));
        pinButton_->setAccessibleName(hostText("quick.pin", "Pin quick access bar"));
        layout->addWidget(pinButton_);
        setOpacity(quickAccessOpacityPercent);
        connect(pinButton_, &QToolButton::toggled, this, [this](bool pinned) {
            setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
            show();
            centerAtTop();
        });

        hideButton_ = iconButton(QStringLiteral("hide"), this);
        hideButton_->setObjectName(QStringLiteral("quickAccessHideButton"));
        hideButton_->setToolTip(hostText("quick.hide", "Collapse quick access bar"));
        hideButton_->setAccessibleName(hostText("quick.hide", "Collapse quick access bar"));
        layout->addWidget(hideButton_);
        connect(hideButton_, &QToolButton::clicked, this,
                [this] { setExpanded(false, activeScreen()); });

        themeButton_ = iconButton(QStringLiteral("theme"), this);
        themeButton_->setObjectName(QStringLiteral("quickAccessThemeButton"));
        themeButton_->setToolTip(hostText("quick.theme", "Switch theme"));
        themeButton_->setAccessibleName(hostText("quick.theme", "Switch theme"));
        themeButton_->setPopupMode(QToolButton::InstantPopup);
        layout->addWidget(themeButton_);
        layout->addStretch();

        statusLabel_ = new Controls::TextLabel(this);
        statusLabel_->setFixedWidth(quickAccessSideRegionWidth);
        statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        statusLabel_->setToolTip(hostText("quick.status.help", "JACK DSP usage and XRun count"));
        layout->addWidget(statusLabel_);

        themeProgress_ = new Controls::ProgressField(this);
        themeProgress_->setObjectName(QStringLiteral("quickAccessThemeProgress"));
        themeProgress_->setRange(0, 100);
        themeProgress_->setTextVisible(false);
        themeProgress_->setFixedHeight(5);
        themeProgress_->hide();
        outerLayout->addWidget(themeProgress_);

        bindRefreshTimer(autoHideTimer_, this);
        connect(&autoHideTimer_, &QTimer::timeout, this, [this] { updateAutoVisibility(); });
        autoHideTimer_.start();
        if (auto *manager = I18nManager::installed())
            manager->bind(this, [this] { retranslate(); });
    }

    void setPatchbayAction(QAction *action)
    {
        patchbayButton_->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr && patchbayAction_ == nullptr)
        {
            patchbayAction_ = action;
            connect(patchbayButton_, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, this,
                    [this, action] { patchbayButton_->setEnabled(action->isEnabled()); });
        }
    }

    void setMeterbridgeAction(QAction *action)
    {
        meterbridgeButton_->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr && meterbridgeAction_ == nullptr)
        {
            meterbridgeAction_ = action;
            connect(meterbridgeButton_, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, this,
                    [this, action] { meterbridgeButton_->setEnabled(action->isEnabled()); });
        }
    }

    void setMixerAction(QAction *action)
    {
        mixerButton_->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr && mixerAction_ == nullptr)
        {
            mixerAction_ = action;
            mixerButton_->setMenu(action->menu());
            connect(action, &QAction::changed, this,
                    [this, action] { mixerButton_->setEnabled(action->isEnabled()); });
        }
    }

    void setThemeMenu(QMenu *menu)
    {
        themeButton_->setMenu(menu);
        themeButton_->setEnabled(menu != nullptr);
    }

    void setSwitchMenu(QMenu *menu)
    {
        disconnect(switchMenuChanged_);
        switchButton_->setMenu(menu);
        switchButton_->setEnabled(menu != nullptr);
        switchButton_->setText(menu ? menu->title() : QStringLiteral("Control Deck"));
        switchButton_->setAccessibleName(switchButton_->text());
        if (menu)
            switchMenuChanged_ = connect(menu->menuAction(), &QAction::changed, switchButton_, [this, menu] {
                switchButton_->setText(menu->title());
                switchButton_->setAccessibleName(menu->title());
            });
    }

    void setSettingsAction(QAction *action)
    {
        settingsButton_->setDefaultAction(action);
        settingsButton_->setEnabled(action != nullptr);
    }

    void retranslate()
    {
        fileButton_->setText(hostText("menu.file", "File"));
        fileButton_->setToolTip(hostText("quick.file.help", "Create, open and save projects"));
        patchbayButton_->setText(hostText("view.patchbay", "Patchbay"));
        patchbayButton_->setToolTip(hostText("quick.patchbay.help", "Open patchbay"));
        meterbridgeButton_->setText(hostText("view.meterbridge", "Meter bridge"));
        meterbridgeButton_->setToolTip(hostText("quick.meterbridge.help", "Open meter bridge"));
        mixerButton_->setText(hostText("view.mixer", "Mixer"));
        mixerButton_->setToolTip(hostText("quick.mixer.help", "Open mixer"));
        systemDevicesButton_->setText(hostText("quick.devices", "System devices"));
        systemDevicesButton_->setToolTip(
            hostText("quick.devices.help", "Open added WASAPI and MIDI device nodes"));
        pinButton_->setToolTip(
            hostText("quick.pin.help", "Keep the quick access bar above other windows"));
        pinButton_->setAccessibleName(hostText("quick.pin", "Pin quick access bar"));
        hideButton_->setToolTip(hostText("quick.hide", "Collapse quick access bar"));
        hideButton_->setAccessibleName(hideButton_->toolTip());
        themeButton_->setToolTip(hostText("quick.theme", "Switch theme"));
        themeButton_->setAccessibleName(themeButton_->toolTip());
        statusLabel_->setToolTip(hostText("quick.status.help", "JACK DSP usage and XRun count"));
        for (auto *button :
             {patchbayButton_, meterbridgeButton_, mixerButton_, systemDevicesButton_})
            button->setAccessibleName(button->text());
        fileButton_->setAccessibleName(fileButton_->text());
        entriesInitialized_ = false;
        setEntries(nodeGuiEntries);
        recenterAfterLayout();
    }

    void setFileMenu(QMenu *menu)
    {
        fileButton_->setMenu(menu);
        fileButton_->setEnabled(menu != nullptr);
    }

    void setEntries(const std::vector<TrayNodeGuiEntry> &entries)
    {
        const auto signature = entriesSignature(entries);
        if (entriesInitialized_ && signature == entriesSignature_)
            return;

        while (auto *item = contentLayout_->takeAt(0))
        {
            delete item->widget();
            delete item;
        }

        EntryList wasapiInputEntries;
        EntryList wasapiOutputEntries;
        EntryList midiInputEntries;
        EntryList midiOutputEntries;
        EntryList vstEntries;
        std::map<QString, std::map<QString, EntryList>> groupedEntries;
        for (const auto &entry : entries)
        {
            if (entry.id.isEmpty() || entry.name.isEmpty() || !entry.show)
                continue;
            if (entry.category.compare(QStringLiteral("Meter"), Qt::CaseInsensitive) == 0)
                continue;
            if (entry.category.compare(QStringLiteral("Digital Mixer"), Qt::CaseInsensitive) == 0)
                continue;

            switch (systemDeviceType(entry))
            {
            case SystemDeviceType::wasapiInput:
                wasapiInputEntries.push_back(&entry);
                continue;
            case SystemDeviceType::wasapiOutput:
                wasapiOutputEntries.push_back(&entry);
                continue;
            case SystemDeviceType::midiInput:
                midiInputEntries.push_back(&entry);
                continue;
            case SystemDeviceType::midiOutput:
                midiOutputEntries.push_back(&entry);
                continue;
            case SystemDeviceType::none:
                break;
            }

            if (isVstEntry(entry))
            {
                vstEntries.push_back(&entry);
                continue;
            }

            const auto category = entry.category;
            const auto project = entry.clientName.isEmpty() ? entry.name : entry.clientName;
            groupedEntries[category][project].push_back(&entry);
        }

        rebuildSystemDevicesMenu(wasapiInputEntries, wasapiOutputEntries, midiInputEntries,
                                 midiOutputEntries);

        auto *vstButton = new Controls::IconActionButton(this);
        vstButton->setText(QStringLiteral("VST"));
        vstButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        vstButton->setPopupMode(QToolButton::InstantPopup);
        auto *vstMenu = new QMenu(vstButton);
        addProjectEntries(vstMenu, vstEntries);
        vstButton->setMenu(vstMenu);
        contentLayout_->addWidget(vstButton);

        for (const auto &[category, projects] : groupedEntries)
        {
            auto *categoryButton = new Controls::IconActionButton(this);
            categoryButton->setText(category.isEmpty() ? hostText("tray.other", "Other")
                                                       : category);
            categoryButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
            categoryButton->setPopupMode(QToolButton::InstantPopup);
            auto *categoryMenu = new QMenu(categoryButton);
            EntryList categoryEntries;
            for (const auto &[project, nodes] : projects)
                categoryEntries.insert(categoryEntries.end(), nodes.cbegin(), nodes.cend());
            addProjectEntries(categoryMenu, categoryEntries);
            categoryButton->setMenu(categoryMenu);
            contentLayout_->addWidget(categoryButton);
        }
        entriesSignature_ = signature;
        entriesInitialized_ = true;
        recenterAfterLayout();
    }

    void setStatus(const QString &status)
    {
        statusLabel_->setText(status);
    }

    void setThemeLoadingProgress(int percent)
    {
        themeProgress_->setValue(std::clamp(percent, 0, 100));
        themeProgress_->setVisible(percent < 100);
        recenterAfterLayout();
    }

    void setOpacity(int percent)
    {
        opacityPercent_ = std::clamp(percent, 10, 100);
        setWindowOpacity(static_cast<qreal>(opacityPercent_) / 100.0);
    }

    void showCentered()
    {
        setExpanded(true, screenForCurrentPosition());
        raise();
    }

  private:
    void updateAutoVisibility()
    {
        if (!isVisible())
            return;

        const auto cursorPosition = QCursor::pos();
        if (auto *topEdgeScreen = screenAtTopEdge(cursorPosition); topEdgeScreen != nullptr)
        {
            setExpanded(true, topEdgeScreen);
            return;
        }

        if (expanded_ && !isCursorOverInteractiveSurface(cursorPosition))
            setExpanded(false, activeScreen());
    }

    static QScreen *screenAtTopEdge(const QPoint &position)
    {
        for (auto *screen : QGuiApplication::screens())
        {
            const auto geometry = screen->geometry();
            if (position.x() >= geometry.left() && position.x() <= geometry.right() &&
                position.y() >= geometry.top() &&
                position.y() < geometry.top() + quickAccessTopEdgeHeight)
                return screen;
        }
        return nullptr;
    }

    QScreen *activeScreen() const
    {
        return activeScreen_.isNull() ? QGuiApplication::primaryScreen() : activeScreen_.data();
    }

    QScreen *screenForCurrentPosition() const
    {
        if (auto *screen = QGuiApplication::screenAt(QCursor::pos()); screen != nullptr)
            return screen;
        return activeScreen();
    }

    bool isCursorOverInteractiveSurface(const QPoint &position) const
    {
        if (frameGeometry().contains(position))
            return true;
        const auto *popup = QApplication::activePopupWidget();
        return popup != nullptr && popup->isVisible() && popup->frameGeometry().contains(position);
    }

    QRect geometryForState(bool expanded, QScreen *screen) const
    {
        if (screen == nullptr)
            return {};
        const auto screenGeometry = screen->geometry();
        const auto height = sizeHint().height();
        const auto top = expanded ? screenGeometry.top()
                                  : screenGeometry.top() - height + quickAccessTopEdgeHeight;
        return {screenGeometry.left(), top, screenGeometry.width(), height};
    }

    void setExpanded(bool expanded, QScreen *screen)
    {
        if (screen == nullptr)
            return;

        activeScreen_ = screen;
        const auto geometry = geometryForState(expanded, screen);
        expanded_ = expanded;
        if (!isVisible())
            show();
        if (!geometry.isEmpty() && this->geometry() != geometry)
            setGeometry(geometry);
        if (expanded)
            raise();
    }

    static QString entriesSignature(const std::vector<TrayNodeGuiEntry> &entries)
    {
        QString signature;
        for (const auto &entry : entries)
        {
            signature += entry.id;
            signature += QChar::Null;
            signature += entry.category;
            signature += QChar::Null;
            signature += entry.clientName;
            signature += QChar::Null;
            signature += entry.name;
            signature += entryDisplayName(entry);
            signature += QChar::Null;
        }
        return signature;
    }

    static SystemDeviceType systemDeviceType(const TrayNodeGuiEntry &entry)
    {
        if (entry.category.compare(QStringLiteral("Windows Native Driver"), Qt::CaseInsensitive) !=
            0)
            return SystemDeviceType::none;
        if (entry.name == QStringLiteral("WASAPI In"))
            return SystemDeviceType::wasapiInput;
        if (entry.name == QStringLiteral("WASAPI Out"))
            return SystemDeviceType::wasapiOutput;
        if (entry.name == QStringLiteral("System MIDI In"))
            return SystemDeviceType::midiInput;
        if (entry.name == QStringLiteral("System MIDI Out"))
            return SystemDeviceType::midiOutput;
        return SystemDeviceType::none;
    }

    static bool isVstEntry(const TrayNodeGuiEntry &entry)
    {
        return entry.category.startsWith(QStringLiteral("VST"), Qt::CaseInsensitive);
    }

    static void addNoRelatedEntriesAction(QMenu *menu)
    {
        auto *action = menu->addAction(hostText("tray.noEntries", "No matching entries"));
        action->setEnabled(false);
    }

    void addEntryAction(QMenu *menu, const TrayNodeGuiEntry &entry, const QString &text)
    {
        auto *action = menu->addAction(text);
        action->setData(entry.id);
        connect(action, &QAction::triggered, this, [show = entry.show] { show(); });
    }

    void addProjectEntries(QMenu *menu, const EntryList &entries)
    {
        if (entries.empty())
        {
            addNoRelatedEntriesAction(menu);
            return;
        }

        std::map<QString, EntryList> projects;
        for (const auto *entry : entries)
        {
            const auto project = entry->clientName.isEmpty() ? entry->name : entry->clientName;
            projects[project].push_back(entry);
        }
        for (const auto &[project, nodes] : projects)
        {
            if (nodes.size() == 1)
            {
                addEntryAction(menu, *nodes.front(),
                               nodes.front()->clientName.isEmpty()
                                   ? entryDisplayName(*nodes.front())
                                   : project);
                continue;
            }
            auto *nodeMenu = menu->addMenu(
                nodes.front()->clientName.isEmpty() ? entryDisplayName(*nodes.front()) : project);
            for (const auto *node : nodes)
                addEntryAction(nodeMenu, *node, entryDisplayName(*node));
        }
    }

    void addSystemDeviceDirectionMenu(QMenu *parent, const QString &direction,
                                      const EntryList &entries)
    {
        auto *directionMenu = parent->addMenu(direction);
        if (entries.empty())
        {
            addNoRelatedEntriesAction(directionMenu);
            return;
        }
        for (const auto *entry : entries)
        {
            const auto name =
                entry->clientName.isEmpty() ? entryDisplayName(*entry) : entry->clientName;
            addEntryAction(directionMenu, *entry, name);
        }
    }

    void rebuildSystemDevicesMenu(const EntryList &wasapiInputEntries,
                                  const EntryList &wasapiOutputEntries,
                                  const EntryList &midiInputEntries,
                                  const EntryList &midiOutputEntries)
    {
        // QMenu::clear removes submenu actions, but QObject-owned submenus
        // survive it. Destroy them so old display text and node callbacks
        // cannot remain among this menu's children after a rebuild.
        qDeleteAll(
            systemDevicesMenu_->findChildren<QMenu *>(QString{}, Qt::FindDirectChildrenOnly));
        systemDevicesMenu_->clear();
        auto *wasapiMenu = systemDevicesMenu_->addMenu(QStringLiteral("WASAPI"));
        addSystemDeviceDirectionMenu(wasapiMenu, hostText("tray.device.input", "In"),
                                     wasapiInputEntries);
        addSystemDeviceDirectionMenu(wasapiMenu, hostText("tray.device.output", "Out"),
                                     wasapiOutputEntries);
        auto *midiMenu = systemDevicesMenu_->addMenu(QStringLiteral("MIDI"));
        addSystemDeviceDirectionMenu(midiMenu, hostText("tray.device.input", "In"),
                                     midiInputEntries);
        addSystemDeviceDirectionMenu(midiMenu, hostText("tray.device.output", "Out"),
                                     midiOutputEntries);
    }

    void recenterAfterLayout()
    {
        layout()->activate();
        if (isVisible())
            setExpanded(expanded_, activeScreen());
    }

    void centerAtTop()
    {
        setExpanded(true, screenForCurrentPosition());
    }

    QHBoxLayout *contentLayout_ = nullptr;
    QToolButton *patchbayButton_ = nullptr;
    QToolButton *meterbridgeButton_ = nullptr;
    QToolButton *switchButton_ = nullptr;
    QMetaObject::Connection switchMenuChanged_;
    QToolButton *mixerButton_ = nullptr;
    QToolButton *themeButton_ = nullptr;
    QToolButton *systemDevicesButton_ = nullptr;
    QToolButton *pinButton_ = nullptr;
    QToolButton *hideButton_ = nullptr;
    Controls::IconActionButton *settingsButton_ = nullptr;
    Controls::IconActionButton *fileButton_ = nullptr;
    QAction *patchbayAction_ = nullptr;
    QAction *meterbridgeAction_ = nullptr;
    QAction *mixerAction_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    Controls::ProgressField *themeProgress_ = nullptr;
    QMenu *systemDevicesMenu_ = nullptr;
    QString entriesSignature_;
    QPointer<QScreen> activeScreen_;
    bool entriesInitialized_ = false;
    bool expanded_ = true;
    int opacityPercent_ = quickAccessOpacityPercent;
    QTimer autoHideTimer_;
};

}

class TrayController::Impl
{
  public:
    QSystemTrayIcon *tray = nullptr;
    QMenu *menu = nullptr;
    QAction *placeholderPatchbay = nullptr;
    QAction *placeholderMixer = nullptr;
    QAction *mixerDetails = nullptr;
    QAction *mixerOverview = nullptr;
    QAction *placeholderMeterbridge = nullptr;
    QAction *quickAccessAction = nullptr;
    QAction *nodeGuiSeparator = nullptr;
    QAction *quitAction = nullptr;
    QuickAccessBar *quickAccessBar = nullptr;
    QPointer<QMenu> quickAccessThemeMenu;
    QPointer<QAction> settingsAction;
};

TrayController::TrayController(QObject *parent) : QObject(parent), impl_(new Impl)
{
    trayControllers.push_back(this);
}

TrayController::~TrayController()
{
    shutdown();
    std::erase(trayControllers, this);
    delete impl_;
}

bool TrayController::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool TrayController::initialize(const QIcon &icon, const QString &tooltip)
{
    if (impl_->tray != nullptr)
        return true;
    if (!isAvailable())
        return false;

    impl_->tray = new QSystemTrayIcon(this);
    impl_->tray->setIcon(icon.isNull() ? QApplication::windowIcon() : icon);
    impl_->tray->setToolTip(tooltip.isEmpty() ? QStringLiteral("WinJACKNexus") : tooltip);

    connect(impl_->tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                Q_EMIT activated(static_cast<int>(reason));
            });

    impl_->menu = new QMenu(nullptr);
    buildPlaceholderMenu();
    rebuildQuickAccessBar();
    impl_->quickAccessBar->showCentered();
    impl_->tray->setContextMenu(impl_->menu);
    impl_->tray->show();
    if (auto *manager = I18nManager::installed())
        manager->bind(this, [this] { retranslate(); });
    return true;
}
void TrayController::initializeFallback()
{
    if (impl_->menu)
        return;
    impl_->menu = new QMenu;
    buildPlaceholderMenu();
    rebuildQuickAccessBar();
    impl_->quickAccessBar->showCentered();
    if (auto *manager = I18nManager::installed())
        manager->bind(this, [this] { retranslate(); });
}

void TrayController::shutdown()
{
    if (impl_->tray != nullptr)
    {
        impl_->tray->hide();
        impl_->tray->setContextMenu(nullptr);
        impl_->tray->deleteLater();
        impl_->tray = nullptr;
    }
    if (impl_->menu != nullptr)
    {
        impl_->menu->deleteLater();
        impl_->menu = nullptr;
    }
    if (impl_->quickAccessBar != nullptr)
    {
        impl_->quickAccessBar->hide();
        impl_->quickAccessBar->deleteLater();
        impl_->quickAccessBar = nullptr;
    }
    impl_->placeholderPatchbay = nullptr;
    impl_->placeholderMixer = nullptr;
    impl_->mixerDetails = nullptr;
    impl_->mixerOverview = nullptr;
    impl_->placeholderMeterbridge = nullptr;
    impl_->quickAccessAction = nullptr;
    impl_->nodeGuiSeparator = nullptr;
    impl_->quitAction = nullptr;
    impl_->settingsAction.clear();
}

bool TrayController::isVisible() const
{
    return impl_->tray != nullptr && impl_->tray->isVisible();
}

void TrayController::setVisible(bool visible)
{
    if (impl_->tray != nullptr)
    {
        if (visible)
            impl_->tray->show();
        else
            impl_->tray->hide();
    }
}

void TrayController::setIcon(const QIcon &icon)
{
    if (impl_->tray != nullptr)
        impl_->tray->setIcon(icon);
}

QIcon TrayController::icon() const
{
    return impl_->tray != nullptr ? impl_->tray->icon() : QIcon();
}

void TrayController::setToolTip(const QString &tooltip)
{
    if (impl_->tray != nullptr)
        impl_->tray->setToolTip(tooltip);
}

QString TrayController::toolTip() const
{
    return impl_->tray != nullptr ? impl_->tray->toolTip() : QString();
}

QMenu *TrayController::contextMenu() const
{
    return impl_->menu;
}

void TrayController::buildPlaceholderMenu()
{
    impl_->placeholderPatchbay = impl_->menu->addAction(hostText("view.patchbay", "Patchbay"));
    impl_->placeholderPatchbay->setEnabled(false);
    impl_->placeholderPatchbay->setToolTip(
        hostText("tray.nodePending", "Available after node registration"));

    impl_->placeholderMixer = new QAction(hostText("view.mixer", "Mixer"), impl_->menu);
    impl_->placeholderMixer->setEnabled(false);
    impl_->placeholderMixer->setToolTip(
        hostText("tray.quickOpen", "Open from the quick access bar"));
    auto *mixerMenu = new QMenu(impl_->menu);
    mixerMenu->setObjectName(QStringLiteral("quickAccessMixerMenu"));
    auto *openMixer = mixerMenu->addAction(hostText("view.mixer", "Mixer"));
    connect(openMixer, &QAction::triggered, impl_->placeholderMixer, &QAction::trigger);
    impl_->mixerDetails = mixerMenu->addAction(hostText("view.channelDetails", "Channel details"));
    impl_->mixerOverview =
        mixerMenu->addAction(hostText("view.channelOverview", "Channel overview"));
    impl_->placeholderMixer->setMenu(mixerMenu);
    for (auto *action : mixerMenu->actions())
        action->setEnabled(false);

    impl_->placeholderMeterbridge =
        new QAction(hostText("view.meterbridge", "Meter bridge"), impl_->menu);
    impl_->placeholderMeterbridge->setEnabled(false);
    impl_->placeholderMeterbridge->setToolTip(
        hostText("tray.quickOpen", "Open from the quick access bar"));

    impl_->quickAccessAction = impl_->menu->addAction(hostText("tray.quickAccess", "Quick access"));
    connect(impl_->quickAccessAction, &QAction::triggered, this, [this] {
        if (impl_->quickAccessBar != nullptr)
            impl_->quickAccessBar->showCentered();
    });

    impl_->nodeGuiSeparator = impl_->menu->addSeparator();
    impl_->quitAction = impl_->menu->addAction(hostText("action.quit", "Quit WinJACKNexus"));
    connect(impl_->quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    if (auto *host = HostI18n::installed())
    {
        auto *languages = host->createLanguageMenu(impl_->menu);
        impl_->menu->insertMenu(impl_->nodeGuiSeparator, languages);
    }
}
void TrayController::retranslate()
{
    if (!impl_->menu)
        return;
    impl_->placeholderPatchbay->setText(hostText("view.patchbay", "Patchbay"));
    impl_->placeholderPatchbay->setToolTip(
        hostText("tray.nodePending", "Available after node registration"));
    impl_->placeholderMixer->setText(hostText("view.mixer", "Mixer"));
    impl_->placeholderMixer->setToolTip(
        hostText("tray.quickOpen", "Open from the quick access bar"));
    impl_->placeholderMixer->menu()->actions().first()->setText(hostText("view.mixer", "Mixer"));
    impl_->mixerDetails->setText(hostText("view.channelDetails", "Channel details"));
    impl_->mixerOverview->setText(hostText("view.channelOverview", "Channel overview"));
    impl_->placeholderMeterbridge->setText(hostText("view.meterbridge", "Meter bridge"));
    impl_->placeholderMeterbridge->setToolTip(
        hostText("tray.quickOpen", "Open from the quick access bar"));
    impl_->quickAccessAction->setText(hostText("tray.quickAccess", "Quick access"));
    impl_->quitAction->setText(hostText("action.quit", "Quit WinJACKNexus"));
}

void TrayController::setNodeGuiEntries(std::vector<TrayNodeGuiEntry> entries)
{
    setNodeGuiEntries(std::move(entries), {});
}
void TrayController::setNodeGuiEntries(std::vector<TrayNodeGuiEntry> entries,
                                       const QHash<QString, QString> &displayNames)
{
    nodeGuiDisplayNames.clear();
    for (const auto &entry : entries)
        if (const auto found = displayNames.constFind(entry.id);
            found != displayNames.cend() && !found->isEmpty())
            nodeGuiDisplayNames.insert(entry.id, *found);
    std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
        if (left.category == right.category && left.clientName == right.clientName)
            return left.name < right.name;
        if (left.category == right.category)
            return left.clientName < right.clientName;
        return left.category < right.category;
    });
    nodeGuiEntries = std::move(entries);
    for (auto *controller : trayControllers)
    {
        if (controller != nullptr)
        {
            if (controller->impl_->placeholderMixer != nullptr)
            {
                const bool mixerLoaded = std::any_of(
                    nodeGuiEntries.cbegin(), nodeGuiEntries.cend(), [](const auto &entry) {
                        return entry.category.compare(QStringLiteral("Digital Mixer"),
                                                      Qt::CaseInsensitive) == 0;
                    });
                controller->impl_->placeholderMixer->setEnabled(mixerLoaded);
                for (auto *action : controller->impl_->placeholderMixer->menu()->actions())
                    action->setEnabled(mixerLoaded);
            }
            controller->rebuildQuickAccessBar();
        }
    }
}

void TrayController::clearNodeGuiEntries()
{
    setNodeGuiEntries({});
}

void TrayController::setQuickAccessStatus(const QString &status)
{
    quickAccessStatus = status;
    for (auto *controller : trayControllers)
    {
        if (controller != nullptr && controller->impl_->quickAccessBar != nullptr)
            controller->impl_->quickAccessBar->setStatus(quickAccessStatus);
    }
}

void TrayController::setQuickAccessOpacity(int percent)
{
    quickAccessOpacityPercent = std::clamp(percent, 10, 100);
    for (auto *controller : trayControllers)
    {
        if (controller != nullptr && controller->impl_->quickAccessBar != nullptr)
            controller->impl_->quickAccessBar->setOpacity(quickAccessOpacityPercent);
    }
}

int TrayController::quickAccessOpacity()
{
    return quickAccessOpacityPercent;
}

void TrayController::rebuildQuickAccessBar()
{
    if (impl_->menu == nullptr || impl_->nodeGuiSeparator == nullptr)
        return;

    if (impl_->quickAccessBar == nullptr)
    {
        impl_->quickAccessBar = new QuickAccessBar;
        impl_->quickAccessBar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(impl_->quickAccessBar, &QWidget::customContextMenuRequested, this,
                [this](const QPoint &point) {
                    if (impl_->menu)
                        impl_->menu->popup(impl_->quickAccessBar->mapToGlobal(point));
                });
    }
    impl_->quickAccessBar->setPatchbayAction(impl_->placeholderPatchbay);
    impl_->quickAccessBar->setMixerAction(impl_->placeholderMixer);
    impl_->quickAccessBar->setMeterbridgeAction(impl_->placeholderMeterbridge);
    impl_->quickAccessBar->setSwitchMenu(quickAccessSwitchMenu);
    impl_->quickAccessBar->setThemeMenu(impl_->quickAccessThemeMenu);
    impl_->quickAccessBar->setFileMenu(quickAccessFileMenu);
    impl_->quickAccessBar->setSettingsAction(impl_->settingsAction);
    impl_->quickAccessBar->setStatus(quickAccessStatus);
    impl_->quickAccessBar->setEntries(nodeGuiEntries);
    impl_->quickAccessAction->setEnabled(true);
}

QAction *TrayController::patchbayAction() const
{
    return impl_->placeholderPatchbay;
}

QAction *TrayController::mixerAction() const
{
    return impl_->placeholderMixer;
}

QAction *TrayController::mixerDetailsAction() const
{
    return impl_->mixerDetails;
}
QAction *TrayController::mixerOverviewAction() const
{
    return impl_->mixerOverview;
}

QAction *TrayController::meterbridgeAction() const
{
    return impl_->placeholderMeterbridge;
}

QAction *TrayController::switchAction() const
{
    return quickAccessSwitchMenu ? quickAccessSwitchMenu->menuAction() : nullptr;
}

void TrayController::setQuickAccessSwitchMenu(QMenu *menu)
{
    quickAccessSwitchMenu = menu;
    for (auto *controller : trayControllers)
    {
        if (controller->impl_->quickAccessBar)
            controller->impl_->quickAccessBar->setSwitchMenu(menu);
    }
}

void TrayController::setQuickAccessThemeMenu(QMenu *menu)
{
    impl_->quickAccessThemeMenu = menu;
    if (impl_->quickAccessBar != nullptr)
        impl_->quickAccessBar->setThemeMenu(menu);
}
void TrayController::setQuickAccessSettingsAction(QAction *action)
{
    impl_->settingsAction = action;
    if (impl_->quickAccessBar)
        impl_->quickAccessBar->setSettingsAction(action);
}

void TrayController::setQuickAccessFileMenu(QMenu *menu)
{
    QObject::disconnect(quickAccessFileMenuDestroyed);
    quickAccessFileMenu = menu;
    if (menu)
        quickAccessFileMenuDestroyed =
            QObject::connect(menu, &QObject::destroyed, [] { setQuickAccessFileMenu(nullptr); });
    for (auto *controller : trayControllers)
        if (controller->impl_->quickAccessBar)
            controller->impl_->quickAccessBar->setFileMenu(menu);
}

void TrayController::setThemeLoadingProgress(int percent)
{
    if (impl_->quickAccessBar != nullptr)
        impl_->quickAccessBar->setThemeLoadingProgress(percent);
}

void TrayController::setPlaceholderEnabled(bool enabled)
{
    for (QAction *action :
         {impl_->placeholderPatchbay, impl_->placeholderMixer, impl_->placeholderMeterbridge})
    {
        if (action != nullptr)
            action->setEnabled(enabled);
    }
}

void TrayController::registerPlaceholder(const QAction *action)
{
    // 对应节点注册后启用(替换文本/行为由调用方决定,此处仅解锁)
    if (action != nullptr)
        const_cast<QAction *>(action)->setEnabled(true);
}

QAction *TrayController::addMenuAction(const QString &text, const QIcon &icon)
{
    return insertMenuActionBefore(impl_->quitAction, text, icon);
}

QAction *TrayController::insertMenuActionBefore(QAction *before, const QString &text,
                                                const QIcon &icon)
{
    if (impl_->menu == nullptr)
        return nullptr;
    auto *action = new QAction(text, impl_->menu);
    if (!icon.isNull())
        action->setIcon(icon);
    impl_->menu->insertAction(before, action);
    return action;
}

void TrayController::removeMenuAction(QAction *action)
{
    if (impl_->menu == nullptr || action == nullptr)
        return;
    impl_->menu->removeAction(action);
    action->deleteLater();
}

void TrayController::showMessage(const QString &title, const QString &message, int iconType,
                                 int timeoutMs)
{
    if (impl_->tray == nullptr)
        return;

    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    if (iconType == 1)
        icon = QSystemTrayIcon::Warning;
    else if (iconType == 2)
        icon = QSystemTrayIcon::Critical;

    impl_->tray->showMessage(title, message, icon, timeoutMs);
}

} // namespace WinJACKNexus::UI
