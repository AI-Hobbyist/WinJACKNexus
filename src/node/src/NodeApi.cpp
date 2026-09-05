#include <WinJACKNexus/Node/NodeApi.h>

const char* wjn_get_node_api_version(void)
{
    return "2";
}

uint32_t wjn_get_node_count(void)
{
    return 0;
}

const char* wjn_get_node_name(uint32_t)
{
    return "WinJACKNexus Node API";
}

const char* wjn_get_node_category(uint32_t)
{
    return "Interface";
}

WinJACKNexus::Node::INode* wjn_create_node(uint32_t)
{
    return nullptr;
}

void wjn_destroy_node(WinJACKNexus::Node::INode*)
{
}

void* wjn_create_settings_view(WinJACKNexus::Node::INode*, void*)
{
    return nullptr;
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*)
{
}

QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode*, QWidget*)
{
    return nullptr;
}

void wjn_destroy_settings_widget(WinJACKNexus::Node::INode*, QWidget*)
{
}

const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode*)
{
    return "";
}

int wjn_set_node_settings_json(WinJACKNexus::Node::INode*, const char*)
{
    return 0;
}