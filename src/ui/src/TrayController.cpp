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

#include <algorithm>
#include <map>

namespace WinJACKNexus::UI {

namespace {

std::vector<TrayController*> trayControllers;
std::vector<TrayNodeGuiEntry> nodeGuiEntries;
QString quickAccessStatus;
constexpr int quickAccessSideRegionWidth = 128;
constexpr int quickAccessAutoHideIntervalMilliseconds = 40;
constexpr int quickAccessTopEdgeHeight = 2;
int quickAccessOpacityPercent = 50;

enum class QuickAccessIcon {
    pin,
    hide,
    theme
};

class QuickAccessIconButton final : public QToolButton {
public:
    explicit QuickAccessIconButton(QuickAccessIcon icon, QWidget* parent = nullptr)
        : QToolButton(parent)
        , icon_(icon)
    {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto bounds = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const auto active = isCheckable() && isChecked();
        const auto background = isDown() ? QColor(QStringLiteral("#1e5068"))
            : underMouse() ? QColor(QStringLiteral("#3d4854"))
            : active ? QColor(QStringLiteral("#275d75"))
            : QColor(QStringLiteral("#303841"));
        painter.setPen(QPen(active ? QColor(QStringLiteral("#5db1d7")) : QColor(QStringLiteral("#5a6570"))));
        painter.setBrush(background);
        painter.drawRoundedRect(bounds, 4.0, 4.0);

        const auto iconColor = active ? QColor(QStringLiteral("#a8e6ff")) : QColor(QStringLiteral("#f1f5f8"));
        painter.setPen(QPen(iconColor, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        if (icon_ == QuickAccessIcon::pin) {
            QPolygonF pin { QPointF(9.0, 7.0), QPointF(19.0, 7.0), QPointF(17.0, 12.0),
                            QPointF(17.0, 14.0), QPointF(11.0, 14.0), QPointF(11.0, 12.0),
                            QPointF(9.0, 7.0) };
            painter.drawPolyline(pin);
            painter.drawLine(QPointF(14.0, 14.0), QPointF(14.0, 21.0));
        } else if (icon_ == QuickAccessIcon::hide) {
            painter.drawEllipse(QRectF(5.0, 9.0, 18.0, 10.0));
            painter.drawEllipse(QRectF(12.0, 12.0, 4.0, 4.0));
            painter.drawLine(QPointF(6.0, 7.0), QPointF(22.0, 21.0));
        } else {
            painter.drawEllipse(QRectF(5.0, 5.0, 18.0, 18.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(iconColor);
            painter.drawEllipse(QRectF(8.0, 9.0, 3.0, 3.0));
            painter.drawEllipse(QRectF(13.0, 7.0, 3.0, 3.0));
            painter.drawEllipse(QRectF(18.0, 10.0, 3.0, 3.0));
            painter.drawEllipse(QRectF(8.0, 16.0, 3.0, 3.0));
            painter.setBrush(background);
            painter.drawEllipse(QRectF(14.0, 15.0, 7.0, 6.0));
        }
    }

private:
    QuickAccessIcon icon_;
};

class QuickAccessBar final : public QFrame {
    using EntryList = std::vector<const TrayNodeGuiEntry*>;

    enum class SystemDeviceType {
        none,
        wasapiInput,
        wasapiOutput,
        midiInput,
        midiOutput
    };

public:
    explicit QuickAccessBar(QWidget* parent = nullptr)
    : QFrame(parent)
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_DeleteOnClose, false);
        setObjectName(QStringLiteral("quickAccessBar"));
        setStyleSheet(QStringLiteral(
            "QFrame#quickAccessBar { background: #1f2428; border: 1px solid #5a6570;"
            " border-top-left-radius: 0px; border-top-right-radius: 0px;"
            " border-bottom-left-radius: 6px; border-bottom-right-radius: 6px; }"
            "QLabel { color: #d8dee4; }"
            "QToolButton { color: #f1f5f8; background: #303841; border: 1px solid #5a6570; border-radius: 4px; padding: 5px 9px; }"
            "QToolButton:hover { background: #3d4854; }"
            "QToolButton:checked { background: #275d75; border-color: #5db1d7; }"));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 7, 10, 7);
        layout->setSpacing(8);
        auto* title = new Controls::TextLabel(QStringLiteral("WinJACKNexus"), this);
        title->setStyleSheet(QStringLiteral("font-weight: 700;"));
        title->setFixedWidth(quickAccessSideRegionWidth);
        title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(title);
        layout->addStretch();

        patchbayButton_ = new Controls::IconActionButton(this);
        patchbayButton_->setText(QStringLiteral("接线盘"));
        patchbayButton_->setToolTip(QStringLiteral("打开接线盘"));
        layout->addWidget(patchbayButton_);

        meterbridgeButton_ = new Controls::IconActionButton(this);
        meterbridgeButton_->setText(QStringLiteral("电平表桥"));
        meterbridgeButton_->setToolTip(QStringLiteral("打开电平表桥"));
        layout->addWidget(meterbridgeButton_);

        mixerButton_ = new Controls::IconActionButton(this);
        mixerButton_->setText(QStringLiteral("调音台"));
        mixerButton_->setToolTip(QStringLiteral("打开调音台"));
        layout->addWidget(mixerButton_);

        systemDevicesButton_ = new Controls::IconActionButton(this);
        systemDevicesButton_->setText(QStringLiteral("系统设备"));
        systemDevicesButton_->setToolTip(QStringLiteral("打开已添加的 WASAPI 和 MIDI 设备节点"));
        systemDevicesButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        systemDevicesButton_->setPopupMode(QToolButton::InstantPopup);
        systemDevicesMenu_ = new QMenu(systemDevicesButton_);
        systemDevicesButton_->setMenu(systemDevicesMenu_);
        layout->addWidget(systemDevicesButton_);

        contentLayout_ = new QHBoxLayout;
        contentLayout_->setSpacing(6);
        layout->addLayout(contentLayout_);

        pinButton_ = new QuickAccessIconButton(QuickAccessIcon::pin, this);
        pinButton_->setObjectName(QStringLiteral("quickAccessPinButton"));
        pinButton_->setCheckable(true);
        pinButton_->setChecked(true);
        pinButton_->setToolTip(QStringLiteral("保持快捷栏位于其他窗口上方"));
        pinButton_->setAccessibleName(QStringLiteral("置顶快捷栏"));
        layout->addWidget(pinButton_);
        setOpacity(quickAccessOpacityPercent);
        connect(pinButton_, &QToolButton::toggled, this, [this](bool pinned) {
            setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
            show();
            centerAtTop();
        });

        auto* hideButton = new QuickAccessIconButton(QuickAccessIcon::hide, this);
        hideButton->setObjectName(QStringLiteral("quickAccessHideButton"));
        hideButton->setToolTip(QStringLiteral("收起快捷栏"));
        hideButton->setAccessibleName(QStringLiteral("收起快捷栏"));
        layout->addWidget(hideButton);
        connect(hideButton, &QToolButton::clicked, this, [this] { setExpanded(false, activeScreen()); });

        themeButton_ = new QuickAccessIconButton(QuickAccessIcon::theme, this);
        themeButton_->setObjectName(QStringLiteral("quickAccessThemeButton"));
        themeButton_->setToolTip(QStringLiteral("切换皮肤"));
        themeButton_->setAccessibleName(QStringLiteral("切换皮肤"));
        themeButton_->setPopupMode(QToolButton::InstantPopup);
        layout->addWidget(themeButton_);
        layout->addStretch();

        statusLabel_ = new Controls::TextLabel(this);
        statusLabel_->setFixedWidth(quickAccessSideRegionWidth);
        statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        statusLabel_->setToolTip(QStringLiteral("JACK DSP 使用率与 XRun 计数"));
        layout->addWidget(statusLabel_);

        autoHideTimer_.setInterval(quickAccessAutoHideIntervalMilliseconds);
        connect(&autoHideTimer_, &QTimer::timeout, this, [this] {
            updateAutoVisibility();
        });
        autoHideTimer_.start();
    }

    void setPatchbayAction(QAction* action)
    {
        patchbayButton_->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr && patchbayAction_ == nullptr) {
            patchbayAction_ = action;
            connect(patchbayButton_, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, this, [this, action] {
                patchbayButton_->setEnabled(action->isEnabled());
            });
        }
    }

