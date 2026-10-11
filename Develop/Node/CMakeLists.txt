# SPDX-License-Identifier: AGPL-3.0-only
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(wjn_single_node_tests SingleNodeTests.cpp SingleNodeCAbi.c)
set_property(TARGET wjn_single_node_tests PROPERTY C_STANDARD 11)
target_link_libraries(wjn_single_node_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Node Qt6::Test)
target_compile_definitions(wjn_single_node_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_single_node_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_dependencies(wjn_single_node_tests
    mod_wasapi_in mod_wasapi_out mod_system_midi_in mod_system_midi_out
    mod_meter_mono mod_meter_stereo mod_meter_5_1 mod_meter_7_1 mod_meter_custom)

set(wjn_v4_fixture_targets)
foreach(kind IN ITEMS jack virt dual zero unknown old missing incomplete)
    set(target "wjn_v4_fixture_${kind}")
    add_library(${target} SHARED NodeV4Fixture.cpp)
    target_link_libraries(${target} PRIVATE WinJACKNexus::Node)
    target_compile_definitions(${target} PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
    if(kind STREQUAL "jack")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_CAPABILITIES=1)
    elseif(kind STREQUAL "virt")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_CAPABILITIES=2)
    elseif(kind STREQUAL "zero")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_CAPABILITIES=0)
    elseif(kind STREQUAL "unknown")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_CAPABILITIES=4)
    elseif(kind STREQUAL "old")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_OLD)
    elseif(kind STREQUAL "missing")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_MISSING_METADATA)
    elseif(kind STREQUAL "incomplete")
        target_compile_definitions(${target} PRIVATE WJN_FIXTURE_INCOMPLETE)
    endif()
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}/tests/node-v4")
    list(APPEND wjn_v4_fixture_targets ${target})
endforeach()
add_executable(wjn_node_v4_tests NodeV4Tests.cpp NodeV4CAbi.c)
set_property(TARGET wjn_node_v4_tests PROPERTY C_STANDARD 11)
target_link_libraries(wjn_node_v4_tests PRIVATE WinJACKNexus::Node)
target_compile_definitions(wjn_node_v4_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_node_v4_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_dependencies(wjn_node_v4_tests ${wjn_v4_fixture_targets} wjn_example_node)

add_library(wjn_subgraph_jack_fixture SHARED SubGraphJackFixture.cpp SubGraphJackFixture.def)
target_include_directories(wjn_subgraph_jack_fixture PRIVATE ${JACK2_INCLUDE_DIR})
target_compile_definitions(wjn_subgraph_jack_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_subgraph_jack_fixture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}/tests/node-v4")

add_executable(wjn_subgraph_tests SubGraphTests.cpp)
target_include_directories(wjn_subgraph_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/patchbay/mod/include ${PROJECT_SOURCE_DIR}/src/native_fx/include)
target_compile_definitions(wjn_subgraph_tests PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(wjn_subgraph_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(wjn_subgraph_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Audio
    WinJACKNexus::Node WinJACKNexus::Common mod_patchbay patchbay_core Qt6::Widgets Qt6::Test Dbghelp)
set_target_properties(wjn_subgraph_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_dependencies(wjn_subgraph_tests wjn_subgraph_jack_fixture ${wjn_v4_fixture_targets} WinJACKNexus)
