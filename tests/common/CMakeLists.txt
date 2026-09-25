# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_common_tests
    CommonTests.cpp
)

target_link_libraries(wjn_common_tests PRIVATE WinJACKNexus::Common mod_compressor)
target_compile_features(wjn_common_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_common_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(
    wjn_common_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME common_unit_tests COMMAND wjn_common_tests)
set_tests_properties(common_unit_tests PROPERTIES ENVIRONMENT
    "PATH=$<TARGET_FILE_DIR:mod_compressor>;${WJN_OUTPUT_DIR}")
