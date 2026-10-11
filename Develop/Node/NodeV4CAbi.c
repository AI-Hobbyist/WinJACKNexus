/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <stddef.h>

_Static_assert(sizeof(WjnNodeMetadataV4) == 24, "metadata is six uint32 fields");
_Static_assert(offsetof(WjnNodeMetadataV4, capabilities) == 8, "capability offset");
_Static_assert(offsetof(WjnNodeMetadataV4, features) == 12, "GUI feature offset");
_Static_assert(WJN_NODE_HAS_GUI == 1, "frozen GUI feature bit");
_Static_assert(_Generic(&wjn_get_node_metadata_v4, int32_t (*)(WjnNodeMetadataV4 *): 1, default: 0),
               "creation-free metadata C signature");
_Static_assert(_Generic(&wjn_create_node_v4, WjnNode *(*)(uint32_t): 1, default: 0),
               "explicit-mode factory C signature");
_Static_assert(sizeof(WjnNodeVirtualApiV1) == 8 + 6 * sizeof(void *), "VIRT table layout");
_Static_assert(WJN_NODE_JACK_NODE == 1 && WJN_NODE_VIRT_NODE == 2, "frozen capability bits");
int wjn_node_v4_c_abi(void)
{
    return WJN_NODE_METADATA_VERSION == 4;
}
