# ==============================================================================
#  WinJACKNexus.Common - 跨模块共享库（静态）
#
#  内容规划（按计划书 §1.7，骨架阶段仅 Version 占位）：
#    include/WinJACKNexus/Common/
#      UI/           Theme.h、NexusLookAndFeel、AudioLed/MidiLed（M1.1）
#      IO/           SpscRingBuffer、Resampler（M2.2/M2.3）
#      Bridge/       JackClientBase、JackAudioClient、JackMidiClient（M2.1）
#      Serialization/AdapterConfig（M1.4）
#      Audio/        WasapiAsyncDevice、WinMidiListener（M2.2/M2.4）
#      App/          SingleInstanceGuard（M1.1）
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus.Common VERSION 0.1.0 LANGUAGES CXX)

add_library(WinJACKNexus.Common STATIC)

if(MSVC)
    target_compile_definitions(WinJACKNexus.Common PUBLIC NOMINMAX)
endif()

target_include_directories(WinJACKNexus.Common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(WinJACKNexus.Common PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics)

# ---------------------------------------------------------------------------
# JACK2（third_party/JACK2）
#   头文件路径现在即暴露；libjack64.lib 为 MSVC COFF 导入库。
#   Bridge/ 下真正使用 libjack 符号（M2.1）时方才参与链接，此处先链入无碍。
# ---------------------------------------------------------------------------
set(WINJACK_JACK2_DIR "${CMAKE_SOURCE_DIR}/third_party/JACK2")

target_include_directories(WinJACKNexus.Common PUBLIC
    "${WINJACK_JACK2_DIR}/include")

target_link_libraries(WinJACKNexus.Common PUBLIC
    "${WINJACK_JACK2_DIR}/lib/libjack64.lib")

# ---------------------------------------------------------------------------
# 源码（骨架阶段：仅 Version 占位，保证静态库可编译、架构可被 codegraph 索引）
# ---------------------------------------------------------------------------
target_sources(WinJACKNexus.Common PRIVATE
    include/WinJACKNexus/Common/Version.cpp
    include/WinJACKNexus/Common/Audio/AudioEngine.cpp
    include/WinJACKNexus/Common/Audio/JackClient.cpp
    include/WinJACKNexus/Common/Audio/JackAudioBackend.cpp
    include/WinJACKNexus/Common/Audio/JackAudioInput.cpp
    include/WinJACKNexus/Common/Audio/JackAudioOutput.cpp
    include/WinJACKNexus/Common/Audio/MeterEngine.cpp
    include/WinJACKNexus/Common/Audio/SilenceDetector.cpp
    include/WinJACKNexus/Common/Midi/MidiEventQueue.cpp
    include/WinJACKNexus/Common/MIDI/JackMidiInput.cpp
    include/WinJACKNexus/Common/MIDI/JackMidiOutput.cpp
    include/WinJACKNexus/Common/DSP/LevelMeterProbe.cpp
    include/WinJACKNexus/Common/Metering/LoudnessPresetLibrary.cpp
    include/WinJACKNexus/Common/IO/CsvLogWriter.cpp
    include/WinJACKNexus/Common/UI/NexusLookAndFeel.cpp
    include/WinJACKNexus/Common/UI/AudioLed.cpp
    include/WinJACKNexus/Common/UI/MidiLed.cpp
    include/WinJACKNexus/Common/UI/ThemeContext.cpp
    include/WinJACKNexus/Common/UI/ThemePackage.cpp
    include/WinJACKNexus/Common/UI/ThemeAssetCache.cpp
    include/WinJACKNexus/Common/UI/FontManager.cpp
    include/WinJACKNexus/Common/UI/MeterComponent.cpp
    include/WinJACKNexus/Common/UI/ChannelCard.cpp
    include/WinJACKNexus/Common/UI/MixerChannelStripComponent.cpp
    include/WinJACKNexus/Common/UI/SpatialPannerComponent.cpp
    include/WinJACKNexus/Common/Localization/TextCatalog.cpp
    include/WinJACKNexus/Common/Localization/LocaleManager.cpp
    include/WinJACKNexus/Common/App/SingleInstanceGuard.cpp)

include(CTest)

if(BUILD_TESTING)
    add_executable(WinJACKNexus.Common.AudioEngineTests
        tests/AudioEngineTests.cpp)
    target_link_libraries(WinJACKNexus.Common.AudioEngineTests
        PRIVATE WinJACKNexus.Common)
    add_test(NAME WinJACKNexus.Common.AudioEngineTests
        COMMAND WinJACKNexus.Common.AudioEngineTests)

    add_executable(WinJACKNexus.Common.MeterEngineTests
        tests/MeterEngineTests.cpp)
    target_link_libraries(WinJACKNexus.Common.MeterEngineTests
        PRIVATE WinJACKNexus.Common)
    add_test(NAME WinJACKNexus.Common.MeterEngineTests
        COMMAND WinJACKNexus.Common.MeterEngineTests)

    add_executable(WinJACKNexus.Common.SilenceDetectorTests
        tests/SilenceDetectorTests.cpp)
    target_link_libraries(WinJACKNexus.Common.SilenceDetectorTests
        PRIVATE WinJACKNexus.Common)
    add_test(NAME WinJACKNexus.Common.SilenceDetectorTests
        COMMAND WinJACKNexus.Common.SilenceDetectorTests)

    add_executable(WinJACKNexus.Common.MidiAndFifoTests
        tests/MidiAndFifoTests.cpp)
    target_link_libraries(WinJACKNexus.Common.MidiAndFifoTests
        PRIVATE WinJACKNexus.Common)
    add_test(NAME WinJACKNexus.Common.MidiAndFifoTests
        COMMAND WinJACKNexus.Common.MidiAndFifoTests)

    add_executable(WinJACKNexus.Common.M1IntegrationBoundaryTests
        tests/M1IntegrationBoundaryTests.cpp)
    target_link_libraries(WinJACKNexus.Common.M1IntegrationBoundaryTests
        PRIVATE WinJACKNexus.Common)
    add_test(NAME WinJACKNexus.Common.M1IntegrationBoundaryTests
        COMMAND WinJACKNexus.Common.M1IntegrationBoundaryTests)

    add_executable(WinJACKNexus.Common.M2UiTests
        tests/M2UiTests.cpp)
    target_link_libraries(WinJACKNexus.Common.M2UiTests
        PRIVATE WinJACKNexus.Common)
    add_test(NAME WinJACKNexus.Common.M2UiTests
        COMMAND WinJACKNexus.Common.M2UiTests)
endif()
