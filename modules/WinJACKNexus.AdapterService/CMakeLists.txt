cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus.AdapterService VERSION 0.1.0 LANGUAGES CXX)

add_library(WinJACKNexus.AdapterServiceCore STATIC)

target_include_directories(WinJACKNexus.AdapterServiceCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(WinJACKNexus.AdapterServiceCore PRIVATE
    Source/ServiceConfig.cpp
    Source/ConfigSynchronizer.cpp
    Source/ClientRuntime.cpp
    Source/ServiceRuntime.cpp
    Source/CommandLineOptions.cpp
    Source/ServiceInstanceGuard.cpp
    Source/ServiceLogger.cpp)

target_link_libraries(WinJACKNexus.AdapterServiceCore PUBLIC
    WinJACKNexus.AdapterBackend
    WinJACKNexus.Common
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_core
    juce::juce_audio_devices)

juce_add_binary_data(WinJACKNexus.AdapterServiceResources
    NAMESPACE ServiceBinaryData
    HEADER_NAME ServiceBinaryData.h
    SOURCES "${CMAKE_SOURCE_DIR}/logos/adapter_service_transparent.png")

juce_add_gui_app(WinJACKNexus.AdapterService
    PRODUCT_NAME "WinJACKNexus.AdapterService"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "AI-Hobbyist"
    ICON_BIG "${CMAKE_SOURCE_DIR}/logos/adapter_service_transparent.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/logos/adapter_service_transparent.png")

target_include_directories(WinJACKNexus.AdapterService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_sources(WinJACKNexus.AdapterService PRIVATE
    Source/Main.cpp
    Source/ServiceApplication.cpp
    Source/ServiceProgressWindow.cpp
    Source/ServiceTrayIcon.cpp)

target_compile_definitions(WinJACKNexus.AdapterService PRIVATE
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WinJACKNexus.AdapterService,JUCE_VERSION>")

target_link_libraries(WinJACKNexus.AdapterService PRIVATE
    WinJACKNexus.AdapterServiceResources
    WinJACKNexus.AdapterServiceCore
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_gui_basics
    juce::juce_gui_extra)

    add_custom_command(TARGET WinJACKNexus.AdapterService POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:WinJACKNexus.AdapterService>/locales/AdapterService"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/locales/zh-CN.lang"
            "$<TARGET_FILE_DIR:WinJACKNexus.AdapterService>/locales/zh-CN.lang"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/locales/AdapterService/zh-CN.lang"
            "$<TARGET_FILE_DIR:WinJACKNexus.AdapterService>/locales/AdapterService/zh-CN.lang"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/locales/zh-CN.lang"
            "${CMAKE_SOURCE_DIR}/locales/AdapterService/zh-CN.lang")

if(BUILD_TESTING)
    add_executable(WinJACKNexus.AdapterService.ConfigSynchronizerTests
        tests/ConfigSynchronizerTests.cpp)
    target_link_libraries(WinJACKNexus.AdapterService.ConfigSynchronizerTests
        PRIVATE WinJACKNexus.AdapterServiceCore)
    add_test(NAME WinJACKNexus.AdapterService.ConfigSynchronizerTests
        COMMAND WinJACKNexus.AdapterService.ConfigSynchronizerTests)

    add_executable(WinJACKNexus.AdapterService.ServiceRuntimeTests
        tests/ServiceRuntimeTests.cpp)
    target_link_libraries(WinJACKNexus.AdapterService.ServiceRuntimeTests
        PRIVATE WinJACKNexus.AdapterServiceCore)
    add_test(NAME WinJACKNexus.AdapterService.ServiceRuntimeTests
        COMMAND WinJACKNexus.AdapterService.ServiceRuntimeTests)

    add_executable(WinJACKNexus.AdapterService.CommandLineOptionsTests
        tests/CommandLineOptionsTests.cpp)
    target_link_libraries(WinJACKNexus.AdapterService.CommandLineOptionsTests
        PRIVATE WinJACKNexus.AdapterServiceCore)
    add_test(NAME WinJACKNexus.AdapterService.CommandLineOptionsTests
        COMMAND WinJACKNexus.AdapterService.CommandLineOptionsTests)
endif()