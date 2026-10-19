# SPDX-License-Identifier: AGPL-3.0-only
add_library(signal_switch_state STATIC State.cpp State.hpp)
target_link_libraries(signal_switch_state PUBLIC Qt6::Gui)
target_include_directories(signal_switch_state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(signal_switch_state PRIVATE /utf-8)

if(BUILD_TESTING)
    add_executable(wjn_signal_switch_state_tests StateTests.cpp)
    target_link_libraries(wjn_signal_switch_state_tests PRIVATE signal_switch_state)
    target_compile_options(wjn_signal_switch_state_tests PRIVATE /utf-8)
    set_target_properties(wjn_signal_switch_state_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_test(NAME signal_switch_state_tests COMMAND wjn_signal_switch_state_tests)
endif()
