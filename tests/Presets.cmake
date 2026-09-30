add_executable(wjn_preset_tests "${CMAKE_CURRENT_LIST_DIR}/PresetTests.cpp")
target_link_libraries(wjn_preset_tests PRIVATE
    WinJACKNexus::MeterbridgeModUi WinJACKNexus::MeterbridgePreviewModel Qt6::Widgets)
target_compile_features(wjn_preset_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_preset_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_preset_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME preset_tests COMMAND wjn_preset_tests)
set_tests_properties(preset_tests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
