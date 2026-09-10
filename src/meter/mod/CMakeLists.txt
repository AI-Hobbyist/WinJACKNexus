# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(meterbridge_mod_ui STATIC
    src/MeterCard.cpp
    src/MeterbridgeWindow.cpp
    include/WinJACKNexus/Meter/Mod/MeterCard.hpp
    include/WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp
)

target_include_directories(meterbridge_mod_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(meterbridge_mod_ui
    PUBLIC
        WinJACKNexus::MeterbridgeUiModel
        WinJACKNexus::UI
        Qt6::Widgets
)

target_compile_features(meterbridge_mod_ui PUBLIC cxx_std_20)
target_compile_definitions(meterbridge_mod_ui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_library(WinJACKNexus::MeterbridgeModUi ALIAS meterbridge_mod_ui)

add_library(mod_meterbrigde SHARED
    Library.cpp
)

target_link_libraries(mod_meterbrigde
    PRIVATE
        WinJACKNexus::MeterbridgeModUi
        WinJACKNexus::Node
)

wjn_configure_shared_target(mod_meterbrigde WJN_METERBRIDGE_MOD_BUILD)
target_compile_definitions(mod_meterbrigde PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(mod_meterbrigde PROPERTIES OUTPUT_NAME "mod_meterbrigde")