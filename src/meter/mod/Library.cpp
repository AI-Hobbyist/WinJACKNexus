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
#include <WinJACKNexus/Meter/MockMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ThemeManager.hpp>

namespace {

using WinJACKNexus::Meter::MockMeterDataProvider;

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
        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        destroyWindow(nullptr);
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        if (!initialized_)
            return nullptr;

        if (window_ == nullptr) {
            window_ = new WinJACKNexus::Meter::Mod::MeterbridgeWindow(provider_);
            window_->setAttribute(Qt::WA_DeleteOnClose, false);
            themeManager_.applyToWidget(window_);
        }
        window_->showNormal();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
    }

    void destroyWindow(HWND) noexcept override
    {
        if (window_ != nullptr) {
            delete window_;
            window_ = nullptr;
        }
    }

private:
    bool initialized_ = false;
    MockMeterDataProvider provider_;
    WinJACKNexus::Meter::Mod::MeterbridgeWindow* window_ = nullptr;
    WinJACKNexus::UI::ThemeManager themeManager_;
};

}

const char* wjn_get_node_api_version(void)
{
    return "2";
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