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
#include <WinJACKNexus/Node/NodeApi.h>
#include "../I18nExample.hpp"

#include <cstdint>

namespace
{

class ExampleNode final : public WinJACKNexus::Node::INode
{
  public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"Example Node", "Tools", "1.0.0", "WinJACKNexus",
                "Minimal Node API implementation for integration validation."};
    }

    bool init() noexcept override
    {
        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }

    void destroyWindow(HWND) noexcept override {}

  private:
    bool initialized_ = false;
};

}

const char *wjn_get_node_api_version(void)
{
    return "4";
}

uint32_t wjn_get_node_i18n_api_version(void)
{
    return WJN_NODE_I18N_API_VERSION;
}
const char *wjn_get_node_i18n_descriptor_json(void)
{
    return R"json({"descriptorVersion":1,"i18nApiVersion":1,
        "moduleId":"example-node","domains":["example.node"],"locatePath":"locate","catalogVersion":1,
        "displayNameKey":{"domain":"example.node","key":"name"}})json";
}

std::uint32_t wjn_get_node_theme_api_version(void)
{
    return WJN_NODE_THEME_API_VERSION;
}

const char *wjn_get_node_theme_namespace_json(void)
{
    static constexpr char descriptor[] = R"json({
        "descriptorVersion": 1,
        "themeApiVersion": 1,
        "moduleId": "module-winjacknexus-example-node",
        "namespace": "--wjn-theme-module-winjacknexus-example-node-",
        "displayName": "Example Node",
        "tokens": [{
            "name": "--wjn-theme-module-winjacknexus-example-node-node-card-background",
            "type": "color",
            "default": "var(--wjn-theme-patchbay-node-card-background)",
            "description": "Host-drawn node card background for this example node type"
        }]
    })json";
    return descriptor;
}

const char *wjn_get_node_name(void)
{
    return "Example Node";
}

const char *wjn_get_node_category(void)
{
    return "Tools";
}

WinJACKNexus::Node::INode *wjn_create_node(void)
{
    return new ExampleNode{};
}

void wjn_destroy_node(WinJACKNexus::Node::INode *node)
{
    delete node;
}

void *wjn_create_settings_view(WinJACKNexus::Node::INode *, void *)
{
    return nullptr;
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode *, void *) {}

#include <WinJACKNexus/Node/JackOnlyModuleV4.inc>
