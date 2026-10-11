// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <Windows.h>
#include <cstring>

namespace WinJACKNexus::Node
{
// Validate the version before calling any signature changed by Node v3.
inline bool isSingleNodeModule(HMODULE library) noexcept
{
    using Version = const char *(*)(void);
    const auto version =
        reinterpret_cast<Version>(GetProcAddress(library, "wjn_get_node_api_version"));
    const auto text = version ? version() : nullptr;
    return text && std::strcmp(text, "3") == 0 &&
           GetProcAddress(library, "wjn_get_node_count") == nullptr;
}
}