    void setMeterbridgeAction(QAction* action)
    {
        meterbridgeButton_->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr && meterbridgeAction_ == nullptr) {
            meterbridgeAction_ = action;
            connect(meterbridgeButton_, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, this, [this, action] {
                meterbridgeButton_->setEnabled(action->isEnabled());
            });
        }
    }

    void setMixerAction(QAction* action)
    {
        mixerButton_->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr && mixerAction_ == nullptr) {
            mixerAction_ = action;
            connect(mixerButton_, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, this, [this, action] {
                mixerButton_->setEnabled(action->isEnabled());
            });
        }
    }

    void setThemeMenu(QMenu* menu)
    {
        themeButton_->setMenu(menu);
        themeButton_->setEnabled(menu != nullptr);
    }

    void setEntries(const std::vector<TrayNodeGuiEntry>& entries)
    {
        const auto signature = entriesSignature(entries);
        if (entriesInitialized_ && signature == entriesSignature_)
            return;

        while (auto* item = contentLayout_->takeAt(0)) {
            delete item->widget();
            delete item;
        }

        EntryList wasapiInputEntries;
        EntryList wasapiOutputEntries;
        EntryList midiInputEntries;
        EntryList midiOutputEntries;
        EntryList vstEntries;
        std::map<QString, std::map<QString, EntryList>> groupedEntries;
        for (const auto& entry : entries) {
            if (entry.id.isEmpty() || entry.name.isEmpty() || !entry.show)
                continue;
            if (entry.category.compare(QStringLiteral("Meter"), Qt::CaseInsensitive) == 0)
                continue;
            if (entry.category.compare(QStringLiteral("Digital Mixer"), Qt::CaseInsensitive) == 0)
                continue;

            switch (systemDeviceType(entry)) {
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

            if (isVstEntry(entry)) {
                vstEntries.push_back(&entry);
                continue;
            }

            const auto category = entry.category.isEmpty() ? QStringLiteral("其他") : entry.category;
            const auto project = entry.clientName.isEmpty() ? entry.name : entry.clientName;
            groupedEntries[category][project].push_back(&entry);
        }

        rebuildSystemDevicesMenu(wasapiInputEntries, wasapiOutputEntries,
                                 midiInputEntries, midiOutputEntries);

        auto* vstButton = new Controls::IconActionButton(this);
        vstButton->setText(QStringLiteral("VST"));
        vstButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        vstButton->setPopupMode(QToolButton::InstantPopup);
        auto* vstMenu = new QMenu(vstButton);
        addProjectEntries(vstMenu, vstEntries);
        vstButton->setMenu(vstMenu);
        contentLayout_->addWidget(vstButton);

        for (const auto& [category, projects] : groupedEntries) {
            auto* categoryButton = new Controls::IconActionButton(this);
            categoryButton->setText(category);
            categoryButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
            categoryButton->setPopupMode(QToolButton::InstantPopup);
            auto* categoryMenu = new QMenu(categoryButton);
            EntryList categoryEntries;
            for (const auto& [project, nodes] : projects)
                categoryEntries.insert(categoryEntries.end(), nodes.cbegin(), nodes.cend());
            addProjectEntries(categoryMenu, categoryEntries);
            categoryButton->setMenu(categoryMenu);
            contentLayout_->addWidget(categoryButton);
        }
        entriesSignature_ = signature;
        entriesInitialized_ = true;
        recenterAfterLayout();
    }

    void setStatus(const QString& status)
    {
        statusLabel_->setText(status);
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
        if (auto* topEdgeScreen = screenAtTopEdge(cursorPosition); topEdgeScreen != nullptr) {
            setExpanded(true, topEdgeScreen);
            return;
        }

        if (expanded_ && !isCursorOverInteractiveSurface(cursorPosition))
            setExpanded(false, activeScreen());
    }

    static QScreen* screenAtTopEdge(const QPoint& position)
    {
        for (auto* screen : QGuiApplication::screens()) {
            const auto geometry = screen->geometry();
            if (position.x() >= geometry.left() && position.x() <= geometry.right()
                && position.y() >= geometry.top()
                && position.y() < geometry.top() + quickAccessTopEdgeHeight)
                return screen;
        }
        return nullptr;
    }

    QScreen* activeScreen() const
    {
        return activeScreen_.isNull() ? QGuiApplication::primaryScreen() : activeScreen_.data();
    }

    QScreen* screenForCurrentPosition() const
    {
        if (auto* screen = QGuiApplication::screenAt(QCursor::pos()); screen != nullptr)
            return screen;
        return activeScreen();
    }

    bool isCursorOverInteractiveSurface(const QPoint& position) const
    {
        if (frameGeometry().contains(position))
            return true;
        const auto* popup = QApplication::activePopupWidget();
        return popup != nullptr && popup->isVisible() && popup->frameGeometry().contains(position);
    }

    QRect geometryForState(bool expanded, QScreen* screen) const
    {
        if (screen == nullptr)
            return {};
        const auto screenGeometry = screen->geometry();
        const auto height = sizeHint().height();
        const auto top = expanded
            ? screenGeometry.top()
            : screenGeometry.top() - height + quickAccessTopEdgeHeight;
        return { screenGeometry.left(), top, screenGeometry.width(), height };
    }

    void setExpanded(bool expanded, QScreen* screen)
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

    static QString entriesSignature(const std::vector<TrayNodeGuiEntry>& entries)
    {
        QString signature;
        for (const auto& entry : entries) {
            signature += entry.id;
            signature += QChar::Null;
            signature += entry.category;
            signature += QChar::Null;
            signature += entry.clientName;
            signature += QChar::Null;
            signature += entry.name;
            signature += QChar::Null;
        }
        return signature;
    }

    static SystemDeviceType systemDeviceType(const TrayNodeGuiEntry& entry)
    {
        if (entry.category.compare(QStringLiteral("Windows Native Driver"), Qt::CaseInsensitive) != 0)
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

    static bool isVstEntry(const TrayNodeGuiEntry& entry)
    {
        return entry.category.startsWith(QStringLiteral("VST"), Qt::CaseInsensitive);
    }

    static void addNoRelatedEntriesAction(QMenu* menu)
    {
        auto* action = menu->addAction(QStringLiteral("无相关条目"));
        action->setEnabled(false);
    }

    void addEntryAction(QMenu* menu, const TrayNodeGuiEntry& entry, const QString& text)
    {
        auto* action = menu->addAction(text);
        connect(action, &QAction::triggered, this, [show = entry.show] {
            show();
        });
    }

    void addProjectEntries(QMenu* menu, const EntryList& entries)
    {
        if (entries.empty()) {
            addNoRelatedEntriesAction(menu);
            return;
        }

        std::map<QString, EntryList> projects;
        for (const auto* entry : entries) {
            const auto project = entry->clientName.isEmpty() ? entry->name : entry->clientName;
            projects[project].push_back(entry);
        }
        for (const auto& [project, nodes] : projects) {
            if (nodes.size() == 1) {
                addEntryAction(menu, *nodes.front(), project);
                continue;
            }
            auto* nodeMenu = menu->addMenu(project);
            for (const auto* node : nodes)
                addEntryAction(nodeMenu, *node, node->name);
        }
    }

    void addSystemDeviceDirectionMenu(QMenu* parent, const QString& direction, const EntryList& entries)
    {
        auto* directionMenu = parent->addMenu(direction);
        if (entries.empty()) {
            addNoRelatedEntriesAction(directionMenu);
            return;
        }
        for (const auto* entry : entries) {
            const auto name = entry->clientName.isEmpty() ? entry->name : entry->clientName;
            addEntryAction(directionMenu, *entry, name);
        }
    }

    void rebuildSystemDevicesMenu(const EntryList& wasapiInputEntries,
                                  const EntryList& wasapiOutputEntries,
                                  const EntryList& midiInputEntries,
                                  const EntryList& midiOutputEntries)
    {
        systemDevicesMenu_->clear();
        auto* wasapiMenu = systemDevicesMenu_->addMenu(QStringLiteral("WASAPI"));
        addSystemDeviceDirectionMenu(wasapiMenu, QStringLiteral("In"), wasapiInputEntries);
        addSystemDeviceDirectionMenu(wasapiMenu, QStringLiteral("Out"), wasapiOutputEntries);
        auto* midiMenu = systemDevicesMenu_->addMenu(QStringLiteral("MIDI"));
        addSystemDeviceDirectionMenu(midiMenu, QStringLiteral("In"), midiInputEntries);
        addSystemDeviceDirectionMenu(midiMenu, QStringLiteral("Out"), midiOutputEntries);
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

    QHBoxLayout* contentLayout_ = nullptr;
    QToolButton* patchbayButton_ = nullptr;
    QToolButton* meterbridgeButton_ = nullptr;
    QToolButton* mixerButton_ = nullptr;
    QToolButton* themeButton_ = nullptr;
    QToolButton* systemDevicesButton_ = nullptr;
    QToolButton* pinButton_ = nullptr;
    QAction* patchbayAction_ = nullptr;
    QAction* meterbridgeAction_ = nullptr;
    QAction* mixerAction_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QMenu* systemDevicesMenu_ = nullptr;
    QString entriesSignature_;
    QPointer<QScreen> activeScreen_;
    bool entriesInitialized_ = false;
    bool expanded_ = true;
    int opacityPercent_ = quickAccessOpacityPercent;
    QTimer autoHideTimer_;
};

}

class TrayController::Impl {
public:
    QSystemTrayIcon* tray = nullptr;
    QMenu* menu = nullptr;
    QAction* placeholderPatchbay = nullptr;
    QAction* placeholderMixer = nullptr;
    QAction* placeholderMeterbridge = nullptr;
    QAction* quickAccessAction = nullptr;
    QAction* nodeGuiSeparator = nullptr;
    QAction* quitAction = nullptr;
    QuickAccessBar* quickAccessBar = nullptr;
    QPointer<QMenu> quickAccessThemeMenu;
};

TrayController::TrayController(QObject* parent)
    : QObject(parent)
    , impl_(new Impl)
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

bool TrayController::initialize(const QIcon& icon, const QString& tooltip)
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
    return true;
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
    impl_->placeholderMeterbridge = nullptr;
    impl_->quickAccessAction = nullptr;
    impl_->nodeGuiSeparator = nullptr;
    impl_->quitAction = nullptr;
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

void TrayController::setIcon(const QIcon& icon)
{
    if (impl_->tray != nullptr)
        impl_->tray->setIcon(icon);
}

QIcon TrayController::icon() const
{
    return impl_->tray != nullptr ? impl_->tray->icon() : QIcon();
}

void TrayController::setToolTip(const QString& tooltip)
{
    if (impl_->tray != nullptr)
        impl_->tray->setToolTip(tooltip);
}

QString TrayController::toolTip() const
{
    return impl_->tray != nullptr ? impl_->tray->toolTip() : QString();
}

QMenu* TrayController::contextMenu() const
{
    return impl_->menu;
}

void TrayController::buildPlaceholderMenu()
{
    impl_->placeholderPatchbay = impl_->menu->addAction(QStringLiteral("接线盘"));
    impl_->placeholderPatchbay->setEnabled(false);
    impl_->placeholderPatchbay->setToolTip(QStringLiteral("敬请期待:节点注册后可用"));

    impl_->placeholderMixer = new QAction(QStringLiteral("调音台"), impl_->menu);
    impl_->placeholderMixer->setEnabled(false);
    impl_->placeholderMixer->setToolTip(QStringLiteral("由快捷栏打开"));

    impl_->placeholderMeterbridge = new QAction(QStringLiteral("电平表桥"), impl_->menu);
    impl_->placeholderMeterbridge->setEnabled(false);
    impl_->placeholderMeterbridge->setToolTip(QStringLiteral("由快捷栏打开"));

    impl_->quickAccessAction = impl_->menu->addAction(QStringLiteral("快速入口"));
    connect(impl_->quickAccessAction, &QAction::triggered, this, [this] {
        if (impl_->quickAccessBar != nullptr)
            impl_->quickAccessBar->showCentered();
    });

    impl_->nodeGuiSeparator = impl_->menu->addSeparator();
    impl_->quitAction = impl_->menu->addAction(QStringLiteral("退出 WinJACKNexus"));
    connect(impl_->quitAction, &QAction::triggered, this, &TrayController::quitRequested);
}

void TrayController::setNodeGuiEntries(std::vector<TrayNodeGuiEntry> entries)
{
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        if (left.category == right.category && left.clientName == right.clientName)
            return left.name < right.name;
        if (left.category == right.category)
            return left.clientName < right.clientName;
        return left.category < right.category;
    });
    nodeGuiEntries = std::move(entries);
    for (auto* controller : trayControllers) {
        if (controller != nullptr) {
            if (controller->impl_->placeholderMixer != nullptr) {
                const bool mixerLoaded = std::any_of(nodeGuiEntries.cbegin(), nodeGuiEntries.cend(), [](const auto& entry) {
                    return entry.category.compare(QStringLiteral("Digital Mixer"), Qt::CaseInsensitive) == 0;
                });
                controller->impl_->placeholderMixer->setEnabled(mixerLoaded);
            }
            controller->rebuildQuickAccessBar();
        }
    }
}

