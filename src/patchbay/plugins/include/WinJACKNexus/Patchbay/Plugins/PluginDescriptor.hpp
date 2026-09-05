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

#include <WinJACKNexus/Patchbay/Plugins/Export.hpp>

#include <string>

namespace WinJACKNexus::Patchbay::Plugins {

enum class PluginKind : unsigned char {
    internal,
    vst2,
    vst3
};

struct WINJACK_PATCHBAY_PLUGINS_API PluginDescriptor final {
    PluginKind kind = PluginKind::internal;
    std::string identifier;
    std::string name;
    std::string author;
    std::string version;
    std::string path;
    bool available = false;
    std::string unavailableReason;
};

}