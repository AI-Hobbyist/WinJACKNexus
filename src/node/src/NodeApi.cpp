#include <WinJACKNexus/Node/NodeApi.h>

const char* wjn_get_node_api_version(void)
{
    return "1";
}

const char* wjn_get_node_name(void)
{
    return "WinJACKNexus Node API";
}

const char* wjn_get_node_category(void)
{
    return "Interface";
}

WinJACKNexus::Node::INode* wjn_create_node(void)
{
    return nullptr;
}

void wjn_destroy_node(WinJACKNexus::Node::INode*)
{
}