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
    using PrepareNodeForCanvas = int (*)(WinJACKNexus::Node::INode*);
    using ShowNodeGui = int (*)(WinJACKNexus::Node::INode*);
    using GetNodeClientId = const char* (*)(WinJACKNexus::Node::INode*);

    const auto getApiVersion = loadFunction<GetApiVersion>(module, "wjn_get_node_api_version");
    const auto getNodeCount = loadFunction<GetNodeCount>(module, "wjn_get_node_count");
    const auto getNodeName = loadFunction<GetNodeName>(module, "wjn_get_node_name");
    const auto createNode = loadFunction<CreateNode>(module, "wjn_create_node");
    const auto destroyNode = loadFunction<DestroyNode>(module, "wjn_destroy_node");
    const auto getSettings = loadFunction<GetSettings>(module, "wjn_get_node_settings_json");
    const auto setSettings = loadFunction<SetSettings>(module, "wjn_set_node_settings_json");
    const auto prepareNodeForCanvas = loadFunction<PrepareNodeForCanvas>(module, "wjn_prepare_node_for_canvas");
    const auto showNodeGui = loadFunction<ShowNodeGui>(module, "wjn_show_node_gui");
    const auto getNodeClientId = loadFunction<GetNodeClientId>(module, "wjn_get_node_client_id");
    expect(getApiVersion != nullptr && getNodeCount != nullptr && getNodeName != nullptr && createNode != nullptr
               && destroyNode != nullptr && getSettings != nullptr && setSettings != nullptr && prepareNodeForCanvas != nullptr && showNodeGui != nullptr && getNodeClientId != nullptr,
           "mod_vst_host.dll is missing a required Node API export");
    expect(std::string(getApiVersion()) == "2", "Unexpected VST node API version");
    expect(getNodeCount() == 1 && std::string(getNodeName(0)) == "VST Plugin", "Unexpected VST node definition");

    auto* node = createNode(0);
    expect(node != nullptr, "Unable to create VST node");
    expect(setSettings(node, "{\"path\":\"\",\"clientName\":\"Test VST\",\"bits\":\"32\",\"portDisplayMode\":3}") == 1,
           "Unable to restore VST node settings");
        expect(setSettings(node, "{\"path\":\"missing.vst3\",\"clientName\":\"Test VST\",\"bits\":\"32\",\"portDisplayMode\":3}") == 1,
            "Unable to configure a VST node before adding it to the canvas");
        expect(prepareNodeForCanvas(node) == 1, "A configured VST node should be accepted for canvas insertion");
        expect(!node->init(), "A missing configured plugin should prevent VST node initialization");
        expect(setSettings(node, "{\"path\":\"\",\"clientName\":\"Test VST\",\"bits\":\"32\",\"portDisplayMode\":3}") == 1,
            "Unable to restore empty VST node settings");
    const std::string settings(getSettings(node));
    expect(settings.find("Test VST") != std::string::npos && settings.find("\"bits\":\"32\"") != std::string::npos
               && settings.find("\"portDisplayMode\":3") != std::string::npos
               && settings.find("\"instanceId\":") != std::string::npos,
           "VST node settings round trip failed");
    expect(showNodeGui(node) == 0, "An unstarted VST node must reject opening an editor");
    node->shutdown();
    destroyNode(node);
    FreeLibrary(module);
    return 0;
}