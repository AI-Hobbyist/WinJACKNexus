# Buildable API examples and boundary checks.
if(BUILD_TESTING)
    add_executable(wjn_main_session_probe MainSessionProbe.cpp)
    target_compile_definitions(wjn_main_session_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_main_session_probe PRIVATE /utf-8)
    target_link_libraries(wjn_main_session_probe PRIVATE ole32 uuid mmdevapi)
    set_target_properties(wjn_main_session_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
endif()

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

add_custom_command(TARGET wjn_example_native_fx POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/NativeFx/locate"
        "$<TARGET_FILE_DIR:wjn_example_native_fx>/locate")

if(BUILD_TESTING)
    add_executable(wjn_native_fx_i18n_tests NativeFx/NativeFxI18nTests.cpp)
    target_include_directories(wjn_native_fx_i18n_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/native_fx/include)
    target_compile_definitions(wjn_native_fx_i18n_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_native_fx_i18n_tests PRIVATE /utf-8)
    target_link_libraries(wjn_native_fx_i18n_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Node Qt6::Widgets)
    set_target_properties(wjn_native_fx_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_native_fx_i18n_tests WinJACKNexus mod_native_fx wjn_example_native_fx)
    add_test(NAME native_fx_i18n_tests COMMAND wjn_native_fx_i18n_tests "$<TARGET_FILE_DIR:WinJACKNexus>" "${PROJECT_SOURCE_DIR}/Docs/i18n/S5-rendered/scale-1")
    set_tests_properties(native_fx_i18n_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_PLUGIN_PATH=C:/Qt/6.10.3/msvc2022_64/plugins")
    set_property(TEST native_fx_i18n_tests PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${WJN_OUTPUT_DIR}" "PATH=path_list_prepend:C:/Qt/6.10.3/msvc2022_64/bin")
    add_executable(wjn_mixer_meter_i18n_tests MixerMeterI18nTests.cpp)
    target_compile_definitions(wjn_mixer_meter_i18n_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_mixer_meter_i18n_tests PRIVATE /utf-8)
    target_link_libraries(wjn_mixer_meter_i18n_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Common WinJACKNexus::MeterbridgeModUi Qt6::Widgets)
    set_target_properties(wjn_mixer_meter_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_mixer_meter_i18n_tests WinJACKNexus mod_digital_mixer mod_meterbrigde mod_meter_mono mod_meter_stereo mod_meter_5_1 mod_meter_7_1 mod_meter_custom mod_parametric_eq mod_compressor mod_limiter)
    add_test(NAME mixer_meter_i18n_tests COMMAND wjn_mixer_meter_i18n_tests "$<TARGET_FILE_DIR:WinJACKNexus>" "${PROJECT_SOURCE_DIR}/Docs/i18n/S4-rendered/scale-1")
    set_tests_properties(mixer_meter_i18n_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_PLUGIN_PATH=C:/Qt/6.10.3/msvc2022_64/plugins")
    set_property(TEST mixer_meter_i18n_tests PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${WJN_OUTPUT_DIR}" "PATH=path_list_prepend:C:/Qt/6.10.3/msvc2022_64/bin")
    add_executable(wjn_patchbay_i18n_tests PatchbayI18nTests.cpp)
    target_include_directories(wjn_patchbay_i18n_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/patchbay/mod/include ${PROJECT_SOURCE_DIR}/src/native_fx/include)
    target_compile_definitions(wjn_patchbay_i18n_tests PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_patchbay_i18n_tests PRIVATE /utf-8)
    target_link_libraries(wjn_patchbay_i18n_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Node WinJACKNexus::Common mod_patchbay patchbay_core Qt6::Widgets)
    set_target_properties(wjn_patchbay_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_patchbay_i18n_tests WinJACKNexus mod_wasapi_in mod_wasapi_out mod_system_midi_in mod_system_midi_out wjn_example_node)
    add_test(NAME patchbay_i18n_tests COMMAND wjn_patchbay_i18n_tests "$<TARGET_FILE_DIR:WinJACKNexus>" "$<TARGET_FILE:mod_wasapi_in>" "$<TARGET_FILE:wjn_example_node>" "${PROJECT_SOURCE_DIR}/Docs/i18n/S3-rendered/scale-1")
    set_tests_properties(patchbay_i18n_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_PLUGIN_PATH=C:/Qt/6.10.3/msvc2022_64/plugins")
    set_property(TEST patchbay_i18n_tests PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${WJN_OUTPUT_DIR}" "PATH=path_list_prepend:C:/Qt/6.10.3/msvc2022_64/bin")
    add_executable(wjn_host_i18n_tests HostI18nTests.cpp)
    target_link_libraries(wjn_host_i18n_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Audio WinJACKNexus::Node WinJACKNexus::Common Qt6::Widgets)
    target_compile_definitions(wjn_host_i18n_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_host_i18n_tests PRIVATE /utf-8)
    set_target_properties(wjn_host_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_host_i18n_tests WinJACKNexus)
    add_test(NAME host_i18n_tests COMMAND wjn_host_i18n_tests "$<TARGET_FILE_DIR:WinJACKNexus>" "${PROJECT_SOURCE_DIR}/Docs/i18n/S2-rendered/scale-1")
    set_tests_properties(host_i18n_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_PLUGIN_PATH=C:/Qt/6.10.3/msvc2022_64/plugins")
    set_property(TEST host_i18n_tests PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${WJN_OUTPUT_DIR}" "PATH=path_list_prepend:C:/Qt/6.10.3/msvc2022_64/bin")
    enable_language(C)
    add_executable(wjn_i18n_tests I18nTests.cpp I18nCAbi.c)
    set_property(TARGET wjn_i18n_tests PROPERTY C_STANDARD 11)
    target_link_libraries(wjn_i18n_tests PRIVATE WinJACKNexus::UI)
    target_compile_definitions(wjn_i18n_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(wjn_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_i18n_tests wjn_example_node wjn_example_native_fx node)
    add_test(NAME i18n_contract_tests COMMAND wjn_i18n_tests
        "$<TARGET_FILE:wjn_example_node>" "$<TARGET_FILE:wjn_example_native_fx>" "$<TARGET_FILE:node>")
    set_tests_properties(i18n_contract_tests PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=C:/Qt/6.10.3/msvc2022_64/plugins/platforms")
    set_property(TEST i18n_contract_tests PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${WJN_OUTPUT_DIR}"
        "PATH=path_list_prepend:C:/Qt/6.10.3/msvc2022_64/bin")
    add_executable(wjn_native_fx_boundary_tests NativeFx/NativeFxBoundaryTests.cpp)
    target_include_directories(wjn_native_fx_boundary_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/native_fx/include
        ${PROJECT_SOURCE_DIR}/src/common/include
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

if(BUILD_TESTING)
    add_subdirectory(Node)
endif()
