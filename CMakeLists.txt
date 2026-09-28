# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
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

option(WJN_LINK_JACK "Link the JACK import library instead of runtime loading" OFF)
option(WJN_ENABLE_VST2 "Enable VST2 runtime metadata probing (licensed use only)" ON)

set(WJN_QT6_DIR "C:/Qt/6.10.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Qt6 CMake package directory")
if(NOT Qt6_DIR AND EXISTS "${WJN_QT6_DIR}/Qt6Config.cmake")
    set(Qt6_DIR "${WJN_QT6_DIR}" CACHE PATH "Qt6 CMake package directory" FORCE)
endif()
find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(WJNMacros)

add_subdirectory(src/common)
add_subdirectory(src/default_theme)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
add_subdirectory(src/node)
add_subdirectory(src/meter)
add_subdirectory(src/patchbay)
add_subdirectory(src/mixer)
add_subdirectory(src/adapter)
add_subdirectory(src/native_fx)
add_subdirectory(src/vst)
add_subdirectory(src/app)
add_subdirectory(tools/vst_scanner)
add_subdirectory(tools/vst_process)

include(ExternalProject)

function(wjn_add_win32_auxiliary_target target source_dir build_target)
    ExternalProject_Add(
        ${target}
        SOURCE_DIR "${source_dir}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/${target}"
        CMAKE_GENERATOR "${CMAKE_GENERATOR}"
        CMAKE_GENERATOR_PLATFORM Win32
        CMAKE_ARGS
            "-DWJN_OUTPUT_DIR:PATH=${WJN_OUTPUT_DIR}"
            "-DWJN_ENABLE_VST2:BOOL=${WJN_ENABLE_VST2}"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config $<CONFIG> --target ${build_target}
        BUILD_ALWAYS TRUE
        INSTALL_COMMAND ""
        TEST_COMMAND ""
    )
endfunction()

wjn_add_win32_auxiliary_target(
    wjn_vst_scanner_probe32
    "${PROJECT_SOURCE_DIR}/tools/vst_scanner/probe32"
    vst_scanner_probe32
)
wjn_add_win32_auxiliary_target(
    wjn_vst_process32
    "${PROJECT_SOURCE_DIR}/tools/vst_process/process32"
    vst_process32
)

add_dependencies(vst_scanner_probe64 wjn_vst_scanner_probe32)
add_dependencies(vst_scanner vst_scanner_probe64 wjn_vst_scanner_probe32)
add_dependencies(vst_process64 wjn_vst_process32)

include(CTest)
add_subdirectory(Develop)
if(BUILD_TESTING AND EXISTS "${PROJECT_SOURCE_DIR}/tests/common/CMakeLists.txt")
    add_subdirectory(tests/common)
    add_subdirectory(tests/mixer)
    add_subdirectory(tests/audio)
    add_subdirectory(tests/ui)
    add_subdirectory(tests/theme)
    add_subdirectory(tests/meter)
    add_subdirectory(tests/node)
    add_subdirectory(tests/vst)
    add_subdirectory(tests/patchbay/core)
    add_subdirectory(tests/patchbay/plugins)
    add_subdirectory(tests/native_fx)
    add_test(
        NAME m1_dll_outputs
        COMMAND "${CMAKE_COMMAND}"
            "-DWJN_OUTPUT_DIR=${WJN_OUTPUT_DIR}"
            -P "${PROJECT_SOURCE_DIR}/tests/m1/check_outputs.cmake"
    )
endif()
