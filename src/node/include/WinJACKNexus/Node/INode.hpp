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