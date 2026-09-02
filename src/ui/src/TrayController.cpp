#include <WinJACKNexus/UI/TrayController.hpp>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>

namespace WinJACKNexus::UI {

class TrayController::Impl {
public:
    QSystemTrayIcon* tray = nullptr;
    QMenu* menu = nullptr;
    QAction* placeholderPatchbay = nullptr;
    QAction* placeholderMixer = nullptr;
    QAction* placeholderMeterbridge = nullptr;
    QAction* placeholderSwitch = nullptr;
    QAction* quitAction = nullptr;
};

TrayController::TrayController(QObject* parent)
    : QObject(parent)
    , impl_(new Impl)
{
}

TrayController::~TrayController()
{
    shutdown();
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
    impl_->placeholderPatchbay = nullptr;
    impl_->placeholderMixer = nullptr;
    impl_->placeholderMeterbridge = nullptr;
    impl_->placeholderSwitch = nullptr;
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
    impl_->placeholderMixer = impl_->menu->addAction(QStringLiteral("调音台"));
    impl_->placeholderMeterbridge = impl_->menu->addAction(QStringLiteral("电平表桥"));
    impl_->placeholderSwitch = impl_->menu->addAction(QStringLiteral("信号切换开关"));

    // 占位:对应节点注册前禁用
    for (QAction* action : { impl_->placeholderPatchbay,
                             impl_->placeholderMixer,
                             impl_->placeholderMeterbridge,
                             impl_->placeholderSwitch })
    {
        action->setEnabled(false);
        action->setToolTip(QStringLiteral("敬请期待:节点注册后可用"));
    }

    impl_->menu->addSeparator();
    impl_->quitAction = impl_->menu->addAction(QStringLiteral("退出 WinJACKNexus"));
    connect(impl_->quitAction, &QAction::triggered, this, &TrayController::quitRequested);
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
    return impl_->placeholderSwitch;
}

void TrayController::setPlaceholderEnabled(bool enabled)
{
    for (QAction* action : { impl_->placeholderPatchbay,
                             impl_->placeholderMixer,
                             impl_->placeholderMeterbridge,
                             impl_->placeholderSwitch })
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