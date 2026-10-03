# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(mod_vst_host SHARED
    src/Library.cpp
    src/I18n.cpp
    src/I18n.hpp
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
        WinJACKNexus::UI
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

add_custom_command(TARGET mod_vst_host POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/locate" "$<TARGET_FILE_DIR:mod_vst_host>/locate"
    VERBATIM)

if(BUILD_TESTING)
    add_executable(wjn_vst_i18n_tests "${PROJECT_SOURCE_DIR}/Develop/VstI18nTests.cpp")
    target_link_libraries(wjn_vst_i18n_tests PRIVATE WinJACKNexus::UI WinJACKNexus::Node Qt6::Widgets)
    target_compile_features(wjn_vst_i18n_tests PRIVATE cxx_std_20)
    target_compile_options(wjn_vst_i18n_tests PRIVATE /utf-8)
    target_compile_definitions(wjn_vst_i18n_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_target_properties(wjn_vst_i18n_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
    add_dependencies(wjn_vst_i18n_tests mod_vst_host)
endif()
