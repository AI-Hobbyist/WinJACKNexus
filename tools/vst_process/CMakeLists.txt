# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.25)

set(WJN_VST3SDK_ROOT "${PROJECT_SOURCE_DIR}/third_party/vst3sdk")

set(WJN_VST_PROCESS_SOURCES
    src/ProcessMain.cpp
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
    "${WJN_VST3SDK_ROOT}/public.sdk/source/common/threadchecker_win32.cpp"
)

add_executable(vst_process64 ${WJN_VST_PROCESS_SOURCES})

target_include_directories(vst_process64
    PRIVATE
        "${WJN_VST3SDK_ROOT}"
        "${PROJECT_SOURCE_DIR}/third_party/JACK2/include"
)
target_compile_features(vst_process64 PRIVATE cxx_std_17)
target_compile_definitions(vst_process64 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(WJN_ENABLE_VST2)
    target_include_directories(vst_process64 PRIVATE "${PROJECT_SOURCE_DIR}/third_party/vst2sdk")
    target_compile_definitions(vst_process64 PRIVATE WJN_ENABLE_VST2=1)
endif()
target_compile_options(vst_process64 PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(vst_process64 PRIVATE Ole32 Shell32)
set_target_properties(
    vst_process64
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        OUTPUT_NAME "VSTProcess64"
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)