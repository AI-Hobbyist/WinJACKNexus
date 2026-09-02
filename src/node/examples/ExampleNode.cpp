#include <WinJACKNexus/Node/NodeApi.h>

namespace {

class ExampleNode final : public WinJACKNexus::Node::INode {
public:
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {
            "Example Node",
            "Tools",
            "1.0.0",
            "WinJACKNexus",
            "Minimal Node API implementation for integration validation."
        };
    }

    bool init() noexcept override
    {
        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override
    {
        initialized_ = false;
    }

    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }

    void destroyWindow(HWND) noexcept override
    {
    }

private:
    bool initialized_ = false;
};

}

const char* wjn_get_node_api_version(void)
{
    return "1";
}

const char* wjn_get_node_name(void)
{
    return "Example Node";
}

const char* wjn_get_node_category(void)
{
    return "Tools";
}

WinJACKNexus::Node::INode* wjn_create_node(void)
{
    return new ExampleNode {};
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
    delete node;
}