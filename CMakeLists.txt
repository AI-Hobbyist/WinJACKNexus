cmake_minimum_required(VERSION 3.25)

project(WinJACKNexus VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "WinJACKNexus currently supports Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(WJN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Directory for WinJACKNexus runtime files")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(WJN_WITH_JUCE "Build the optional JUCE layer in audio.dll" OFF)
option(WJN_LINK_JACK "Link the JACK import library instead of runtime loading" OFF)

set(WJN_QT6_DIR "C:/Qt/6.10.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Qt6 CMake package directory")
if(NOT Qt6_DIR AND EXISTS "${WJN_QT6_DIR}/Qt6Config.cmake")
    set(Qt6_DIR "${WJN_QT6_DIR}" CACHE PATH "Qt6 CMake package directory" FORCE)
endif()
find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Core Gui Widgets)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(WJNMacros)

add_subdirectory(src/common)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
add_subdirectory(src/plugin)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests/common)
    add_subdirectory(tests/audio)
    add_test(
        NAME m1_dll_outputs
        COMMAND "${CMAKE_COMMAND}"
            "-DWJN_OUTPUT_DIR=${WJN_OUTPUT_DIR}"
            -P "${PROJECT_SOURCE_DIR}/tests/m1/check_outputs.cmake"
    )
endif()