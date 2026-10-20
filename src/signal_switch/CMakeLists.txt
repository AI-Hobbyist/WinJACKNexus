# SPDX-License-Identifier: AGPL-3.0-only
add_library(signal_switch_state STATIC State.cpp State.hpp)
target_link_libraries(signal_switch_state PUBLIC Qt6::Gui)
target_include_directories(signal_switch_state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(signal_switch_state PRIVATE /utf-8)

add_library(signal_switch SHARED Service.cpp Service.hpp Editor.hpp
    "${PROJECT_SOURCE_DIR}/src/ui/src/SignalSwitchEditor.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/src/SignalSwitchLayout.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/src/SignalSwitchOverlay.cpp")
wjn_configure_shared_target(signal_switch WJN_SIGNAL_SWITCH_BUILD)
target_include_directories(signal_switch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/common/include")
target_link_libraries(signal_switch PRIVATE signal_switch_state WinJACKNexus::UI PUBLIC Qt6::Widgets)
target_compile_options(signal_switch PRIVATE /utf-8)
target_compile_definitions(signal_switch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

foreach(kind mono stereo 5_1 7_1 midi)
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
    else()
        set(channels 0)
        set(display "Signal Switch MIDI")
    endif()
    set(target wjn_signal_switch_${kind})
    string(REPLACE "_" "-" module_kind "${kind}")
    add_library(${target} SHARED Module.cpp)
    wjn_configure_shared_target(${target} WJN_NODE_IMPLEMENTATION)
    target_compile_definitions(${target} PRIVATE WJN_SWITCH_CHANNELS=${channels}
        "WJN_SWITCH_KIND=\"${module_kind}\""
        "WJN_SWITCH_NAME=\"${display}\"" WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(${target} PRIVATE /utf-8)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/node/include")
    target_link_libraries(${target} PRIVATE signal_switch WinJACKNexus::Audio)
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${upper} "${WJN_OUTPUT_DIR}/nodes/signal_switch/${kind}")
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/${kind}/locate"
            "$<TARGET_FILE_DIR:${target}>/locate")
endforeach()

add_custom_command(TARGET signal_switch POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/locate"
        "${WJN_OUTPUT_DIR}/signal_switch/locate")

if(BUILD_TESTING)
    add_executable(wjn_signal_switch_state_tests StateTests.cpp)
    target_link_libraries(wjn_signal_switch_state_tests PRIVATE signal_switch_state)
    target_compile_options(wjn_signal_switch_state_tests PRIVATE /utf-8)
    set_target_properties(wjn_signal_switch_state_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_test(NAME signal_switch_state_tests COMMAND wjn_signal_switch_state_tests)
    add_executable(wjn_signal_switch_module_tests ModuleTests.cpp)
    target_link_libraries(wjn_signal_switch_module_tests PRIVATE signal_switch signal_switch_state)
    target_include_directories(wjn_signal_switch_module_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/node/include")
    target_compile_definitions(wjn_signal_switch_module_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_signal_switch_module_tests PRIVATE /utf-8)
    set_target_properties(wjn_signal_switch_module_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_signal_switch_module_tests wjn_signal_switch_mono wjn_signal_switch_stereo
        wjn_signal_switch_5_1 wjn_signal_switch_7_1 wjn_signal_switch_midi)
    add_test(NAME signal_switch_module_tests COMMAND wjn_signal_switch_module_tests)
    find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(wjn_signal_switch_host_tests HostTests.cpp)
    target_include_directories(wjn_signal_switch_host_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/patchbay/mod/include"
        "${PROJECT_SOURCE_DIR}/src/native_fx/include")
    target_link_libraries(wjn_signal_switch_host_tests PRIVATE signal_switch signal_switch_state
        mod_patchbay patchbay_core WinJACKNexus::UI WinJACKNexus::Node WinJACKNexus::Audio Qt6::Test)
    target_compile_definitions(wjn_signal_switch_host_tests PRIVATE WJN_NODE_IMPLEMENTATION
        WIN32_LEAN_AND_MEAN NOMINMAX "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_signal_switch_host_tests PRIVATE /utf-8)
    set_target_properties(wjn_signal_switch_host_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_signal_switch_host_tests wjn_signal_switch_module_tests)
    add_executable(wjn_signal_switch_final_tests FinalTests.cpp)
    target_include_directories(wjn_signal_switch_final_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src/patchbay/mod/include"
        "${PROJECT_SOURCE_DIR}/src/native_fx/include")
    target_link_libraries(wjn_signal_switch_final_tests PRIVATE signal_switch signal_switch_state
        mod_patchbay patchbay_core WinJACKNexus::UI WinJACKNexus::Node WinJACKNexus::Audio Qt6::Test)
    target_compile_definitions(wjn_signal_switch_final_tests PRIVATE WJN_NODE_IMPLEMENTATION
        WIN32_LEAN_AND_MEAN NOMINMAX "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_signal_switch_final_tests PRIVATE /utf-8)
    set_target_properties(wjn_signal_switch_final_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_signal_switch_final_tests wjn_signal_switch_module_tests)
    add_executable(wjn_signal_switch_layout_tests LayoutTests.cpp)
    target_link_libraries(wjn_signal_switch_layout_tests PRIVATE signal_switch signal_switch_state
        WinJACKNexus::UI Qt6::Test)
    target_compile_definitions(wjn_signal_switch_layout_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        "SWITCH_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    target_compile_options(wjn_signal_switch_layout_tests PRIVATE /utf-8)
    set_target_properties(wjn_signal_switch_layout_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
endif()
