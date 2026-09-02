#pragma once

#include <WinJACKNexus/Node/Export.hpp>

#ifdef __cplusplus
#    include <WinJACKNexus/Node/INode.hpp>
extern "C" {
#endif

WINJACK_NODE_API const char* wjn_get_node_api_version(void);
WINJACK_NODE_API const char* wjn_get_node_name(void);
WINJACK_NODE_API const char* wjn_get_node_category(void);

#ifdef __cplusplus
WINJACK_NODE_API WinJACKNexus::Node::INode* wjn_create_node(void);
WINJACK_NODE_API void wjn_destroy_node(WinJACKNexus::Node::INode* node);
#else
typedef struct WjnNode WjnNode;
WINJACK_NODE_API WjnNode* wjn_create_node(void);
WINJACK_NODE_API void wjn_destroy_node(WjnNode* node);
#endif

#ifdef __cplusplus
}
#endif