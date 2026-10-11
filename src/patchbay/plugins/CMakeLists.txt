# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(
    patchbay_plugins SHARED
    src/Library.cpp
    src/Loader.cpp
    src/InternalLoader.cpp
    include/WinJACKNexus/Patchbay/Plugins/Export.hpp
    include/WinJACKNexus/Patchbay/Plugins/PluginDescriptor.hpp
    include/WinJACKNexus/Patchbay/Plugins/IPluginInstance.hpp
    include/WinJACKNexus/Patchbay/Plugins/Loader.hpp
    include/WinJACKNexus/Patchbay/Plugins/InternalLoader.hpp)

target_include_directories(
    patchbay_plugins PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                            $<INSTALL_INTERFACE:include>)

target_link_libraries(patchbay_plugins PRIVATE WinJACKNexus::Common WinJACKNexus::PatchbayCore)

wjn_configure_shared_target(patchbay_plugins WJN_PATCHBAY_PLUGINS_BUILD)
add_library(WinJACKNexus::PatchbayPlugins ALIAS patchbay_plugins)
