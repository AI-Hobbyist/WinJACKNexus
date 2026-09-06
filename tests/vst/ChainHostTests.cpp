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

#include <cstdint>
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
    const auto module = LoadLibraryW(L"nodes\\vst\\mod_chain_host.dll");
    expect(module != nullptr, "Unable to load mod_chain_host.dll");

    using GetApiVersion = const char* (*)();
    using GetNodeCount = std::uint32_t (*)();
    using GetNodeName = const char* (*)(std::uint32_t);
    using GetNodeCategory = const char* (*)(std::uint32_t);
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
    const auto getNodeCategory = loadFunction<GetNodeCategory>(module, "wjn_get_node_category");
    const auto createNode = loadFunction<CreateNode>(module, "wjn_create_node");
    const auto destroyNode = loadFunction<DestroyNode>(module, "wjn_destroy_node");
    const auto getSettings = loadFunction<GetSettings>(module, "wjn_get_node_settings_json");
    const auto setSettings = loadFunction<SetSettings>(module, "wjn_set_node_settings_json");
    const auto prepareNodeForCanvas = loadFunction<PrepareNodeForCanvas>(module, "wjn_prepare_node_for_canvas");
    const auto showNodeGui = loadFunction<ShowNodeGui>(module, "wjn_show_node_gui");
    const auto getNodeClientId = loadFunction<GetNodeClientId>(module, "wjn_get_node_client_id");
    expect(getApiVersion != nullptr && getNodeCount != nullptr && getNodeName != nullptr
               && getNodeCategory != nullptr && createNode != nullptr && destroyNode != nullptr
               && getSettings != nullptr && setSettings != nullptr && prepareNodeForCanvas != nullptr
               && showNodeGui != nullptr && getNodeClientId != nullptr,
           "mod_chain_host.dll is missing a required Node API export");
    expect(std::string(getApiVersion()) == "2", "Unexpected VST chain node API version");
    expect(getNodeCount() == 1 && std::string(getNodeName(0)) == "VST Chain"
               && std::string(getNodeCategory(0)) == "VST",
           "Unexpected VST chain node definition");
    expect(createNode(1) == nullptr, "VST chain module accepts an out-of-range node index");

    auto* node = createNode(0);
    expect(node != nullptr, "Unable to create VST chain node");
    expect(std::string(node->info().name) == "VST Chain" && std::string(node->info().category) == "VST",
           "VST chain node metadata is incorrect");
    expect(setSettings(node,
                       R"({"version":1,"clientName":"Test Chain","ports":[{"name":"chain_in","alias":"Input","kind":"audio","direction":"input"},{"name":"chain_out","alias":"Output","kind":"audio","direction":"output"},{"name":"chain_midi_in","alias":"MIDI Input","kind":"midi","direction":"input"},{"name":"chain_midi_out","alias":"MIDI Output","kind":"midi","direction":"output"}],"slots":[{"settings":{},"x":360,"y":100,"bypassed":false}]})")
               == 1,
           "Unable to restore VST chain node settings");
    expect(prepareNodeForCanvas(node) == 1, "A configured VST chain should be accepted for canvas insertion");

    const std::string settings(getSettings(node));
    expect(settings.find("Test Chain") != std::string::npos
               && settings.find("chain_in") != std::string::npos
               && settings.find("chain_midi_in") != std::string::npos
               && settings.find("\"slots\":[") != std::string::npos,
           "VST chain node settings round trip failed");

    const auto initialized = node->init();
    if (initialized)
        node->shutdown();
    expect(showNodeGui(node) == 0, "A headless VST chain node must reject opening an editor");
    expect(getNodeClientId(node) != nullptr, "VST chain node client id export returned null");
    node->shutdown();
    destroyNode(node);
    FreeLibrary(module);
    return 0;
}