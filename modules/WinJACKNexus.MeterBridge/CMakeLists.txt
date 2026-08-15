cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus.MeterBridge VERSION 0.1.0 LANGUAGES CXX)

juce_add_gui_app(WinJACKNexus.MeterBridge
    PRODUCT_NAME "WinJACKNexus.MeterBridge"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "AI-Hobbyist")

target_include_directories(WinJACKNexus.MeterBridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_sources(WinJACKNexus.MeterBridge PRIVATE
    Source/Main.cpp
    Source/App/MeterBridgeApplication.cpp
    Source/App/MeterBridgeMainWindow.cpp
    Source/Engine/MeterAudioBridge.cpp
    Source/Model/MeterProject.cpp
    Source/Model/MeterChannelModel.cpp
    Source/UI/HistoryWindow.cpp
    Source/UI/SettingsDialog.cpp
    Source/UI/MeterBridgeMainComponent.cpp)

target_link_libraries(WinJACKNexus.MeterBridge PRIVATE
    WinJACKNexus.Common
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)

add_custom_command(TARGET WinJACKNexus.MeterBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WinJACKNexus.MeterBridge>/LCD"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WinJACKNexus.MeterBridge>/locales/MeterBridge"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LCD/zpix.ttf"
        "$<TARGET_FILE_DIR:WinJACKNexus.MeterBridge>/LCD/zpix.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LCD/DS-DIGI.TTF"
        "$<TARGET_FILE_DIR:WinJACKNexus.MeterBridge>/LCD/DS-DIGI.TTF"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/locales/zh-CN.lang"
        "$<TARGET_FILE_DIR:WinJACKNexus.MeterBridge>/locales/zh-CN.lang"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/locales/MeterBridge/zh-CN.lang"
        "$<TARGET_FILE_DIR:WinJACKNexus.MeterBridge>/locales/MeterBridge/zh-CN.lang")
