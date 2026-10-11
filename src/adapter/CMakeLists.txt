# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_adapter SHARED src/Library.cpp)

target_link_libraries(mod_adapter PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common
                                          WinJACKNexus::Node)

wjn_configure_shared_target(mod_adapter WJN_ADAPTER_BUILD)
target_compile_definitions(mod_adapter PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mod_adapter PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(
    mod_adapter
    PROPERTIES OUTPUT_NAME "mod_adapter"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_adapter
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
                   "${WJN_OUTPUT_DIR}/nodes/adapter")
endforeach()

# Independent node DLLs; the legacy target is retired after split validation.
add_library(mod_wasapi_in SHARED src/Library.cpp)

target_link_libraries(mod_wasapi_in PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common
                                          WinJACKNexus::Node)

wjn_configure_shared_target(mod_wasapi_in WJN_ADAPTER_BUILD)
target_compile_definitions(mod_wasapi_in PRIVATE WJN_NODE_IMPLEMENTATION WJN_ADAPTER_NODE_INDEX=0 WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mod_wasapi_in PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(
    mod_wasapi_in
    PROPERTIES OUTPUT_NAME "mod_wasapi_in"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_in"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_in")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_wasapi_in
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_in"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
                   "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_in")
endforeach()

add_library(mod_wasapi_out SHARED src/Library.cpp)

target_link_libraries(mod_wasapi_out PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common
                                          WinJACKNexus::Node)

wjn_configure_shared_target(mod_wasapi_out WJN_ADAPTER_BUILD)
target_compile_definitions(mod_wasapi_out PRIVATE WJN_NODE_IMPLEMENTATION WJN_ADAPTER_NODE_INDEX=1 WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mod_wasapi_out PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(
    mod_wasapi_out
    PROPERTIES OUTPUT_NAME "mod_wasapi_out"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_out"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_out")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_wasapi_out
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_out"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
                   "${WJN_OUTPUT_DIR}/nodes/adapter/wasapi_out")
endforeach()

add_library(mod_system_midi_in SHARED src/Library.cpp)

target_link_libraries(mod_system_midi_in PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common
                                          WinJACKNexus::Node)

wjn_configure_shared_target(mod_system_midi_in WJN_ADAPTER_BUILD)
target_compile_definitions(mod_system_midi_in PRIVATE WJN_NODE_IMPLEMENTATION WJN_ADAPTER_NODE_INDEX=2 WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mod_system_midi_in PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(
    mod_system_midi_in
    PROPERTIES OUTPUT_NAME "mod_system_midi_in"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_in"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_in")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_system_midi_in
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_in"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
                   "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_in")
endforeach()

add_library(mod_system_midi_out SHARED src/Library.cpp)

target_link_libraries(mod_system_midi_out PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Common
                                          WinJACKNexus::Node)

wjn_configure_shared_target(mod_system_midi_out WJN_ADAPTER_BUILD)
target_compile_definitions(mod_system_midi_out PRIVATE WJN_NODE_IMPLEMENTATION WJN_ADAPTER_NODE_INDEX=3 WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mod_system_midi_out PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
set_target_properties(
    mod_system_midi_out
    PROPERTIES OUTPUT_NAME "mod_system_midi_out"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_out"
               LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_out")

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        mod_system_midi_out
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_out"
                   LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
                   "${WJN_OUTPUT_DIR}/nodes/adapter/system_midi_out")
endforeach()
