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
#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QIcon>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QMenu;

namespace WinJACKNexus::UI {

struct WINJACK_UI_API TrayNodeGuiEntry final {
    QString id;
    QString category;
    QString clientName;
    QString name;
    std::function<bool()> show;
};

/**
 * 系统托盘图标(后台常驻守护)。
 *
 * 封装 QSystemTrayIcon,提供:
 *  - 托盘菜单:接线盘、快速入口和退出;
 *  - 快速入口:位于屏幕上方居中的浮动栏，按节点类型并排显示当前已加载的项目;
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
    /** Build the same native menu and quick bar without an OS tray icon. */
    void initializeFallback();
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

    // --- 托盘入口 ---
    QAction* patchbayAction() const;     // 接线盘
    QAction* mixerAction() const;        // 调音台
    QAction* mixerDetailsAction() const; // 通道详情
    QAction* mixerOverviewAction() const; // 通道总览
    QAction* meterbridgeAction() const;  // 电平表桥
    QAction* switchAction() const;       // 信号切换开关
    void setQuickAccessThemeMenu(QMenu* menu);
    void setQuickAccessSettingsAction(QAction* action);
    /** 将补丁台的原生文件菜单绑定到所有快捷栏；菜单所有权由调用方保留。 */
    static void setQuickAccessFileMenu(QMenu* menu);
    void setPlaceholderEnabled(bool enabled);          // 启用/禁用接线盘入口
    void registerPlaceholder(const QAction* action);   // 对应节点注册后启用该入口

    // --- 动态菜单注入口 ---
    QAction* addMenuAction(const QString& text, const QIcon& icon = {});
    QAction* insertMenuActionBefore(QAction* before, const QString& text, const QIcon& icon = {});
    void removeMenuAction(QAction* action);

    /** 更新所有快速入口栏中按分类组织的自定义节点 GUI 项目。 */
    static void setNodeGuiEntries(std::vector<TrayNodeGuiEntry> entries);
    // Names are display-only, indexed by stable entry.id; the original metadata
    // continues to determine device type, grouping and callback identity.
    static void setNodeGuiEntries(std::vector<TrayNodeGuiEntry> entries,const QHash<QString,QString>& displayNames);
    static void clearNodeGuiEntries();
    /** 更新快速入口栏显示的 JACK DSP 与 XRun 状态。 */
    static void setQuickAccessStatus(const QString& status);
    /** 显示主题加载进度；100 表示完成并隐藏进度条。 */
    void setThemeLoadingProgress(int percent);
    /** 设置快速入口栏透明度，范围为 10 到 100。 */
    static void setQuickAccessOpacity(int percent);
    static int quickAccessOpacity();

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
    void rebuildQuickAccessBar();
    void retranslate();

    class Impl;
    Impl* impl_;
};

}
