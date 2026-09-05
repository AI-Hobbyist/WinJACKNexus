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
#include <WinJACKNexus/Patchbay/Plugins/Loader.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

using namespace WinJACKNexus::Patchbay::Plugins;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool testInternalGain()
{
    auto gain = Loader::create("internal:gain");
    bool passed = true;
    passed &= check(gain != nullptr && gain->initialize(48000.0, 256), "internal gain instance initializes");
    passed &= check(gain->setParameter("gain", 2.0F), "internal gain parameter updates");
    const std::array<float, 3> input { 0.25F, -0.5F, 0.75F };
    std::array<float, 3> output {};
    gain->process({ input, output });
    passed &= check(output == std::array<float, 3> { 0.5F, -1.0F, 1.5F }, "internal gain processes audio");
    return passed;
}

bool testInternalMeter()
{
    auto meter = Loader::create("internal:meter");
    const std::array<float, 3> input { 0.25F, -0.75F, 0.5F };
    std::array<float, 3> output {};
    meter->initialize(48000.0, 256);
    meter->process({ input, output });
    const auto parameters = meter->parameters();
    return check(output == input, "internal meter passes audio through")
        && check(parameters.size() == 1 && parameters.front().id == "peak" && parameters.front().value == 0.75F,
                 "internal meter exposes peak parameter");
}

bool testAvailability()
{
    const auto plugins = Loader::availablePlugins();
    return check(plugins.size() == 3, "loader exposes only internal plugins")
        && check(std::all_of(plugins.cbegin(), plugins.cend(), [](const PluginDescriptor& plugin) {
            return plugin.kind == PluginKind::internal && plugin.available;
        }), "all exposed plugins are available internal plugins");
}

}

int main()
{
    const bool passed = testInternalGain() && testInternalMeter() && testAvailability();
    if (passed)
        std::puts("Patchbay plugin tests passed");
    return passed ? 0 : 1;
}