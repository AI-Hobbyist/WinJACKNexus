/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>

_Static_assert(_Generic(&wjn_create_node, WjnNode *(*)(void): 1, default: 0),
               "Node factory has no type index");
_Static_assert(_Generic(&wjn_get_node_name, const char *(*)(void): 1, default: 0),
               "Node metadata has no type index");
_Static_assert(_Generic(&wjn_get_node_theme_namespace_json, const char *(*)(void): 1, default: 0),
               "Node theme descriptor has no type index");
_Static_assert(_Generic(&wjn_get_node_i18n_descriptor_json, const char *(*)(void): 1, default: 0),
               "Node language descriptor has no type index");

int wjn_single_node_c_abi(void)
{
    return WJN_NODE_THEME_API_VERSION == 2 && WJN_NODE_I18N_API_VERSION == 2;
}
