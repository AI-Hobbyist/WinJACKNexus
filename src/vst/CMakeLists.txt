# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_vst_host SHARED
    src/Library.cpp
)

target_link_libraries(mod_vst_host
    PRIVATE
        WinJACKNexus::Node
        Qt6::Core
        Qt6::Widgets
)

wjn_configure_shared_target(mod_vst_host WJN_VST_HOST_BUILD)
target_compile_definitions(mod_vst_host PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(mod_vst_host PROPERTIES
    OUTPUT_NAME "mod_vst_host"
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/vst"
    LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/vst"
)

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(mod_vst_host PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/vst"
        LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/vst"
    )
endforeach()