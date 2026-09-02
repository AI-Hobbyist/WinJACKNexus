#pragma once

#include <WinJACKNexus/Node/NodeInfo.hpp>

#include <Windows.h>

namespace WinJACKNexus::Node {

enum class NodeLifecycle {
    init,
    shutdown
};

class WINJACK_NODE_API INode {
public:
    virtual ~INode() = default;

    virtual NodeInfo info() const noexcept = 0;
    virtual bool init() noexcept = 0;
    virtual void shutdown() noexcept = 0;
    virtual HWND createWindow(HWND parent) noexcept = 0;
    virtual void destroyWindow(HWND window) noexcept = 0;
};

}