add_executable(wjn_patchbay_core_tests
    PatchbayCoreTests.cpp
)

target_link_libraries(wjn_patchbay_core_tests PRIVATE WinJACKNexus::PatchbayCore WinJACKNexus::Audio)
target_compile_features(wjn_patchbay_core_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_patchbay_core_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(wjn_patchbay_core_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME patchbay_core_unit_tests COMMAND wjn_patchbay_core_tests)