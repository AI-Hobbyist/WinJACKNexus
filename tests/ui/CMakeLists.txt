# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_ui_tests
    UITests.cpp
)

target_link_libraries(wjn_ui_tests PRIVATE WinJACKNexus::UI
    mod_gate mod_parametric_eq mod_compressor mod_deesser)
target_compile_features(wjn_ui_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_ui_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(
    wjn_ui_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME ui_unit_tests COMMAND wjn_ui_tests)
set_tests_properties(ui_unit_tests PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
set_property(TEST ui_unit_tests PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:mod_gate>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:mod_parametric_eq>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:mod_compressor>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:mod_deesser>"
    "PATH=path_list_prepend:${WJN_OUTPUT_DIR}")
