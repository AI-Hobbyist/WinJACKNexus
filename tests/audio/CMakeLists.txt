add_executable(wjn_audio_tests
    AudioTests.cpp
)

target_link_libraries(wjn_audio_tests PRIVATE WinJACKNexus::Audio)
target_compile_features(wjn_audio_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_audio_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(
    wjn_audio_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME audio_unit_tests COMMAND wjn_audio_tests)