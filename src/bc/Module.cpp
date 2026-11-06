// SPDX-License-Identifier: AGPL-3.0-only
#include "Node.hpp"

using BroadcastNode = WinJACKNexus::Broadcast::BroadcastNode;
using INode = WinJACKNexus::Node::INode;

const char *wjn_get_node_api_version()
{
    return "4";
}
const char *wjn_get_node_name()
{
    return "Broadcast Sender";
}
const char *wjn_get_node_category()
{
    return "Broadcast";
}
INode *wjn_create_node()
{
    return new BroadcastNode;
}
void wjn_destroy_node(INode *node)
{
    delete node;
}
const char *wjn_get_node_settings_json(INode *node)
{
    return node ? static_cast<BroadcastNode *>(node)->settings() : nullptr;
}
int wjn_set_node_settings_json(INode *node, const char *json)
{
    try
    {
        return node && static_cast<BroadcastNode *>(node)->restore(json);
    }
    catch (...)
    {
        return 0;
    }
}
extern "C" WINJACK_NODE_API const char *wjn_get_node_client_id(INode *node)
{
    return node ? static_cast<BroadcastNode *>(node)->clientId() : nullptr;
}

#define WJN_NODE_MODULE_FEATURES 0u
#include <WinJACKNexus/Node/JackOnlyModuleV4.inc>
