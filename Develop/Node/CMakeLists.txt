# SPDX-License-Identifier: AGPL-3.0-only
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(wjn_single_node_tests SingleNodeTests.cpp)
target_link_libraries(wjn_single_node_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Node Qt6::Test)
target_compile_definitions(wjn_single_node_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_single_node_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_dependencies(wjn_single_node_tests
    mod_wasapi_in mod_wasapi_out mod_system_midi_in mod_system_midi_out
    mod_meter_mono mod_meter_stereo mod_meter_5_1 mod_meter_7_1 mod_meter_custom)
