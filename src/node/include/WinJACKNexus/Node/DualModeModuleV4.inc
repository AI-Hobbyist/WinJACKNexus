// SPDX-License-Identifier: AGPL-3.0-only
// Include once with WJN_NODE_MODULE_TYPE and WJN_NODE_MODULE_FEATURES defined.
#include <WinJACKNexus/Node/NodeApi.h>

extern "C" WINJACK_NODE_API int32_t wjn_get_node_metadata_v4(WjnNodeMetadataV4 *metadata)
{
    if (!metadata || metadata->structSize != sizeof(*metadata))
    {
        return 0;
    }
    *metadata = {sizeof(*metadata),
                 WJN_NODE_METADATA_VERSION,
                 WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE,
                 WJN_NODE_MODULE_FEATURES,
                 {0, 0}};
    return 1;
}

extern "C" WINJACK_NODE_API WinJACKNexus::Node::INode *wjn_create_node_v4(uint32_t mode)
{
    if (mode != WJN_NODE_MODE_JACK && mode != WJN_NODE_MODE_VIRT)
    {
        return nullptr;
    }
    try
    {
        return new WJN_NODE_MODULE_TYPE(mode);
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" WINJACK_NODE_API const WjnNodeVirtualApiV1 *wjn_get_node_virtual_api_v1(void)
{
    using Node = WJN_NODE_MODULE_TYPE;
    static const WjnNodeVirtualApiV1 api{
        sizeof(api),
        WJN_NODE_VIRTUAL_API_VERSION,
        [](void *instance) -> uint32_t {
            return instance ? static_cast<Node *>(instance)->virtualPortCount() : 0;
        },
        [](void *instance, uint32_t index, WjnNodePortV1 *description) -> int32_t {
            return instance && static_cast<Node *>(instance)->virtualPort(index, description);
        },
        [](void *instance, const WjnNodePrepareV1 *context) -> int32_t {
            return instance && static_cast<Node *>(instance)->virtualPrepare(context);
        },
        [](void *instance, WjnNodeProcessV1 *block) {
            if (instance)
            {
                static_cast<Node *>(instance)->virtualProcess(block);
            }
        },
        [](void *instance) {
            if (instance)
            {
                static_cast<Node *>(instance)->virtualReset();
            }
        },
        [](void *instance) {
            if (instance)
            {
                static_cast<Node *>(instance)->virtualStop();
            }
        }};
    return &api;
}
