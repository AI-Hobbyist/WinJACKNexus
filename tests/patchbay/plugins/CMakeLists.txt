# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_patchbay_plugins_tests
    PatchbayPluginsTests.cpp
)

target_link_libraries(wjn_patchbay_plugins_tests PRIVATE WinJACKNexus::PatchbayPlugins)
target_compile_features(wjn_patchbay_plugins_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_patchbay_plugins_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(wjn_patchbay_plugins_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME patchbay_plugins_unit_tests COMMAND wjn_patchbay_plugins_tests)