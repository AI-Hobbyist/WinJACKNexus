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
#include <WinJACKNexus/UI/IconManager.hpp>
#include <WinJACKNexus/UI/TrayController.hpp>
#include <WinJACKNexus/Node/NodeApi.h>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QStyle>

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace {

class NodeModule final {
public:
    ~NodeModule()
    {
        unload();
    }

    bool load(const QString& filePath)
    {
        unload();

        library_ = LoadLibraryW(reinterpret_cast<LPCWSTR>(filePath.utf16()));
        if (library_ == nullptr)
            return false;

        const auto apiVersion = reinterpret_cast<ApiVersion>(GetProcAddress(library_, "wjn_get_node_api_version"));
        const auto nodeCount = reinterpret_cast<NodeCount>(GetProcAddress(library_, "wjn_get_node_count"));
        const auto create = reinterpret_cast<Create>(GetProcAddress(library_, "wjn_create_node"));
        destroy_ = reinterpret_cast<Destroy>(GetProcAddress(library_, "wjn_destroy_node"));
        if (apiVersion == nullptr || nodeCount == nullptr || create == nullptr || destroy_ == nullptr
            || std::strcmp(apiVersion(), "2") != 0 || nodeCount() == 0) {
            unload();
            return false;
        }

        node_ = create(0);
        if (node_ == nullptr || !node_->init()) {
            unload();
            return false;
        }
        return true;
    }

    WinJACKNexus::Node::INode* node() const noexcept
    {
        return node_;
    }

private:
    using ApiVersion = const char* (*)();
    using NodeCount = std::uint32_t (*)();
    using Create = WinJACKNexus::Node::INode* (*)(std::uint32_t index);
    using Destroy = void (*)(WinJACKNexus::Node::INode*);

    void unload() noexcept
    {
        if (node_ != nullptr) {
            node_->shutdown();
            destroy_(node_);
            node_ = nullptr;
        }
        destroy_ = nullptr;
        if (library_ != nullptr) {
            FreeLibrary(library_);
            library_ = nullptr;
        }
    }

    HMODULE library_ = nullptr;
    WinJACKNexus::Node::INode* node_ = nullptr;
    Destroy destroy_ = nullptr;
};

QString executableDirectory()
{
    wchar_t path[MAX_PATH] {};
    const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return length == 0 ? QString() : QFileInfo(QString::fromWCharArray(path, static_cast<qsizetype>(length))).absolutePath();
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("WinJACKNexus"));
    application.setQuitOnLastWindowClosed(false);

    QIcon applicationIcon = WinJACKNexus::UI::IconManager::icon(QStringLiteral("settings"));
    if (applicationIcon.isNull())
        applicationIcon = application.style()->standardIcon(QStyle::SP_ComputerIcon);
    application.setWindowIcon(applicationIcon);

    WinJACKNexus::UI::TrayController tray;
    const bool trayAvailable = tray.initialize(applicationIcon, application.applicationName());
    if (trayAvailable) {
        QObject::connect(&tray, &WinJACKNexus::UI::TrayController::quitRequested,
                         &application, &QApplication::quit);
    }

    NodeModule patchbayModule;
    const auto patchbayPath = QDir(executableDirectory()).filePath(QStringLiteral("mod_patchbay.dll"));
    if (!patchbayModule.load(patchbayPath)) {
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("无法加载接线盘模块。"));
        return 1;
    }

    if (trayAvailable) {
        tray.registerPlaceholder(tray.patchbayAction());
        QObject::connect(tray.patchbayAction(), &QAction::triggered, &application, [&tray, &patchbayModule] {
            if (patchbayModule.node()->createWindow(nullptr) == nullptr)
                tray.showMessage(QStringLiteral("接线盘"), QStringLiteral("无法创建接线盘窗口。"), 2);
        });
    }

    if (patchbayModule.node()->createWindow(nullptr) == nullptr) {
        QMessageBox::critical(nullptr, application.applicationName(),
                              QStringLiteral("无法创建接线盘窗口。"));
        return 1;
    }

    return application.exec();
}