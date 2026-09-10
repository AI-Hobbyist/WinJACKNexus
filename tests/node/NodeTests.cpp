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
#include <WinJACKNexus/Node/INode.hpp>

#include <Windows.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string_view>

namespace {

using GetStringFunction = const char* (*)();
using GetCountFunction = std::uint32_t (*)();
using GetIndexedStringFunction = const char* (*)(std::uint32_t index);
using CreateNodeFunction = WinJACKNexus::Node::INode* (*)(std::uint32_t index);
using DestroyNodeFunction = void (*)(WinJACKNexus::Node::INode*);
using CreateSettingsViewFunction = void* (*)(WinJACKNexus::Node::INode*, void*);
using DestroySettingsViewFunction = void (*)(WinJACKNexus::Node::INode*, void*);
using GetNodeSettingsJsonFunction = const char* (*)(WinJACKNexus::Node::INode*);
using SetNodeSettingsJsonFunction = int (*)(WinJACKNexus::Node::INode*, const char*);

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

std::filesystem::path executableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

bool testApiStub(const std::filesystem::path& directory)
{
    const auto path = directory / L"node.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "node.dll loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    const auto createSettingsView = reinterpret_cast<CreateSettingsViewFunction>(GetProcAddress(module, "wjn_create_settings_view"));
    const auto destroySettingsView = reinterpret_cast<DestroySettingsViewFunction>(GetProcAddress(module, "wjn_destroy_settings_view"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr
                        && createSettingsView != nullptr && destroySettingsView != nullptr,
                    "node.dll exports the complete Node C API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr
        && createSettingsView != nullptr && destroySettingsView != nullptr) {
        passed &= check(std::string_view(version()) == "2" && count() == 0 && std::string_view(name(0)) == "WinJACKNexus Node API"
                            && std::string_view(category(0)) == "Interface",
                        "node.dll reports stable API metadata");
        passed &= check(create(0) == nullptr, "node.dll remains an interface-only stub");
    }
    FreeLibrary(module);
    return passed;
}

bool testExampleNode(const std::filesystem::path& directory)
{
    const auto path = directory / L"wjn_example_node.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "example node loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "example node exports the complete Node C API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        passed &= check(std::string_view(version()) == "2" && count() == 1 && std::string_view(name(0)) == "Example Node"
                            && std::string_view(category(0)) == "Tools",
                        "example node reports stable metadata");
        auto* node = create(0);
        passed &= check(node != nullptr, "example node creates an INode instance");
        if (node != nullptr) {
            const auto info = node->info();
            passed &= check(std::string_view(info.name) == "Example Node" && std::string_view(info.version) == "1.0.0",
                            "example INode exposes NodeInfo metadata");
            passed &= check(node->init(), "example INode initializes");
            passed &= check(node->createWindow(nullptr) == nullptr, "example INode has no window skeleton implementation");
            node->shutdown();
            destroy(node);
        }
    }
    FreeLibrary(module);
    return passed;
}

