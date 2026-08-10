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

# ---------------------------------------------------------------------------
# 全局编译标准
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS YES)

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
