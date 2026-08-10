# ==============================================================================
#  WinJACKNexus.Adapter - 独立单实例 GUI 应用
#
#  骨架阶段：仅构建可打开空窗口的最小应用（M1.1 起再实现 3 Tab / 托盘 / 单实例锁）。
#  源码规划（按计划书 §1.7）：
#    Source/Main.cpp
#    Source/App/     AdapterApplication、AdapterMainWindow（M1.1 加托盘/单实例）
#    Source/UI/      MainComponent、CascadeDeviceSelector、DeviceItemCard、
#                    DeviceLedPanel、TabPages/...（M1.1/M1.2/M1.3）
#    Source/Model/   DeviceNode、DeviceRegistry（M1.2）
#    Source/Engine/  MockEngine、RealEngine（M1.3 / 阶段二）
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus.Adapter VERSION 0.1.0 LANGUAGES CXX)

juce_add_gui_app(WinJACKNexus.Adapter
    PRODUCT_NAME "WinJACKNexus.Adapter"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "AI-Hobbyist")

target_include_directories(WinJACKNexus.Adapter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_sources(WinJACKNexus.Adapter PRIVATE
    Source/Main.cpp
    Source/App/AdapterApplication.cpp
    Source/App/AdapterMainWindow.cpp
    Source/UI/CascadeDeviceSelector.cpp
    Source/UI/DeviceItemCard.cpp
    Source/UI/MainComponent.cpp)

target_link_libraries(WinJACKNexus.Adapter PRIVATE
    WinJACKNexus.Common
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)
