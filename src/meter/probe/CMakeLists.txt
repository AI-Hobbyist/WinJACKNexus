# SPDX-License-Identifier: AGPL-3.0-only
# One node type per DLL. No aggregate target is supported.
set(node_ids meter_mono meter_stereo meter_5_1 meter_7_1 meter_custom)
set(node_index 0)
foreach(node_id IN LISTS node_ids)
    set(target "mod_${node_id}")
    add_library(${target} SHARED Library.cpp)
    target_link_libraries(${target} PRIVATE
        WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node)
    wjn_configure_shared_target(${target} WJN_METERBRIDGE_PROBE_BUILD)
    target_compile_definitions(${target} PRIVATE WJN_NODE_IMPLEMENTATION
        WJN_METER_PROBE_NODE_INDEX=${node_index} WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/${node_id}"
        LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/meter/${node_id}")
    foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/${node_id}"
            LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/meter/${node_id}")
    endforeach()
    math(EXPR node_index "${node_index} + 1")
endforeach()
