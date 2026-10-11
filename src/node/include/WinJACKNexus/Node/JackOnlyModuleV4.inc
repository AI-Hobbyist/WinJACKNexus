// SPDX-License-Identifier: AGPL-3.0-only
// Include once in a JACK-only module translation unit. Keep the historical
// factory for direct SDK callers; the v4 host uses the explicit-mode factory.
#include <WinJACKNexus/Node/NodeApi.h>

#ifndef WJN_NODE_MODULE_FEATURES
#define WJN_NODE_MODULE_FEATURES 0u
#endif

extern "C" WINJACK_NODE_API int32_t wjn_get_node_metadata_v4(WjnNodeMetadataV4 *metadata)
{
    if (!metadata || metadata->structSize != sizeof(WjnNodeMetadataV4))
    {
        return 0;
    }
    *metadata = {sizeof(WjnNodeMetadataV4),
                 WJN_NODE_METADATA_VERSION,
                 WJN_NODE_JACK_NODE,
                 WJN_NODE_MODULE_FEATURES,
                 {0, 0}};
    return 1;
}

extern "C" WINJACK_NODE_API WinJACKNexus::Node::INode *wjn_create_node_v4(uint32_t mode)
{
    if (mode != WJN_NODE_MODE_JACK)
    {
        return nullptr;
    }
    try
    {
        return wjn_create_node();
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" WINJACK_NODE_API const WjnNodeVirtualApiV1 *wjn_get_node_virtual_api_v1(void)
{
    return nullptr;
}
