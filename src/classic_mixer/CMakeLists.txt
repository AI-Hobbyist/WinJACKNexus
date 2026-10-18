# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_classic_mixer SHARED Module.cpp)
wjn_configure_shared_target(mod_classic_mixer WJN_NODE_IMPLEMENTATION)
target_link_libraries(mod_classic_mixer PRIVATE classic_mixer_core
    WinJACKNexus::Audio WinJACKNexus::UI)
target_include_directories(mod_classic_mixer PRIVATE ${PROJECT_SOURCE_DIR}/src/node/include)
target_compile_options(mod_classic_mixer PRIVATE /utf-8)
set_target_properties(mod_classic_mixer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}/nodes/classic_mixer")
add_custom_command(TARGET mod_classic_mixer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/locate"
        "$<TARGET_FILE_DIR:mod_classic_mixer>/locate")

if(BUILD_TESTING)
    find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(classic_mixer_gui_tests
        ${PROJECT_SOURCE_DIR}/Develop/Node/ClassicMixerGuiTests.cpp)
    target_link_libraries(classic_mixer_gui_tests PRIVATE classic_mixer_core
        WinJACKNexus::UI Qt6::Test)
    target_include_directories(classic_mixer_gui_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/node/include)
    target_compile_options(classic_mixer_gui_tests PRIVATE /utf-8)
    target_compile_definitions(classic_mixer_gui_tests PRIVATE
        CLASSIC_MIXER_DLL="$<TARGET_FILE:mod_classic_mixer>")
    set_target_properties(classic_mixer_gui_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(classic_mixer_gui_tests mod_classic_mixer)
    add_test(NAME classic_mixer_gui COMMAND classic_mixer_gui_tests)
    set_tests_properties(classic_mixer_gui PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1")
    add_executable(classic_mixer_module_tests
        ${PROJECT_SOURCE_DIR}/Develop/Node/ClassicMixerModuleTests.cpp)
    target_link_libraries(classic_mixer_module_tests PRIVATE classic_mixer_core Qt6::Core)
    target_include_directories(classic_mixer_module_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/node/include
        ${PROJECT_SOURCE_DIR}/src/patchbay/mod/include)
    target_compile_options(classic_mixer_module_tests PRIVATE /utf-8)
    target_compile_definitions(classic_mixer_module_tests PRIVATE
        CLASSIC_MIXER_DLL="$<TARGET_FILE:mod_classic_mixer>")
    set_target_properties(classic_mixer_module_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(classic_mixer_module_tests mod_classic_mixer)
    add_test(NAME classic_mixer_module COMMAND classic_mixer_module_tests)
endif()
