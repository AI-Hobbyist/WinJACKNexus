/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Node/NodeApi.h>

#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

template <typename Function>
Function loadFunction(HMODULE module, const char* name)
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}

int main()
{
    const auto module = LoadLibraryW(L"nodes\\vst\\mod_vst_host.dll");
    expect(module != nullptr, "Unable to load mod_vst_host.dll");

    using GetApiVersion = const char* (*)();
    using GetNodeCount = std::uint32_t (*)();
    using GetNodeName = const char* (*)(std::uint32_t);
    using CreateNode = WinJACKNexus::Node::INode* (*)(std::uint32_t);
    using DestroyNode = void (*)(WinJACKNexus::Node::INode*);
    using GetSettings = const char* (*)(WinJACKNexus::Node::INode*);
    using SetSettings = int (*)(WinJACKNexus::Node::INode*, const char*);

    const auto getApiVersion = loadFunction<GetApiVersion>(module, "wjn_get_node_api_version");
    const auto getNodeCount = loadFunction<GetNodeCount>(module, "wjn_get_node_count");
    const auto getNodeName = loadFunction<GetNodeName>(module, "wjn_get_node_name");
    const auto createNode = loadFunction<CreateNode>(module, "wjn_create_node");
    const auto destroyNode = loadFunction<DestroyNode>(module, "wjn_destroy_node");
    const auto getSettings = loadFunction<GetSettings>(module, "wjn_get_node_settings_json");
    const auto setSettings = loadFunction<SetSettings>(module, "wjn_set_node_settings_json");
    expect(getApiVersion != nullptr && getNodeCount != nullptr && getNodeName != nullptr && createNode != nullptr
               && destroyNode != nullptr && getSettings != nullptr && setSettings != nullptr,
           "mod_vst_host.dll is missing a required Node API export");
    expect(std::string(getApiVersion()) == "2", "Unexpected VST node API version");
    expect(getNodeCount() == 1 && std::string(getNodeName(0)) == "VST Plugin", "Unexpected VST node definition");

    auto* node = createNode(0);
    expect(node != nullptr, "Unable to create VST node");
    expect(node->init(), "An unconfigured VST node should initialize without starting a process");
    expect(setSettings(node, "{\"path\":\"\",\"clientName\":\"Test VST\",\"bits\":\"32\"}") == 1,
           "Unable to restore VST node settings");
    const std::string settings(getSettings(node));
    expect(settings.find("Test VST") != std::string::npos && settings.find("\"bits\":\"32\"") != std::string::npos,
           "VST node settings round trip failed");
    node->shutdown();
    destroyNode(node);
    FreeLibrary(module);
    return 0;
}