bool testAdapterModule(const std::filesystem::path& directory)
{
    const auto path = directory / L"nodes" / L"adapter" / L"mod_adapter.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "adapter module loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    const auto createSettingsView = reinterpret_cast<CreateSettingsViewFunction>(GetProcAddress(module, "wjn_create_settings_view"));
    const auto destroySettingsView = reinterpret_cast<DestroySettingsViewFunction>(GetProcAddress(module, "wjn_destroy_settings_view"));
    const auto getNodeSettingsJson = reinterpret_cast<GetNodeSettingsJsonFunction>(GetProcAddress(module, "wjn_get_node_settings_json"));
    const auto setNodeSettingsJson = reinterpret_cast<SetNodeSettingsJsonFunction>(GetProcAddress(module, "wjn_set_node_settings_json"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && create != nullptr && destroy != nullptr
                        && createSettingsView != nullptr && destroySettingsView != nullptr
                        && getNodeSettingsJson != nullptr && setNodeSettingsJson != nullptr,
                    "adapter module exports the complete Node v2 API");
    if (version != nullptr && count != nullptr && name != nullptr && create != nullptr && destroy != nullptr
        && createSettingsView != nullptr && destroySettingsView != nullptr
        && getNodeSettingsJson != nullptr && setNodeSettingsJson != nullptr) {
        constexpr std::string_view expectedNames[] { "WASAPI In", "WASAPI Out", "System MIDI In", "System MIDI Out" };
        passed &= check(std::string_view(version()) == "2" && count() == std::size(expectedNames),
                        "adapter module reports four nodes");
        for (std::uint32_t index = 0; index < count(); ++index) {
            passed &= check(std::string_view(name(index)) == expectedNames[index], "adapter node name matches its index");
            auto* node = create(index);
            passed &= check(node != nullptr && node->init(), "adapter node skeleton initializes");
            if (node != nullptr) {
                const auto* settings = getNodeSettingsJson(node);
                passed &= check(settings != nullptr && setNodeSettingsJson(node, settings) == 1,
                                "adapter node round-trips its JSON settings");
                destroySettingsView(node, createSettingsView(node, nullptr));
                node->shutdown();
                destroy(node);
            }
        }
        passed &= check(create(count()) == nullptr, "adapter rejects an out-of-range node index");
    }
    FreeLibrary(module);
    return passed;
}

bool testMeterbridgeModule(const std::filesystem::path& directory)
{
    const auto path = directory / L"mod_meterbrigde.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "meterbridge module loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "meterbridge module exports the Node API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        passed &= check(std::string_view(version()) == "2" && count() == 1
                            && std::string_view(name(0)) == "Meterbridge"
                            && std::string_view(category(0)) == "Meter",
                        "meterbridge module reports stable metadata");
        auto* node = create(0);
        passed &= check(node != nullptr && node->init(), "meterbridge module initializes its top-level node");
        if (node != nullptr) {
            const auto info = node->info();
            passed &= check(std::string_view(info.name) == "Meterbridge" && std::string_view(info.category) == "Meter",
                            "meterbridge INode exposes stable metadata");
            node->shutdown();
            destroy(node);
        }
        passed &= check(create(count()) == nullptr, "meterbridge module rejects an out-of-range node index");
    }
    FreeLibrary(module);
    return passed;
}

bool testMeterProbeModule(const std::filesystem::path& directory)
{
    const auto path = directory / L"nodes" / L"meter" / L"mod_meterbridge_probe.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "meter probe module loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "meter probe module exports the Node API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        constexpr std::string_view expectedNames[] {
            "Meter Mono", "Meter Stereo", "Meter 5.1", "Meter 7.1", "Meter Custom"
        };
        passed &= check(std::string_view(version()) == "2" && count() == std::size(expectedNames),
                        "meter probe module reports five probe nodes");
        for (std::uint32_t index = 0; index < count(); ++index) {
            passed &= check(std::string_view(name(index)) == expectedNames[index]
                                && std::string_view(category(index)) == "Meter",
                            "meter probe metadata matches its index");
            auto* node = create(index);
            passed &= check(node != nullptr && node->init(), "meter probe node initializes");
            if (node != nullptr) {
                const auto info = node->info();
                passed &= check(std::string_view(info.name) == expectedNames[index]
                                    && std::string_view(info.category) == "Meter",
                                "meter probe INode exposes stable metadata");
                node->shutdown();
                destroy(node);
            }
        }
        passed &= check(create(count()) == nullptr, "meter probe module rejects an out-of-range node index");
    }
    FreeLibrary(module);
    return passed;
}

}

int main()
{
    const auto directory = executableDirectory();
    const auto passed = testApiStub(directory)
        && testExampleNode(directory)
        && testAdapterModule(directory)
        && testMeterbridgeModule(directory)
        && testMeterProbeModule(directory);
    if (!passed)
        return 1;
    std::puts("Node tests passed");
    return 0;
}