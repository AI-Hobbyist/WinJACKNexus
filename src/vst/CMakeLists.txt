# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_vst_host SHARED
    src/Library.cpp
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/pluginterfaces/base/funknown.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/pluginterfaces/base/coreiids.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/vst/hosting/module.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/vst/utility/stringconvert.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/vst/vstinitiids.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/vst3sdk/public.sdk/source/common/commonstringconvert.cpp"
)

target_link_libraries(mod_vst_host
    PRIVATE
        WinJACKNexus::Node
        Qt6::Core
        Qt6::Widgets
        Ole32
)

    target_include_directories(mod_vst_host PRIVATE "${PROJECT_SOURCE_DIR}/third_party/vst3sdk")

wjn_configure_shared_target(mod_vst_host WJN_VST_HOST_BUILD)
target_compile_definitions(mod_vst_host PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
if(WJN_ENABLE_VST2)
    target_include_directories(mod_vst_host PRIVATE "${PROJECT_SOURCE_DIR}/third_party/vst2sdk")
    target_compile_definitions(mod_vst_host PRIVATE WJN_ENABLE_VST2=1)
endif()
set_target_properties(mod_vst_host PROPERTIES
    OUTPUT_NAME "mod_vst_host"
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/vst"
    LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/vst"
)

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(mod_vst_host PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/vst"
        LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/vst"
    )
endforeach()