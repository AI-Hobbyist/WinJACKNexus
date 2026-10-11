// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Node/NodeApi.h>
#include <Windows.h>
#include <cstring>

namespace WinJACKNexus::Node
{
enum class ModuleError
{
    none,
    version,
    indexed,
    metadata,
    capabilities,
    factory,
    processing
};

struct ModuleContract
{
    WjnNodeMetadataV4 metadata{};
    const WjnNodeVirtualApiV1 *processing{};
};

inline const char *moduleErrorText(ModuleError error) noexcept
{
    switch (error)
    {
    case ModuleError::none:
        return "Node v4 module accepted";
    case ModuleError::version:
        return "Node v4 required; rebuild old modules against v4";
    case ModuleError::indexed:
        return "Indexed Node modules are unsupported";
    case ModuleError::metadata:
        return "Missing or incompatible Node v4 metadata";
    case ModuleError::capabilities:
        return "Node capabilities/features contain invalid bits or nonzero reserved fields";
    case ModuleError::factory:
        return "Missing explicit-mode Node v4 factory or destroy export";
    case ModuleError::processing:
        return "Node capabilities and complete VIRT processing interface disagree";
    }
    return "Invalid Node module";
}

inline bool completeVirtualApi(const WjnNodeVirtualApiV1 *api) noexcept
{
    return api && api->structSize == sizeof(WjnNodeVirtualApiV1) &&
           api->version == WJN_NODE_VIRTUAL_API_VERSION && api->portCount && api->port &&
           api->prepare && api->process && api->reset && api->stop;
}

// Inspect only exports and metadata, never factories, init or JACK APIs.
inline ModuleError inspectNodeModule(HMODULE library, ModuleContract &result) noexcept
{
    result = {};
    const auto version = reinterpret_cast<decltype(&wjn_get_node_api_version)>(
        GetProcAddress(library, "wjn_get_node_api_version"));
    const auto text = version ? version() : nullptr;
    if (!text || std::strcmp(text, "4") != 0)
    {
        return ModuleError::version;
    }
    if (GetProcAddress(library, "wjn_get_node_count"))
    {
        return ModuleError::indexed;
    }
    const auto metadata = reinterpret_cast<decltype(&wjn_get_node_metadata_v4)>(
        GetProcAddress(library, "wjn_get_node_metadata_v4"));
    result.metadata.structSize = sizeof(WjnNodeMetadataV4);
    if (!metadata || !metadata(&result.metadata) ||
        result.metadata.structSize != sizeof(WjnNodeMetadataV4) ||
        result.metadata.version != WJN_NODE_METADATA_VERSION)
    {
        return ModuleError::metadata;
    }
    const uint32_t known = WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE;
    if (!result.metadata.capabilities || (result.metadata.capabilities & ~known) ||
        (result.metadata.features & ~WJN_NODE_HAS_GUI) || result.metadata.reserved[0] ||
        result.metadata.reserved[1])
    {
        return ModuleError::capabilities;
    }
    if (!GetProcAddress(library, "wjn_create_node_v4") ||
        !GetProcAddress(library, "wjn_destroy_node"))
    {
        return ModuleError::factory;
    }
    const auto processing = reinterpret_cast<decltype(&wjn_get_node_virtual_api_v1)>(
        GetProcAddress(library, "wjn_get_node_virtual_api_v1"));
    result.processing = processing ? processing() : nullptr;
    const bool virtualNode = (result.metadata.capabilities & WJN_NODE_VIRT_NODE) != 0;
    if ((virtualNode && !completeVirtualApi(result.processing)) ||
        (!virtualNode && result.processing))
    {
        return ModuleError::processing;
    }
    return ModuleError::none;
}

inline bool supportsMode(const ModuleContract &contract, uint32_t mode) noexcept
{
    return (mode == WJN_NODE_MODE_JACK || mode == WJN_NODE_MODE_VIRT) &&
           (contract.metadata.capabilities & mode) != 0;
}

inline bool isSingleNodeModule(HMODULE library) noexcept
{
    ModuleContract contract;
    return inspectNodeModule(library, contract) == ModuleError::none;
}

inline INode *createNodeForMode(HMODULE library, uint32_t mode) noexcept
{
    ModuleContract contract;
    if (inspectNodeModule(library, contract) != ModuleError::none || !supportsMode(contract, mode))
    {
        return nullptr;
    }
    const auto create = reinterpret_cast<decltype(&wjn_create_node_v4)>(
        GetProcAddress(library, "wjn_create_node_v4"));
    return create(mode);
}
}
