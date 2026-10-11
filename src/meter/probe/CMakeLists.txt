# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_meterbridge_probe SHARED Library.cpp)

target_link_libraries(
    mod_meterbridge_probe PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                                  WinJACKNexus::UI Qt6::Core Qt6::Widgets)

wjn_configure_shared_target(mod_meterbridge_probe WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meterbridge_probe PRIVATE WJN_NODE_IMPLEMENTATION
                                                         WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(
    mod_meterbridge_probe
    PROPERTIES OUTPUT_NAME "mod_meterbridge_probe"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_meterbridge_probe
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter")
endforeach()

# Independent node DLLs; the legacy target is retired after split validation.
add_library(mod_meter_mono SHARED Library.cpp)

target_link_libraries(
    mod_meter_mono PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                                  WinJACKNexus::UI Qt6::Core Qt6::Widgets)

wjn_configure_shared_target(mod_meter_mono WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meter_mono PRIVATE WJN_NODE_IMPLEMENTATION WJN_METER_PROBE_NODE_INDEX=0
                                                         WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(
    mod_meter_mono
    PROPERTIES OUTPUT_NAME "mod_meter_mono"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_mono"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_mono")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_meter_mono
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_mono"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_mono")
endforeach()

add_library(mod_meter_stereo SHARED Library.cpp)

target_link_libraries(
    mod_meter_stereo PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                                  WinJACKNexus::UI Qt6::Core Qt6::Widgets)

wjn_configure_shared_target(mod_meter_stereo WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meter_stereo PRIVATE WJN_NODE_IMPLEMENTATION WJN_METER_PROBE_NODE_INDEX=1
                                                         WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(
    mod_meter_stereo
    PROPERTIES OUTPUT_NAME "mod_meter_stereo"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_stereo"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_stereo")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_meter_stereo
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_stereo"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_stereo")
endforeach()

add_library(mod_meter_5_1 SHARED Library.cpp)

target_link_libraries(
    mod_meter_5_1 PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                                  WinJACKNexus::UI Qt6::Core Qt6::Widgets)

wjn_configure_shared_target(mod_meter_5_1 WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meter_5_1 PRIVATE WJN_NODE_IMPLEMENTATION WJN_METER_PROBE_NODE_INDEX=2
                                                         WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(
    mod_meter_5_1
    PROPERTIES OUTPUT_NAME "mod_meter_5_1"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_5_1"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_5_1")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_meter_5_1
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_5_1"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_5_1")
endforeach()

add_library(mod_meter_7_1 SHARED Library.cpp)

target_link_libraries(
    mod_meter_7_1 PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                                  WinJACKNexus::UI Qt6::Core Qt6::Widgets)

wjn_configure_shared_target(mod_meter_7_1 WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meter_7_1 PRIVATE WJN_NODE_IMPLEMENTATION WJN_METER_PROBE_NODE_INDEX=3
                                                         WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(
    mod_meter_7_1
    PROPERTIES OUTPUT_NAME "mod_meter_7_1"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_7_1"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_7_1")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_meter_7_1
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_7_1"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_7_1")
endforeach()

add_library(mod_meter_custom SHARED Library.cpp)

target_link_libraries(
    mod_meter_custom PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                                  WinJACKNexus::UI Qt6::Core Qt6::Widgets)

wjn_configure_shared_target(mod_meter_custom WJN_METERBRIDGE_PROBE_BUILD)
target_compile_definitions(mod_meter_custom PRIVATE WJN_NODE_IMPLEMENTATION WJN_METER_PROBE_NODE_INDEX=4
                                                         WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(
    mod_meter_custom
    PROPERTIES OUTPUT_NAME "mod_meter_custom"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_custom"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/meter_custom")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_meter_custom
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_custom"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/meter_custom")
endforeach()
