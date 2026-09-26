# This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only
# M0: keep the offline suite independent of a running JACK server.
add_executable(wjn_digital_mixer_tests DigitalMixerTests.cpp)
target_link_libraries(wjn_digital_mixer_tests PRIVATE WinJACKNexus::Common)
target_compile_features(wjn_digital_mixer_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_digital_mixer_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_digital_mixer_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}")

add_test(NAME digital_mixer_offline_tests COMMAND wjn_digital_mixer_tests offline)
set_tests_properties(digital_mixer_offline_tests PROPERTIES LABELS "digital_mixer;offline")

add_executable(wjn_digital_mixer_node_tests DigitalMixerNodeTests.cpp)
target_link_libraries(wjn_digital_mixer_node_tests PRIVATE WinJACKNexus::Node WinJACKNexus::Audio
    WinJACKNexus::Common WinJACKNexus::UI mod_graphic_eq Qt6::Widgets)
target_include_directories(wjn_digital_mixer_node_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/native_fx/include"
    "${PROJECT_SOURCE_DIR}/src/native_fx/effects/graphic_eq/include")
target_compile_features(wjn_digital_mixer_node_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_digital_mixer_node_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(wjn_digital_mixer_node_tests mod_digital_mixer mod_patchbay)
set_target_properties(wjn_digital_mixer_node_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}")
add_test(NAME digital_mixer_node_lifecycle_tests COMMAND wjn_digital_mixer_node_tests)
add_test(NAME digital_mixer_node_port_tests COMMAND wjn_digital_mixer_node_tests)
set_tests_properties(digital_mixer_node_port_tests PROPERTIES LABELS "digital_mixer;node_port")
set_tests_properties(digital_mixer_node_lifecycle_tests PROPERTIES LABELS "digital_mixer;node_lifecycle")
set_property(TEST digital_mixer_node_port_tests digital_mixer_node_lifecycle_tests
    PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:mod_graphic_eq>"
    "PATH=path_list_prepend:${WJN_OUTPUT_DIR}")
