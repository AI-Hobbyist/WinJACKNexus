cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus.MixerPro VERSION 0.1.0 LANGUAGES CXX)

add_library(WinJACKNexus.MixerProCore STATIC
    Source/Audio/AudioEngine.cpp
    Source/Audio/NullAudioBackend.cpp
    Source/DSP/GainStage.cpp
    Source/DSP/LevelMeterProbe.cpp
    Source/DSP/Panner.cpp
    Source/Mixer/MixerGraph.cpp
    Source/Mixer/SoloMuteResolver.cpp)

target_include_directories(WinJACKNexus.MixerProCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(WinJACKNexus.MixerProCore
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(WinJACKNexus.MixerProCore
    PUBLIC
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_gui_app(WinJACKNexus.MixerPro
    PRODUCT_NAME "WinJACKNexus.MixerPro"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "AI-Hobbyist"
    BUNDLE_ID "dev.winjacknexus.mixerpro")

target_sources(WinJACKNexus.MixerPro PRIVATE
    Source/App/MainWindow.cpp
    Source/App/MixerProApplication.cpp
    Source/Audio/CommonJackMixerRuntime.cpp
    Source/UI/Views/MixerConsoleView.cpp)

target_include_directories(WinJACKNexus.MixerPro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(WinJACKNexus.MixerPro PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(WinJACKNexus.MixerPro PRIVATE
    WinJACKNexus.MixerProCore
    WinJACKNexus.Common
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

set(WINJACKNEXUS_MIXERPRO_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_custom_command(TARGET WinJACKNexus.MixerPro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WinJACKNexus.MixerPro>/locales/Mixer"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINJACKNEXUS_MIXERPRO_SOURCE_ROOT}/locales/zh-CN.lang"
        "$<TARGET_FILE_DIR:WinJACKNexus.MixerPro>/locales/zh-CN.lang"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINJACKNEXUS_MIXERPRO_SOURCE_ROOT}/locales/Mixer/zh-CN.lang"
        "$<TARGET_FILE_DIR:WinJACKNexus.MixerPro>/locales/Mixer/zh-CN.lang")

add_executable(WinJACKNexus.MixerProEngineTests
    Tests/EngineSmokeTests.cpp)

target_include_directories(WinJACKNexus.MixerProEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_link_libraries(WinJACKNexus.MixerProEngineTests PRIVATE
    WinJACKNexus.MixerProCore)

add_test(NAME WinJACKNexus.MixerProEngineTests
    COMMAND WinJACKNexus.MixerProEngineTests)