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

#include <WinJACKNexus/Patchbay/Plugins/InternalLoader.hpp>

namespace WinJACKNexus::Patchbay::Plugins
{

std::vector<PluginDescriptor> Loader::availablePlugins()
{
    return InternalLoader::descriptors();
}

std::unique_ptr<IPluginInstance> Loader::create(const std::string &identifier)
{
    return InternalLoader::create(identifier);
}

}