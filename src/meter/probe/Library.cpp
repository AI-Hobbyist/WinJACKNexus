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

#include <array>
#include <cstdint>

namespace {

struct ProbeDefinition final {
    const char* name;
    const char* description;
};

constexpr std::array probeDefinitions {
    ProbeDefinition { "Meter Mono", "Single-channel Meterbridge probe." },
    ProbeDefinition { "Meter Stereo", "Two-channel Meterbridge probe." },
    ProbeDefinition { "Meter 5.1", "5.1-channel Meterbridge probe." },
    ProbeDefinition { "Meter 7.1", "7.1-channel Meterbridge probe." },
    ProbeDefinition { "Meter Custom", "Custom-channel Meterbridge probe." }
};

class MeterProbeNode final : public WinJACKNexus::Node::INode {
public:
    explicit MeterProbeNode(std::uint32_t index) noexcept
        : definition_(probeDefinitions[index])
    {
    }

    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {
            definition_.name,
            "Meter",
            "0.1.0",
            "WinJACKNexus",
            definition_.description
        };
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

    void destroyWindow(HWND) noexcept override
    {
    }

private:
    ProbeDefinition definition_;
    bool initialized_ = false;
};

}

const char* wjn_get_node_api_version(void)
{
    return "2";
}

std::uint32_t wjn_get_node_count(void)
{
    return static_cast<std::uint32_t>(probeDefinitions.size());
}

const char* wjn_get_node_name(std::uint32_t index)
{
    return index < probeDefinitions.size() ? probeDefinitions[index].name : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
    return index < probeDefinitions.size() ? "Meter" : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    return index < probeDefinitions.size() ? new MeterProbeNode(index) : nullptr;
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