# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.25)

set(WJN_VST3SDK_ROOT "${PROJECT_SOURCE_DIR}/third_party/vst3sdk")

add_library(
    wjn_vst_scanner_core STATIC
    src/Scanner.cpp
    include/WinJACKNexus/VstScanner/Scanner.hpp
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/moduleinfo/moduleinfo.h"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/moduleinfo/moduleinfoparser.h"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/moduleinfo/json.h"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/moduleinfo/jsoncxx.h")

target_include_directories(
    wjn_vst_scanner_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                $<INSTALL_INTERFACE:include> "${WJN_VST3SDK_ROOT}")

if(WJN_ENABLE_VST2)
    target_include_directories(wjn_vst_scanner_core
                               PRIVATE "${PROJECT_SOURCE_DIR}/third_party/vst2sdk")
endif()

target_compile_features(wjn_vst_scanner_core PUBLIC cxx_std_20)
target_compile_definitions(wjn_vst_scanner_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
                                                        WJN_ENABLE_VST2=$<BOOL:${WJN_ENABLE_VST2}>)
target_link_libraries(wjn_vst_scanner_core PUBLIC Qt6::Core WinJACKNexus::Common)

add_executable(
    vst_scanner_probe64
    src/ProbeMain.cpp
    "${WJN_VST3SDK_ROOT}/pluginterfaces/base/funknown.cpp"
    "${WJN_VST3SDK_ROOT}/pluginterfaces/base/coreiids.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp")

target_include_directories(vst_scanner_probe64 PRIVATE "${WJN_VST3SDK_ROOT}")

if(WJN_ENABLE_VST2)
    target_include_directories(vst_scanner_probe64
                               PRIVATE "${PROJECT_SOURCE_DIR}/third_party/vst2sdk")
endif()

set_target_properties(vst_scanner_probe64 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_compile_features(vst_scanner_probe64 PRIVATE cxx_std_17)
target_compile_definitions(vst_scanner_probe64 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
                                                       WJN_ENABLE_VST2=$<BOOL:${WJN_ENABLE_VST2}>)
target_compile_options(vst_scanner_probe64 PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(vst_scanner_probe64 PRIVATE Shell32)
set_target_properties(vst_scanner_probe64 PROPERTIES OUTPUT_NAME "Vst Scanner Probe64")

add_executable(vst_scanner WIN32 src/main.cpp src/MainWindow.cpp
                                 include/WinJACKNexus/VstScanner/MainWindow.hpp VstScanner.rc)

qt_add_resources(
    vst_scanner
    vst_scanner_icon
    PREFIX
    "/WinJACKNexus/VstScanner"
    BASE
    "${PROJECT_SOURCE_DIR}/ico"
    FILES
    "${PROJECT_SOURCE_DIR}/ico/vst_scan.ico")

target_compile_features(vst_scanner PRIVATE cxx_std_20)
target_compile_definitions(vst_scanner PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(vst_scanner PRIVATE wjn_vst_scanner_core WinJACKNexus::UI Qt6::Widgets)
set_target_properties(vst_scanner PROPERTIES OUTPUT_NAME "Vst Scanner")

# The scanner starts independently of the host; deploy its own complete domain.
qt_add_resources(
    vst_scanner
    vst_scanner_i18n
    PREFIX
    "/WinJACKNexus/VstScanner/i18n"
    BASE
    "${CMAKE_CURRENT_SOURCE_DIR}/locate"
    FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/locate/manifest.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/locate/en-US.json")
add_custom_command(
    TARGET vst_scanner
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/locate"
            "$<TARGET_FILE_DIR:vst_scanner>/vstscanner/locate"
    VERBATIM)

foreach(target IN ITEMS wjn_vst_scanner_core vst_scanner vst_scanner_probe64)
    set_target_properties(
        ${target}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}")
endforeach()

if(BUILD_TESTING AND EXISTS "${PROJECT_SOURCE_DIR}/tests/vst_scanner/CMakeLists.txt")
    add_subdirectory("${PROJECT_SOURCE_DIR}/tests/vst_scanner" tests_vst_scanner)
    add_executable(wjn_vst_scanner_i18n_tests tests/I18nTests.cpp src/MainWindow.cpp)
    target_link_libraries(wjn_vst_scanner_i18n_tests PRIVATE wjn_vst_scanner_core WinJACKNexus::UI
                                                             Qt6::Widgets)
    target_compile_features(wjn_vst_scanner_i18n_tests PRIVATE cxx_std_20)
    target_compile_definitions(wjn_vst_scanner_i18n_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(wjn_vst_scanner_i18n_tests PRIVATE /utf-8)
    set_target_properties(wjn_vst_scanner_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE
                                                                "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_vst_scanner_i18n_tests vst_scanner)
    qt_add_resources(
        wjn_vst_scanner_i18n_tests
        vst_scanner_test_i18n
        PREFIX
        "/WinJACKNexus/VstScanner/i18n"
        BASE
        "${CMAKE_CURRENT_SOURCE_DIR}/locate"
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/locate/manifest.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/locate/en-US.json")
endif()
