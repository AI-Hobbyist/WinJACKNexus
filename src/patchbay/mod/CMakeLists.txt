# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(
    mod_patchbay SHARED
    src/Library.cpp src/PatchbayCanvas.cpp include/mod_patchbay/Export.hpp
    include/mod_patchbay/PatchbayCanvas.hpp include/mod_patchbay/LocalizedDialogs.hpp
    resources/patchbay.qrc)

target_include_directories(mod_patchbay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                                ${PROJECT_SOURCE_DIR}/src/native_fx/include)

target_link_libraries(mod_patchbay PRIVATE WinJACKNexus::Common WinJACKNexus::UI WinJACKNexus::Node
                                           WinJACKNexus::Audio patchbay_core signal_switch signal_switch_state)

wjn_configure_shared_target(mod_patchbay WJN_PATCHBAY_MOD_BUILD)
target_compile_definitions(mod_patchbay PRIVATE WJN_NODE_IMPLEMENTATION)
target_compile_options(mod_patchbay PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(mod_patchbay PROPERTIES AUTOMOC ON AUTORCC ON)
