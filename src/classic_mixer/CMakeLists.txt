# SPDX-License-Identifier: AGPL-3.0-only
set(classic_mixer_targets)
foreach(layout IN ITEMS 8 16 24)
    set(target mod_classic_mixer_${layout})
    list(APPEND classic_mixer_targets ${target})
    add_library(${target} SHARED Module.cpp)
    wjn_configure_shared_target(${target} WJN_NODE_IMPLEMENTATION)
    target_compile_definitions(${target} PRIVATE WJN_CLASSIC_MIXER_LAYOUT=${layout})
    target_link_libraries(${target} PRIVATE classic_mixer_core
        WinJACKNexus::Audio WinJACKNexus::UI)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src/node/include)
    target_compile_options(${target} PRIVATE /utf-8)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}/nodes/classic_mixer_${layout}")

    # Keep one source catalog; each DLL owns its own i18n identity and domain.
    set(catalog_dir "${CMAKE_CURRENT_BINARY_DIR}/catalogs/${layout}/locate")
    file(MAKE_DIRECTORY "${catalog_dir}")
    foreach(language IN ITEMS manifest en-US zh-CN ja-JP ko-KR)
        set(source_catalog "${CMAKE_CURRENT_SOURCE_DIR}/locate/${language}.json")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_catalog}")
        file(READ "${source_catalog}" catalog)
        string(REPLACE "winjacknexus-classic-mixer" "winjacknexus-classic-mixer-${layout}" catalog "${catalog}")
        string(REPLACE "classicMixer" "classicMixer${layout}" catalog "${catalog}")
        string(REPLACE "@LAYOUT@" "${layout}" catalog "${catalog}")
        file(WRITE "${catalog_dir}/${language}.json" "${catalog}")
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${catalog_dir}"
            "$<TARGET_FILE_DIR:${target}>/locate")
endforeach()

if(BUILD_TESTING)
    find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Test)
    add_library(classic_mixer_jack_fixture SHARED
        ${PROJECT_SOURCE_DIR}/Develop/Node/ClassicMixerJackFixture.cpp
        ${PROJECT_SOURCE_DIR}/Develop/Node/SubGraphJackFixture.def)
    target_include_directories(classic_mixer_jack_fixture PRIVATE ${JACK2_INCLUDE_DIR})
    target_compile_definitions(classic_mixer_jack_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_features(classic_mixer_jack_fixture PRIVATE cxx_std_20)
    set_target_properties(classic_mixer_jack_fixture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}/tests/classic_mixer")
    add_executable(classic_mixer_host_tests
        ${PROJECT_SOURCE_DIR}/Develop/Node/ClassicMixerHostTests.cpp)
    target_link_libraries(classic_mixer_host_tests PRIVATE classic_mixer_core
        WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Node
        mod_patchbay patchbay_core control_deck control_deck_state Qt6::Test)
    target_include_directories(classic_mixer_host_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/patchbay/mod/include
        ${PROJECT_SOURCE_DIR}/src/native_fx/include)
    target_compile_definitions(classic_mixer_host_tests PRIVATE WJN_NODE_IMPLEMENTATION
        CLASSIC_MIXER_DLL_8="$<TARGET_FILE:mod_classic_mixer_8>"
        CLASSIC_MIXER_DLL_16="$<TARGET_FILE:mod_classic_mixer_16>"
        CLASSIC_MIXER_DLL_24="$<TARGET_FILE:mod_classic_mixer_24>"
        CLASSIC_MIXER_BOUNDARY="$<TARGET_FILE:classic_mixer_jack_fixture>")
    target_compile_options(classic_mixer_host_tests PRIVATE /utf-8)
    set_target_properties(classic_mixer_host_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(classic_mixer_host_tests ${classic_mixer_targets} classic_mixer_jack_fixture)
    add_test(NAME classic_mixer_host COMMAND classic_mixer_host_tests)
    set_tests_properties(classic_mixer_host PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1")
    add_executable(classic_mixer_gui_tests
        ${PROJECT_SOURCE_DIR}/Develop/Node/ClassicMixerGuiTests.cpp)
    target_link_libraries(classic_mixer_gui_tests PRIVATE classic_mixer_core
        WinJACKNexus::UI Qt6::Test)
    target_include_directories(classic_mixer_gui_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/node/include)
    target_compile_options(classic_mixer_gui_tests PRIVATE /utf-8)
    target_compile_definitions(classic_mixer_gui_tests PRIVATE
        CLASSIC_MIXER_DLL_8="$<TARGET_FILE:mod_classic_mixer_8>"
        CLASSIC_MIXER_DLL_16="$<TARGET_FILE:mod_classic_mixer_16>"
        CLASSIC_MIXER_DLL_24="$<TARGET_FILE:mod_classic_mixer_24>")
    set_target_properties(classic_mixer_gui_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(classic_mixer_gui_tests ${classic_mixer_targets})
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
        CLASSIC_MIXER_DLL_8="$<TARGET_FILE:mod_classic_mixer_8>"
        CLASSIC_MIXER_DLL_16="$<TARGET_FILE:mod_classic_mixer_16>"
        CLASSIC_MIXER_DLL_24="$<TARGET_FILE:mod_classic_mixer_24>")
    set_target_properties(classic_mixer_module_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(classic_mixer_module_tests ${classic_mixer_targets})
    add_test(NAME classic_mixer_module COMMAND classic_mixer_module_tests)
endif()
