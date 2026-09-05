# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_vst_scanner_tests
    ScannerTests.cpp
)

if(WJN_ENABLE_VST2)
    add_library(wjn_vst_scanner_vst2_shell_fixture SHARED
        Vst2ShellFixture.cpp
    )
    add_library(wjn_vst_scanner_vst2_invalid_fixture SHARED
        Vst2InvalidFixture.cpp
    )
    add_library(wjn_vst_scanner_vst2_synth_fixture SHARED
        Vst2SynthFixture.cpp
    )
    target_include_directories(wjn_vst_scanner_vst2_shell_fixture
        PRIVATE
            "${PROJECT_SOURCE_DIR}/third_party/vst2sdk"
    )
    target_include_directories(wjn_vst_scanner_vst2_invalid_fixture
        PRIVATE
            "${PROJECT_SOURCE_DIR}/third_party/vst2sdk"
    )
    target_include_directories(wjn_vst_scanner_vst2_synth_fixture
        PRIVATE
            "${PROJECT_SOURCE_DIR}/third_party/vst2sdk"
    )
    target_compile_features(wjn_vst_scanner_vst2_shell_fixture PRIVATE cxx_std_20)
    target_compile_features(wjn_vst_scanner_vst2_invalid_fixture PRIVATE cxx_std_20)
    target_compile_features(wjn_vst_scanner_vst2_synth_fixture PRIVATE cxx_std_20)
    set_target_properties(
        wjn_vst_scanner_vst2_shell_fixture
        PROPERTIES
            OUTPUT_NAME "Vst2 Shell Fixture"
            RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
    )
    set_target_properties(
        wjn_vst_scanner_vst2_invalid_fixture
        PROPERTIES
            OUTPUT_NAME "Vst2 Invalid Fixture"
            RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
    )
    set_target_properties(
        wjn_vst_scanner_vst2_synth_fixture
        PROPERTIES
            OUTPUT_NAME "Vst2 Synth Fixture"
            RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
    )
    add_dependencies(wjn_vst_scanner_tests wjn_vst_scanner_vst2_shell_fixture)
    add_dependencies(wjn_vst_scanner_tests wjn_vst_scanner_vst2_invalid_fixture)
    add_dependencies(wjn_vst_scanner_tests wjn_vst_scanner_vst2_synth_fixture)
endif()

target_link_libraries(wjn_vst_scanner_tests PRIVATE wjn_vst_scanner_core Qt6::Core)
add_dependencies(wjn_vst_scanner_tests vst_scanner_probe64)
target_compile_features(wjn_vst_scanner_tests PRIVATE cxx_std_20)
target_compile_definitions(
    wjn_vst_scanner_tests
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WJN_ENABLE_VST2=$<BOOL:${WJN_ENABLE_VST2}>
)

set_target_properties(
    wjn_vst_scanner_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME vst_scanner_unit_tests COMMAND wjn_vst_scanner_tests)
