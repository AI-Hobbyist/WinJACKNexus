// SPDX-License-Identifier: AGPL-3.0-only
#include "Node.hpp"
#include <WinJACKNexus/Common/I18nApi.h>

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
    return "LAN & Remote Audio";
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

extern "C" WINJACK_NODE_API int wjn_show_node_gui(INode *node)
{
    return node && node->createWindow(nullptr) ? 1 : 0;
}
extern "C" WINJACK_NODE_API uint32_t wjn_get_node_i18n_api_version()
{
    return 2;
}
extern "C" WINJACK_NODE_API const char *wjn_get_node_i18n_descriptor_json()
{
    return R"({"descriptorVersion":1,"i18nApiVersion":1,"moduleId":"winjacknexus-broadcast","domains":["broadcast"],"locatePath":"locate","catalogVersion":1,"displayNameKey":{"domain":"broadcast","key":"name"}})";
}
namespace
{
const WjnI18nHostV1 *languageHost = nullptr;
}
extern "C" WINJACK_NODE_API int32_t wjn_i18n_attach_host(const WjnI18nHostV1 *host)
{
    if (!host || host->structSize < sizeof(*host) || host->apiVersion != 1 || !host->text ||
        (languageHost && languageHost != host))
        return WJN_I18N_INVALID_ARGUMENT;
    languageHost = host;
    return WJN_I18N_OK;
}
extern "C" WINJACK_NODE_API void wjn_i18n_language_changed(const char *, uint64_t)
{
}
extern "C" WINJACK_NODE_API void wjn_i18n_detach_host()
{
    languageHost = nullptr;
}

#define WJN_NODE_MODULE_FEATURES WJN_NODE_HAS_GUI
#include <WinJACKNexus/Node/JackOnlyModuleV4.inc>
