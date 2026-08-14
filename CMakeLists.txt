# ==============================================================================
#  WinJACKNexus - Windows WDM <-> JACK 桥接套件（顶层工程）
#
#  模块：
#    - WinJACKNexus.Common   跨模块共享库（JUCE + libjack Bridge、无锁 FIFO、主题等）
#    - WinJACKNexus.Adapter  首个独立 GUI 应用（单实例、暗黑 3 Tab 机架）
#
#  依赖：third_party/JUCE（add_subdirectory 集成）、third_party/JACK2（导入库）
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(WinJACKNexus
    VERSION 0.1.0
    DESCRIPTION "WinJACKNexus - Windows WDM <-> JACK bridge suite"
    HOMEPAGE_URL "https://github.com/AI-Hobbyist/WinJACKNexus"
    LANGUAGES C CXX)

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件: " CACHE STRING
        "MSVC /showIncludes prefix used by Ninja dependency scanning" FORCE)
endif()

# ---------------------------------------------------------------------------
# 全局编译标准
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

include(CTest)

# 源文件统一以 UTF-8 处理（工程注释含中文，MSVC 默认按本地代码页解析会产生 C4819/误读）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# 依赖：JUCE（JUCE 9，add_subdirectory 方式集成）
# ---------------------------------------------------------------------------
add_subdirectory(third_party/JUCE)

# ---------------------------------------------------------------------------
# 模块
# ---------------------------------------------------------------------------
add_subdirectory(modules/WinJACKNexus.Common)
add_subdirectory(modules/WinJACKNexus.Adapter)

add_custom_target(WinJACKNexus.CommonResources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WinJACKNexus.Adapter>/LCD"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WinJACKNexus.Adapter>/locales/Adapter"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LCD/zpix.ttf"
        "$<TARGET_FILE_DIR:WinJACKNexus.Adapter>/LCD/zpix.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LCD/DS-DIGI.TTF"
        "$<TARGET_FILE_DIR:WinJACKNexus.Adapter>/LCD/DS-DIGI.TTF"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/locales/zh-CN.lang"
        "$<TARGET_FILE_DIR:WinJACKNexus.Adapter>/locales/zh-CN.lang"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/locales/Adapter/zh-CN.lang"
        "$<TARGET_FILE_DIR:WinJACKNexus.Adapter>/locales/Adapter/zh-CN.lang"
    DEPENDS
        WinJACKNexus.Adapter
        "${CMAKE_SOURCE_DIR}/LCD/zpix.ttf"
        "${CMAKE_SOURCE_DIR}/LCD/DS-DIGI.TTF"
        "${CMAKE_SOURCE_DIR}/locales/zh-CN.lang"
        "${CMAKE_SOURCE_DIR}/locales/Adapter/zh-CN.lang")
