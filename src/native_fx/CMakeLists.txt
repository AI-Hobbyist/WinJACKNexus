# This file is part of WinJACKNexus.
# SPDX-License-Identifier: AGPL-3.0-only
# One DLL per Native Fx. Both hosts discover placement through the module's meta.
set(wjn_native_fx_ids
    gate
    graphic_eq
    deesser
    compressor
    limiter
    delay
    parametric_eq
    reverb)
set(wjn_native_fx_targets)
set(wjn_native_fx_index 0)
foreach(fx IN LISTS wjn_native_fx_ids)
    set(target "mod_${fx}")
    add_library(
        ${target} SHARED
        src/Library.cpp
        src/NativeFxApi.cpp
        src/I18n.cpp
        effects/${fx}/src/Ui.cpp
        effects/${fx}/Meta.hpp
        effects/${fx}/include/Logic.inc
        effects/${fx}/include/Dsp.hpp
        effects/${fx}/src/Dsp.cpp)
    if(fx STREQUAL "graphic_eq" OR fx STREQUAL "parametric_eq")
        target_sources(${target} PRIVATE effects/graphic_eq/src/SpectrumAnalyzer.cpp)
    endif()
    target_include_directories(
        ${target}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/common/include
               ${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/include)
    if(fx STREQUAL "parametric_eq")
        target_include_directories(${target}
                                   PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/effects/graphic_eq/include)
    endif()
    target_link_libraries(
        ${target} PRIVATE WinJACKNexus::Audio WinJACKNexus::Common WinJACKNexus::Node
                          WinJACKNexus::UI Qt6::Core Qt6::Widgets)
    wjn_configure_shared_target(${target} WJN_NATIVE_FX_BUILD)
    target_compile_definitions(
        ${target} PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX
                          WJN_FX_MODULE_INDEX=${wjn_native_fx_index})
    set_target_properties(
        ${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/native_fx/${fx}"
                             LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/native_fx/${fx}")
    add_custom_target(
        ${target}_i18n_catalogs
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/locate"
            "${WJN_OUTPUT_DIR}/nodes/native_fx/${fx}/locate"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/locate/manifest.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/locate/en-US.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/locate/zh-CN.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/locate/ja-JP.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/effects/${fx}/locate/ko-KR.json"
        COMMENT "Deploy ${fx} i18n catalogs"
        VERBATIM)
    add_dependencies(${target} ${target}_i18n_catalogs)
    foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(
            ${target}
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}
                       "${WJN_OUTPUT_DIR}/nodes/native_fx/${fx}"
                       LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}
                       "${WJN_OUTPUT_DIR}/nodes/native_fx/${fx}")
    endforeach()
    list(APPEND wjn_native_fx_targets ${target})
    math(EXPR wjn_native_fx_index "${wjn_native_fx_index}+1")
endforeach()
add_custom_target(mod_native_fx DEPENDS ${wjn_native_fx_targets})