void TrayController::clearNodeGuiEntries()
{
    setNodeGuiEntries({});
}

void TrayController::setQuickAccessStatus(const QString& status)
{
    quickAccessStatus = status;
    for (auto* controller : trayControllers) {
        if (controller != nullptr && controller->impl_->quickAccessBar != nullptr)
            controller->impl_->quickAccessBar->setStatus(quickAccessStatus);
    }
}

void TrayController::setQuickAccessOpacity(int percent)
{
    quickAccessOpacityPercent = std::clamp(percent, 10, 100);
    for (auto* controller : trayControllers) {
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
        impl_->quickAccessBar = new QuickAccessBar;
    impl_->quickAccessBar->setPatchbayAction(impl_->placeholderPatchbay);
    impl_->quickAccessBar->setMixerAction(impl_->placeholderMixer);
    impl_->quickAccessBar->setMeterbridgeAction(impl_->placeholderMeterbridge);
    impl_->quickAccessBar->setThemeMenu(impl_->quickAccessThemeMenu);
    impl_->quickAccessBar->setStatus(quickAccessStatus);
    impl_->quickAccessBar->setEntries(nodeGuiEntries);
    impl_->quickAccessAction->setEnabled(true);
}

QAction* TrayController::patchbayAction() const
{
    return impl_->placeholderPatchbay;
}

QAction* TrayController::mixerAction() const
{
    return impl_->placeholderMixer;
}

QAction* TrayController::meterbridgeAction() const
{
    return impl_->placeholderMeterbridge;
}

QAction* TrayController::switchAction() const
{
    return nullptr;
}

void TrayController::setQuickAccessThemeMenu(QMenu* menu)
{
    impl_->quickAccessThemeMenu = menu;
    if (impl_->quickAccessBar != nullptr)
        impl_->quickAccessBar->setThemeMenu(menu);
}

void TrayController::setPlaceholderEnabled(bool enabled)
{
    for (QAction* action : { impl_->placeholderPatchbay, impl_->placeholderMixer, impl_->placeholderMeterbridge })
    {
        if (action != nullptr)
            action->setEnabled(enabled);
    }
}

void TrayController::registerPlaceholder(const QAction* action)
{
    // 对应节点注册后启用(替换文本/行为由调用方决定,此处仅解锁)
    if (action != nullptr)
        const_cast<QAction*>(action)->setEnabled(true);
}

QAction* TrayController::addMenuAction(const QString& text, const QIcon& icon)
{
    return insertMenuActionBefore(impl_->quitAction, text, icon);
}

QAction* TrayController::insertMenuActionBefore(QAction* before, const QString& text, const QIcon& icon)
{
    if (impl_->menu == nullptr)
        return nullptr;
    auto* action = new QAction(text, impl_->menu);
    if (!icon.isNull())
        action->setIcon(icon);
    impl_->menu->insertAction(before, action);
    return action;
}

void TrayController::removeMenuAction(QAction* action)
{
    if (impl_->menu == nullptr || action == nullptr)
        return;
    impl_->menu->removeAction(action);
    action->deleteLater();
}

void TrayController::showMessage(const QString& title, const QString& message,
                                 int iconType, int timeoutMs)
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
