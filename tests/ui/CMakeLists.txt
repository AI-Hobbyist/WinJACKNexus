add_executable(wjn_ui_tests
    UITests.cpp
)

target_link_libraries(wjn_ui_tests PRIVATE WinJACKNexus::UI)
target_compile_features(wjn_ui_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_ui_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(
    wjn_ui_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME ui_unit_tests COMMAND wjn_ui_tests)
