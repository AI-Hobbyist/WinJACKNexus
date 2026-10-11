# SPDX-License-Identifier: AGPL-3.0-only
# One node type per DLL. No aggregate target is supported.
set(node_ids wasapi_in wasapi_out system_midi_in system_midi_out)
set(node_index 0)
foreach(node_id IN LISTS node_ids)
    set(target "mod_${node_id}")
    add_library(${target} SHARED src/Library.cpp)
    target_link_libraries(${target} PRIVATE
        WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node)
    wjn_configure_shared_target(${target} WJN_ADAPTER_BUILD)
    target_compile_definitions(${target} PRIVATE WJN_NODE_IMPLEMENTATION
        WJN_ADAPTER_NODE_INDEX=${node_index} WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/${node_id}"
        LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/${node_id}")
    foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter/${node_id}"
            LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter/${node_id}")
    endforeach()
    math(EXPR node_index "${node_index} + 1")
endforeach()
