cmake_minimum_required(VERSION 3.25)

project(WinJACKNexusVstProcess32 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "WinJACKNexus VSTProcess currently supports Windows only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(WJN_VST3SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vst3sdk"
    CACHE PATH "VST3 SDK root")
set(WJN_JACK2_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/JACK2"
    CACHE PATH "JACK2 root")
set(WJN_OUTPUT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../build/release/bin"
    CACHE PATH "VSTProcess output directory")
option(WJN_ENABLE_VST2 "Enable VST2 hosting (licensed use only)" ON)
option(BUILD_TESTING "Build offline VST fixtures" OFF)
if(BUILD_TESTING)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../Develop/Vst/Fixtures.cmake")
    wjn_add_vst_block_fixtures(32 "${CMAKE_CURRENT_LIST_DIR}/../../.." "${WJN_OUTPUT_DIR}")
endif()

add_executable(
    vst_process32
    "${CMAKE_CURRENT_LIST_DIR}/../src/ProcessMain.cpp"
    "${WJN_VST3SDK_ROOT}/pluginterfaces/base/funknown.cpp"
    "${WJN_VST3SDK_ROOT}/pluginterfaces/base/coreiids.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/connectionproxy.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/eventlist.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/hosting/processdata.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/utility/stringconvert.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/common/commonstringconvert.cpp"
    "${WJN_VST3SDK_ROOT}/public.sdk/source/common/threadchecker_win32.cpp")

target_include_directories(
    vst_process32 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../src/common/include"
                          "${WJN_VST3SDK_ROOT}" "${WJN_JACK2_ROOT}/include")
target_compile_features(vst_process32 PRIVATE cxx_std_17)
target_compile_definitions(vst_process32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(WJN_ENABLE_VST2)
    target_include_directories(vst_process32
                               PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vst2sdk")
    target_compile_definitions(vst_process32 PRIVATE WJN_ENABLE_VST2=1)
endif()
target_compile_options(vst_process32 PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(vst_process32 PRIVATE Ole32 Shell32)
if(BUILD_TESTING)
    add_dependencies(vst_process32 wjn_vst2_block_fixture32 wjn_vst3_block_fixture32
                                  wjn_vst_jack_fixture32)
endif()
set_target_properties(
    vst_process32
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               OUTPUT_NAME "VSTProcess32"
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
               RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}")
