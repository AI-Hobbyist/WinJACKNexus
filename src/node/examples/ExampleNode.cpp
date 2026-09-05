#include <WinJACKNexus/Node/NodeApi.h>

#include <cstdint>

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
    return "2";
}

std::uint32_t wjn_get_node_count(void)
{
    return 1;
}

const char* wjn_get_node_name(std::uint32_t index)
{
    return index == 0 ? "Example Node" : "";
}

const char* wjn_get_node_category(std::uint32_t index)
{
    return index == 0 ? "Tools" : "";
}

WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index)
{
    return index == 0 ? new ExampleNode {} : nullptr;
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
    delete node;
}

void* wjn_create_settings_view(WinJACKNexus::Node::INode*, void*)
{
    return nullptr;
}

void wjn_destroy_settings_view(WinJACKNexus::Node::INode*, void*)
{
}