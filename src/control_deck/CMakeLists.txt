# SPDX-License-Identifier: AGPL-3.0-only
add_library(control_deck_state STATIC State.cpp State.hpp)
target_link_libraries(control_deck_state PUBLIC Qt6::Gui)
target_include_directories(control_deck_state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(control_deck_state PRIVATE /utf-8)

add_library(control_deck SHARED Service.cpp Service.hpp Editor.hpp Sound.cpp Sound.hpp Hotkeys.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/src/ControlDeckControls.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/src/ControlDeckEditor.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/src/ControlDeckLayout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/src/ControlDeckOverlay.cpp")
wjn_configure_shared_target(control_deck WJN_CONTROL_DECK_BUILD)
target_include_directories(control_deck PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/common/include")
target_link_libraries(control_deck PRIVATE control_deck_state WinJACKNexus::UI PUBLIC Qt6::Widgets)
target_compile_options(control_deck PRIVATE /utf-8)
target_compile_definitions(control_deck PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

foreach(kind mono stereo 5_1 7_1 midi sound peak gain_mono gain_stereo gain_5_1 gain_7_1)
    set(control 0)
    if(kind STREQUAL "mono")
        set(channels 1)
        set(display "Signal Switch Mono")
    elseif(kind STREQUAL "stereo")
        set(channels 2)
        set(display "Signal Switch Stereo")
    elseif(kind STREQUAL "5_1")
        set(channels 6)
        set(display "Signal Switch 5.1")
    elseif(kind STREQUAL "7_1")
        set(channels 8)
        set(display "Signal Switch 7.1")
    elseif(kind STREQUAL "midi")
        set(channels 0)
        set(display "Signal Switch MIDI")
    elseif(kind STREQUAL "sound")
        set(channels 2)
        set(control 1)
        set(display "WJN Control Deck Sound")
    elseif(kind STREQUAL "peak")
        set(channels 1)
        set(control 2)
        set(display "WJN Control Deck Peak Meter")
    else()
        set(control 3)
        if(kind STREQUAL "gain_mono")
            set(channels 1)
            set(display "WJN Control Deck Gain Mono")
        elseif(kind STREQUAL "gain_stereo")
            set(channels 2)
            set(display "WJN Control Deck Gain Stereo")
        elseif(kind STREQUAL "gain_5_1")
            set(channels 6)
            set(display "WJN Control Deck Gain 5.1")
        else()
            set(channels 8)
            set(display "WJN Control Deck Gain 7.1")
        endif()
    endif()
    set(target wjn_signal_switch_${kind})
    string(REPLACE "_" "-" module_kind "${kind}")
    add_library(${target} SHARED Module.cpp)
    wjn_configure_shared_target(${target} WJN_NODE_IMPLEMENTATION)
    target_compile_definitions(${target} PRIVATE WJN_SWITCH_CHANNELS=${channels}
        WJN_SWITCH_CONTROL=${control}
        "WJN_SWITCH_KIND=\"${module_kind}\""
        "WJN_SWITCH_NAME=\"${display}\"" WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${target} PRIVATE /utf-8)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/node/include")
    target_link_libraries(${target} PRIVATE control_deck WinJACKNexus::Audio)
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${upper} "${WJN_OUTPUT_DIR}/nodes/control_deck/${kind}")
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${kind}/locate"
            "$<TARGET_FILE_DIR:${target}>/locate")
endforeach()

if(BUILD_TESTING)
    add_executable(wjn_control_deck_state_tests StateTests.cpp)
    target_link_libraries(wjn_control_deck_state_tests PRIVATE control_deck_state)
    target_compile_options(wjn_control_deck_state_tests PRIVATE /utf-8)
    set_target_properties(wjn_control_deck_state_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_test(NAME control_deck_state_tests COMMAND wjn_control_deck_state_tests)
    add_executable(wjn_control_deck_module_tests ModuleTests.cpp)
    target_link_libraries(wjn_control_deck_module_tests PRIVATE control_deck control_deck_state)
    target_include_directories(wjn_control_deck_module_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/node/include")
    target_compile_definitions(wjn_control_deck_module_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_control_deck_module_tests PRIVATE /utf-8)
    set_target_properties(wjn_control_deck_module_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_control_deck_module_tests wjn_signal_switch_mono wjn_signal_switch_stereo
        wjn_signal_switch_5_1 wjn_signal_switch_7_1 wjn_signal_switch_midi)
    add_test(NAME control_deck_module_tests COMMAND wjn_control_deck_module_tests)
    find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(wjn_control_deck_host_tests HostTests.cpp)
    target_include_directories(wjn_control_deck_host_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/patchbay/mod/include"
        "${PROJECT_SOURCE_DIR}/src/native_fx/include")
    target_link_libraries(wjn_control_deck_host_tests PRIVATE control_deck control_deck_state
        mod_patchbay patchbay_core WinJACKNexus::UI WinJACKNexus::Node WinJACKNexus::Audio Qt6::Test)
    target_compile_definitions(wjn_control_deck_host_tests PRIVATE WJN_NODE_IMPLEMENTATION
        WIN32_LEAN_AND_MEAN NOMINMAX "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_control_deck_host_tests PRIVATE /utf-8)
    set_target_properties(wjn_control_deck_host_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_control_deck_host_tests wjn_control_deck_module_tests)
    add_executable(wjn_control_deck_final_tests FinalTests.cpp)
    target_include_directories(wjn_control_deck_final_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/patchbay/mod/include"
        "${PROJECT_SOURCE_DIR}/src/native_fx/include")
    target_link_libraries(wjn_control_deck_final_tests PRIVATE control_deck control_deck_state
        mod_patchbay patchbay_core WinJACKNexus::UI WinJACKNexus::Node WinJACKNexus::Audio Qt6::Test)
    target_compile_definitions(wjn_control_deck_final_tests PRIVATE WJN_NODE_IMPLEMENTATION
        WIN32_LEAN_AND_MEAN NOMINMAX "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_control_deck_final_tests PRIVATE /utf-8)
    set_target_properties(wjn_control_deck_final_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_control_deck_final_tests wjn_control_deck_module_tests)
    add_executable(wjn_control_deck_tests ControlDeckTests.cpp)
    target_include_directories(wjn_control_deck_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/patchbay/mod/include" "${PROJECT_SOURCE_DIR}/src/native_fx/include")
    target_link_libraries(wjn_control_deck_tests PRIVATE control_deck control_deck_state mod_patchbay
        patchbay_core WinJACKNexus::UI WinJACKNexus::Node WinJACKNexus::Audio Qt6::Test)
    target_compile_definitions(wjn_control_deck_tests PRIVATE WJN_NODE_IMPLEMENTATION
        WIN32_LEAN_AND_MEAN NOMINMAX "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_control_deck_tests PRIVATE /utf-8)
    set_target_properties(wjn_control_deck_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_control_deck_tests wjn_control_deck_module_tests wjn_signal_switch_sound
        wjn_signal_switch_peak wjn_signal_switch_gain_mono wjn_signal_switch_gain_stereo
        wjn_signal_switch_gain_5_1 wjn_signal_switch_gain_7_1 wjn_control_deck_layout_tests)
    add_executable(wjn_control_deck_layout_tests LayoutTests.cpp)
    target_link_libraries(wjn_control_deck_layout_tests PRIVATE control_deck control_deck_state
        WinJACKNexus::UI Qt6::Test)
    target_compile_definitions(wjn_control_deck_layout_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_control_deck_layout_tests PRIVATE /utf-8)
    set_target_properties(wjn_control_deck_layout_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
endif()
