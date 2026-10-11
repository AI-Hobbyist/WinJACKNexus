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
#include <WinJACKNexus/Node/NodeApi.h>

const char *wjn_get_node_api_version(void)
{
    return "4";
}

const char *wjn_get_node_name(void)
{
    return "WinJACKNexus Node API";
}

const char *wjn_get_node_category(void)
{
    return "Interface";
}

WinJACKNexus::Node::INode *wjn_create_node(void)
{
    return nullptr;
}

void wjn_destroy_node(WinJACKNexus::Node::INode *) {}

void *wjn_create_settings_view(WinJACKNexus::Node::INode *, void *)
{
    return nullptr;
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode *, void *) {}

QWidget *wjn_create_settings_widget(WinJACKNexus::Node::INode *, QWidget *)
{
    return nullptr;
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode *, QWidget *) {}

const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *)
{
    return "";
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode *, const char *)
{
    return 0;
}

#include <WinJACKNexus/Node/JackOnlyModuleV4.inc>
