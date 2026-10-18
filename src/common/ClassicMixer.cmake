# Classic Mixer core is shared source with a fixed-size real-time interface.
add_library(classic_mixer_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/ClassicMixer.cpp)
target_include_directories(classic_mixer_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(classic_mixer_core PUBLIC Qt6::Core)
target_compile_features(classic_mixer_core PUBLIC cxx_std_20)
target_compile_options(classic_mixer_core PRIVATE /utf-8)
target_compile_definitions(classic_mixer_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

if(BUILD_TESTING)
    add_executable(classic_mixer_core_tests
        ${PROJECT_SOURCE_DIR}/tests/common/ClassicMixerTests.cpp)
    target_link_libraries(classic_mixer_core_tests PRIVATE classic_mixer_core)
    set_target_properties(classic_mixer_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_test(NAME classic_mixer_core COMMAND classic_mixer_core_tests)
endif()
