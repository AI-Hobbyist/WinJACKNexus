#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QIcon>
#include <QObject>
#include <QString>

class QAction;
class QMenu;

namespace WinJACKNexus::UI {

/**
 * 系统托盘图标(后台常驻守护)。
 *
 * 封装 QSystemTrayIcon,提供:
 *  - 基础占位菜单:接线盘 / 调音台 / 电平表桥 / 信号切换开关(占位时禁用,待节点注册后启用/替换);
 *  - 动态菜单注入口:节点 DLL 注册后注入真实菜单项;
 *  - 退出入口(quitRequested)、气泡通知(showMessage)与激活回调(activated)。
 *
 * 仅允许在 Qt GUI 线程使用。若系统托盘不可用,initialize() 返回 false,调用方自行降级。
 */
class WINJACK_UI_API TrayController final : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QObject* parent = nullptr);
    ~TrayController() override;

    /**
     * 初始化并显示托盘图标。
     * @param icon    图标;为空时使用 QApplication::windowIcon()
     * @param tooltip 悬浮提示;为空时使用 "WinJACKNexus"
     * @return 托盘是否可用并初始化成功
     */
    bool initialize(const QIcon& icon = {}, const QString& tooltip = {});
    /** 隐藏并释放托盘资源(析构时自动调用) */
    void shutdown();

    /** 系统托盘在当前环境是否可用 */
    static bool isAvailable();
    bool isVisible() const;
    void setVisible(bool visible);

    void setIcon(const QIcon& icon);
    QIcon icon() const;
    void setToolTip(const QString& tooltip);
    QString toolTip() const;

    /** 托盘上下文菜单(所有权由本类管理) */
    QMenu* contextMenu() const;

    // --- 基础占位入口(对应规范 §3.0) ---
    QAction* patchbayAction() const;     // 接线盘
    QAction* mixerAction() const;        // 调音台
    QAction* meterbridgeAction() const;  // 电平表桥
    QAction* switchAction() const;       // 信号切换开关
    void setPlaceholderEnabled(bool enabled);          // 统一启用/禁用全部占位入口
    void registerPlaceholder(const QAction* action);   // 对应节点注册后启用该占位项

    // --- 动态菜单注入口 ---
    QAction* addMenuAction(const QString& text, const QIcon& icon = {});
    QAction* insertMenuActionBefore(QAction* before, const QString& text, const QIcon& icon = {});
    void removeMenuAction(QAction* action);

    /** 气泡通知;iconType: 0=Information 1=Warning 2=Critical */
    void showMessage(const QString& title, const QString& message,
                     int iconType = 0, int timeoutMs = 5000);

Q_SIGNALS:
    /** 托盘激活(int 为 QSystemTrayIcon::ActivationReason;1=Trigger 2=DoubleClick 3=MiddleClick) */
    void activated(int reason);
    /** 托盘菜单“退出”被点击 */
    void quitRequested();

private:
    void buildPlaceholderMenu();

    class Impl;
    Impl* impl_;
};

}