# Buildable API examples and boundary checks.
add_library(wjn_example_native_fx SHARED NativeFx/ExampleNativeFx.cpp)
target_include_directories(wjn_example_native_fx PRIVATE
    ${PROJECT_SOURCE_DIR}/src/native_fx/include)
target_link_libraries(wjn_example_native_fx PRIVATE
    WinJACKNexus::Audio WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
wjn_configure_shared_target(wjn_example_native_fx WJN_NATIVE_FX_BUILD)
target_compile_definitions(wjn_example_native_fx PRIVATE
    WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set(wjn_example_native_fx_dir "${WJN_OUTPUT_DIR}/examples/native_fx")
set_target_properties(wjn_example_native_fx PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${wjn_example_native_fx_dir}"
    LIBRARY_OUTPUT_DIRECTORY "${wjn_example_native_fx_dir}")
foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(wjn_example_native_fx PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${wjn_example_native_fx_dir}"
        LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${wjn_example_native_fx_dir}")
endforeach()

if(BUILD_TESTING)
    add_executable(wjn_native_fx_boundary_tests NativeFx/NativeFxBoundaryTests.cpp)
    target_include_directories(wjn_native_fx_boundary_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/native_fx/include
        ${PROJECT_SOURCE_DIR}/src/node/include)
    target_link_libraries(wjn_native_fx_boundary_tests PRIVATE Qt6::Widgets)
    wjn_configure_shared_target(wjn_native_fx_boundary_tests WJN_DEVELOP_TEST_BUILD)
    add_dependencies(wjn_native_fx_boundary_tests wjn_example_native_fx mod_native_fx)
    add_test(NAME native_fx_boundary_tests COMMAND wjn_native_fx_boundary_tests
        "$<TARGET_FILE:wjn_example_native_fx>"
        "$<TARGET_FILE:mod_gate>" "$<TARGET_FILE:mod_graphic_eq>"
        "$<TARGET_FILE:mod_deesser>" "$<TARGET_FILE:mod_compressor>"
        "$<TARGET_FILE:mod_limiter>" "$<TARGET_FILE:mod_delay>"
        "$<TARGET_FILE:mod_parametric_eq>" "$<TARGET_FILE:mod_reverb>")
    set_tests_properties(native_fx_boundary_tests PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")
endif()
