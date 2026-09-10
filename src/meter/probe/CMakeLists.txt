# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_meterbridge_probe SHARED
    Library.cpp
)

target_link_libraries(mod_meterbridge_probe
    PRIVATE
        WinJACKNexus::Node
)

wjn_configure_shared_target(mod_meterbridge_probe WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meterbridge_probe PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(mod_meterbridge_probe PROPERTIES
    OUTPUT_NAME "mod_meterbridge_probe"
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter"
    LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter"
)

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(mod_meterbridge_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter"
        LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter"
    )
endforeach()