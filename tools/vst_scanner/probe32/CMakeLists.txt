cmake_minimum_required(VERSION 3.25)

project(WinJACKNexusVstScannerProbe32 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "WinJACKNexus VST Scanner Probe currently supports Windows only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(WJN_VST3SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vst3sdk"
    CACHE PATH "VST3 SDK root")
set(WJN_VST2SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vst2sdk"
    CACHE PATH "VST2 SDK root")
set(WJN_OUTPUT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../build/release/bin"
    CACHE PATH "Probe output directory")

add_executable(
    vst_scanner_probe32
    "${CMAKE_CURRENT_LIST_DIR}/../src/ProbeMain.cpp"
    "${WJN_VST3SDK_ROOT}/pluginterfaces/base/funknown.cpp"
    "${WJN_VST3SDK_ROOT}/pluginterfaces/base/coreiids.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp")

target_include_directories(vst_scanner_probe32 PRIVATE "${WJN_VST3SDK_ROOT}" "${WJN_VST2SDK_ROOT}")
target_compile_features(vst_scanner_probe32 PRIVATE cxx_std_17)
target_compile_definitions(vst_scanner_probe32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
                                                       WJN_ENABLE_VST2=1)
target_compile_options(vst_scanner_probe32 PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(vst_scanner_probe32 PRIVATE Shell32)

set_target_properties(
    vst_scanner_probe32
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               OUTPUT_NAME "Vst Scanner Probe32"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../tests/vst_scanner/Vst2ShellFixture.cpp")
    add_library(wjn_vst_scanner_vst2_shell_fixture32 SHARED
                "${CMAKE_CURRENT_LIST_DIR}/../../../tests/vst_scanner/Vst2ShellFixture.cpp")
    target_include_directories(wjn_vst_scanner_vst2_shell_fixture32 PRIVATE "${WJN_VST2SDK_ROOT}")
    target_compile_features(wjn_vst_scanner_vst2_shell_fixture32 PRIVATE cxx_std_20)
    set_target_properties(
        wjn_vst_scanner_vst2_shell_fixture32
        PROPERTIES OUTPUT_NAME "Vst2 Shell Fixture32"
                   RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
                   RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}")
endif()
