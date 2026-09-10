# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_meter_ui_tests
    MeterUiModelTests.cpp
)

target_link_libraries(wjn_meter_ui_tests PRIVATE WinJACKNexus::MeterbridgeUiModel)
target_compile_features(wjn_meter_ui_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_meter_ui_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(
    wjn_meter_ui_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME meter_ui_model_tests COMMAND wjn_meter_ui_tests)

add_executable(wjn_meter_ui3_tests
    MeterbridgeWindowTests.cpp
)

target_link_libraries(wjn_meter_ui3_tests PRIVATE WinJACKNexus::MeterbridgeModUi Qt6::Widgets)
target_compile_features(wjn_meter_ui3_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_meter_ui3_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(
    wjn_meter_ui3_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME meter_ui3_tests COMMAND wjn_meter_ui3_tests)