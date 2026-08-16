cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus.AdapterBackend VERSION 0.1.0 LANGUAGES CXX)

add_library(WinJACKNexus.AdapterBackend STATIC)

target_include_directories(WinJACKNexus.AdapterBackend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(WinJACKNexus.AdapterBackend PRIVATE
    Source/RealEngine.cpp
    Source/DeviceEnumerator.cpp
    Source/DeviceFilter.cpp)

target_link_libraries(WinJACKNexus.AdapterBackend PUBLIC
    WinJACKNexus.Common
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_events)
