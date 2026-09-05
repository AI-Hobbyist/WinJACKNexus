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
#pragma once

#include <WinJACKNexus/Node/Export.hpp>

#include <stdint.h>

#ifdef __cplusplus
class QWidget;
#    include <WinJACKNexus/Node/INode.hpp>
extern "C" {
#endif

WINJACK_NODE_API const char* wjn_get_node_api_version(void);
WINJACK_NODE_API uint32_t wjn_get_node_count(void);
WINJACK_NODE_API const char* wjn_get_node_name(uint32_t index);
WINJACK_NODE_API const char* wjn_get_node_category(uint32_t index);

#ifdef __cplusplus
WINJACK_NODE_API WinJACKNexus::Node::INode* wjn_create_node(uint32_t index);
WINJACK_NODE_API void wjn_destroy_node(WinJACKNexus::Node::INode* node);
WINJACK_NODE_API void* wjn_create_settings_view(WinJACKNexus::Node::INode* node, void* parentHwnd);
WINJACK_NODE_API void wjn_destroy_settings_view(WinJACKNexus::Node::INode* node, void* viewHwnd);
WINJACK_NODE_API QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode* node, QWidget* parent);
WINJACK_NODE_API void wjn_destroy_settings_widget(WinJACKNexus::Node::INode* node, QWidget* widget);
WINJACK_NODE_API const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node);
WINJACK_NODE_API int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node, const char* json);
#else
typedef struct WjnNode WjnNode;
WINJACK_NODE_API WjnNode* wjn_create_node(uint32_t index);
WINJACK_NODE_API void wjn_destroy_node(WjnNode* node);
WINJACK_NODE_API void* wjn_create_settings_view(WjnNode* node, void* parentHwnd);
WINJACK_NODE_API void wjn_destroy_settings_view(WjnNode* node, void* viewHwnd);
WINJACK_NODE_API void* wjn_create_settings_widget(WjnNode* node, void* parent);
WINJACK_NODE_API void wjn_destroy_settings_widget(WjnNode* node, void* widget);
WINJACK_NODE_API const char* wjn_get_node_settings_json(WjnNode* node);
WINJACK_NODE_API int wjn_set_node_settings_json(WjnNode* node, const char* json);
#endif

#ifdef __cplusplus
}
#endif