# This file is part of WinJACKNexus.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(wjn_gate_tests GateTests.cpp)
target_link_libraries(wjn_gate_tests PRIVATE
    WinJACKNexus::Common WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Node Qt6::Widgets)
add_dependencies(wjn_gate_tests mod_native_fx)
target_compile_definitions(wjn_gate_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_gate_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_gate_tests COMMAND wjn_gate_tests "$<TARGET_FILE:mod_gate>")
set_tests_properties(native_gate_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_graphic_eq_tests GraphicEqTests.cpp)
target_link_libraries(wjn_graphic_eq_tests PRIVATE WinJACKNexus::Common WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
target_compile_definitions(wjn_graphic_eq_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_graphic_eq_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_graphic_eq_tests COMMAND wjn_graphic_eq_tests "$<TARGET_FILE:mod_graphic_eq>" "${CMAKE_CURRENT_BINARY_DIR}/GraphicEq-preview.png")

add_executable(wjn_parametric_eq_tests ParametricEqTests.cpp)
target_link_libraries(wjn_parametric_eq_tests PRIVATE WinJACKNexus::Common WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
add_dependencies(wjn_parametric_eq_tests mod_native_fx)
target_compile_definitions(wjn_parametric_eq_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_parametric_eq_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_parametric_eq_tests COMMAND wjn_parametric_eq_tests "$<TARGET_FILE:mod_parametric_eq>" "${CMAKE_CURRENT_BINARY_DIR}/ParametricEq-preview.png")
set_tests_properties(native_parametric_eq_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")
set_tests_properties(native_graphic_eq_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_deesser_tests DeEsserTests.cpp)
target_link_libraries(wjn_deesser_tests PRIVATE WinJACKNexus::Common WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
target_compile_definitions(wjn_deesser_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_deesser_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_deesser_tests COMMAND wjn_deesser_tests "$<TARGET_FILE:mod_deesser>" "${CMAKE_CURRENT_BINARY_DIR}/DeEsser-preview.png")
set_tests_properties(native_deesser_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_compressor_tests CompressorTests.cpp)
target_link_libraries(wjn_compressor_tests PRIVATE WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
add_dependencies(wjn_compressor_tests mod_native_fx)
target_compile_definitions(wjn_compressor_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_compressor_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_compressor_tests COMMAND wjn_compressor_tests "$<TARGET_FILE:mod_compressor>" "${CMAKE_CURRENT_BINARY_DIR}/Compressor-preview.png")
set_tests_properties(native_compressor_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_limiter_tests LimiterTests.cpp)
target_link_libraries(wjn_limiter_tests PRIVATE WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
add_dependencies(wjn_limiter_tests mod_native_fx)
target_compile_definitions(wjn_limiter_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_limiter_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_limiter_tests COMMAND wjn_limiter_tests "$<TARGET_FILE:mod_limiter>" "${CMAKE_CURRENT_BINARY_DIR}/Limiter-preview.png")
set_tests_properties(native_limiter_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_delay_tests DelayTests.cpp)
target_link_libraries(wjn_delay_tests PRIVATE WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
add_dependencies(wjn_delay_tests mod_native_fx)
target_compile_definitions(wjn_delay_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_delay_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}")
add_test(NAME native_delay_tests COMMAND wjn_delay_tests "$<TARGET_FILE:mod_delay>" "${CMAKE_CURRENT_BINARY_DIR}/Delay-preview.png")
set_tests_properties(native_delay_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_reverb_tests ReverbTests.cpp)
target_link_libraries(wjn_reverb_tests PRIVATE WinJACKNexus::Node WinJACKNexus::UI Qt6::Widgets)
add_dependencies(wjn_reverb_tests mod_native_fx)
target_compile_definitions(wjn_reverb_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(wjn_reverb_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}")
add_test(NAME native_reverb_tests COMMAND wjn_reverb_tests "$<TARGET_FILE:mod_reverb>" "$<TARGET_FILE_DIR:wjn_reverb_tests>/Reverb-preview.png")
set_tests_properties(native_reverb_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

add_executable(wjn_native_fx_api_tests NativeFxApiTests.cpp)
target_include_directories(wjn_native_fx_api_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/native_fx/include)
target_link_libraries(wjn_native_fx_api_tests PRIVATE WinJACKNexus::UI Qt6::Widgets)
add_dependencies(wjn_native_fx_api_tests mod_native_fx)
set_target_properties(wjn_native_fx_api_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
add_test(NAME native_fx_api_tests COMMAND wjn_native_fx_api_tests
    "$<TARGET_FILE:mod_gate>" "$<TARGET_FILE:mod_graphic_eq>"
    "$<TARGET_FILE:mod_deesser>" "$<TARGET_FILE:mod_compressor>"
    "$<TARGET_FILE:mod_limiter>" "$<TARGET_FILE:mod_delay>"
    "$<TARGET_FILE:mod_parametric_eq>" "$<TARGET_FILE:mod_reverb>")
set_tests_properties(native_fx_api_tests PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=${WJN_OUTPUT_DIR}")

foreach(fx IN ITEMS gate graphic_eq parametric_eq deesser compressor limiter delay reverb)
    target_link_libraries(wjn_${fx}_tests PRIVATE mod_${fx})
    set_tests_properties(native_${fx}_tests PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${WJN_OUTPUT_DIR}/platforms;PATH=$<TARGET_FILE_DIR:mod_${fx}>;${WJN_OUTPUT_DIR}")
endforeach()
