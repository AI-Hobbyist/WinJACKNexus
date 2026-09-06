# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_vst_host_tests
    VstHostTests.cpp
)

target_include_directories(wjn_vst_host_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/node/include")
target_compile_features(wjn_vst_host_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_vst_host_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wjn_vst_host_tests PRIVATE WinJACKNexus::Node)
add_dependencies(wjn_vst_host_tests mod_vst_host)

set_target_properties(wjn_vst_host_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME vst_host_module_contract COMMAND wjn_vst_host_tests)

add_executable(wjn_chain_host_tests
    ChainHostTests.cpp
)

target_include_directories(wjn_chain_host_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/node/include")
target_compile_features(wjn_chain_host_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_chain_host_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wjn_chain_host_tests PRIVATE WinJACKNexus::Node)
add_dependencies(wjn_chain_host_tests mod_chain_host)

set_target_properties(wjn_chain_host_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME chain_host_module_contract COMMAND wjn_chain_host_tests)