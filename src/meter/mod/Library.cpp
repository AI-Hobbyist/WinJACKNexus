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
#include <WinJACKNexus/Meter/LiveMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeService.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <QJsonDocument>
#include <QJsonParseError>

namespace {

WinJACKNexus::Meter::Mod::MeterbridgeService* activeService = nullptr;

class MeterbridgeNode final : public WinJACKNexus::Node::INode {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {
            "Meterbridge",
            "Meter",
            "0.1.0",
            "WinJACKNexus",
            "Top-level Meterbridge window module."
        };
    }

    bool init() noexcept override
    {
        initialized_ = service_.initialize();
        if (initialized_)
            activeService = &service_;
        return initialized_;
    }

    void shutdown() noexcept override
    {
        if (activeService == &service_)
            activeService = nullptr;
        service_.shutdown();
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        auto* window = initialized_ ? service_.showWindow() : nullptr;
        return window == nullptr ? nullptr : reinterpret_cast<HWND>(static_cast<quintptr>(window->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        service_.destroyWindow();
    }

private:
    bool initialized_ = false;
    WinJACKNexus::Meter::Mod::MeterbridgeService service_;
};

}

const char* wjn_get_node_api_version(void)
{
    return "2";
}

std::uint32_t wjn_get_node_theme_api_version(void)
{
    return WJN_NODE_THEME_API_VERSION;
}

const char* wjn_get_node_theme_namespace_json(std::uint32_t index)
{
    static constexpr char descriptor[] = R"json({
        "descriptorVersion":1,"themeApiVersion":1,
        "moduleId":"module-winjacknexus-meterbridge",
        "namespace":"--wjn-theme-module-winjacknexus-meterbridge-",
        "displayName":"Meterbridge",
        "tokens":[{
            "name":"--wjn-theme-module-winjacknexus-meterbridge-node-card-background",
            "type":"color",
            "default":"var(--wjn-theme-patchbay-node-card-background)",
            "description":"Host-drawn card background for the Meterbridge node type"
        }]
    })json";
    return index == 0 ? descriptor : nullptr;
}

std::uint32_t wjn_get_node_count(void)
{
    return 1;
}

const char* wjn_get_node_name(std::uint32_t index)
{
    return index == 0 ? "Meterbridge" : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
    return index == 0 ? "Meter" : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    return index == 0 ? new MeterbridgeNode : nullptr;
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
    delete node;
}

void* wjn_create_settings_view(WinJACKNexus::Node::INode*, void*)
{
    return nullptr;
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*)
{
}

QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode*, QWidget*)
{
    return nullptr;
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode*, QWidget*)
{
}

const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode*)
{
    return "";
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode*, const char*)
{
    return 0;
}

extern "C" WINJACK_NODE_API void wjn_meterbridge_reload_global_settings()
{
    if (activeService != nullptr)
        activeService->reloadGlobalSettings();
}

extern "C" WINJACK_NODE_API int wjn_meterbridge_show_for_instance(const char* instanceId)
{
    if (activeService == nullptr)
        return 0;
    const auto* window = activeService->showWindow(instanceId == nullptr ? QString() : QString::fromUtf8(instanceId));
    return window == nullptr ? 0 : 1;
}

extern "C" WINJACK_NODE_API const char* wjn_meterbridge_project_state_json()
{
    static QByteArray serializedState;
    if (activeService == nullptr) {
        serializedState = "{}";
        return serializedState.constData();
    }
    serializedState = QJsonDocument(activeService->projectState()).toJson(QJsonDocument::Compact);
    return serializedState.constData();
}

extern "C" WINJACK_NODE_API int wjn_meterbridge_restore_project_state_json(const char* json)
{
    if (activeService == nullptr || json == nullptr)
        return 0;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        && activeService->restoreProjectState(document.object()) ? 1 : 0;
}
