# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_executable(WinJACKNexus WIN32
    main.cpp
    WinJACKNexus.rc
)

target_link_libraries(WinJACKNexus
    PRIVATE
        WinJACKNexus::Common
        WinJACKNexus::Audio
        WinJACKNexus::UI
        WinJACKNexus::Node
        Qt6::Widgets
)

target_compile_features(WinJACKNexus PRIVATE cxx_std_20)
target_compile_options(WinJACKNexus PRIVATE /utf-8)
target_compile_definitions(WinJACKNexus PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(WinJACKNexus PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/WinJACKNexus.manifest")
add_dependencies(WinJACKNexus mod_patchbay mod_digital_mixer mod_meterbrigde mod_meterbridge_probe mod_native_fx wjn_default_theme)

set_target_properties(WinJACKNexus PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
)

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(WinJACKNexus PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}"
    )
endforeach()

set(WJN_WINDEPLOYQT "${WJN_QT6_DIR}/../../../bin/windeployqt.exe")

add_custom_command(TARGET WinJACKNexus POST_BUILD
    COMMAND "${WJN_WINDEPLOYQT}"
        --no-translations
        --dir "$<TARGET_FILE_DIR:WinJACKNexus>"
        "$<TARGET_FILE:WinJACKNexus>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qt6::OpenGL>"
        "$<TARGET_FILE_DIR:WinJACKNexus>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qt6::OpenGLWidgets>"
        "$<TARGET_FILE_DIR:WinJACKNexus>"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:WinJACKNexus>/themes"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PROJECT_SOURCE_DIR}/locate"
        "$<TARGET_FILE_DIR:WinJACKNexus>/locate"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:WinJACKNexus>/locate/qt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${WJN_QT6_DIR}/../../../translations/qtbase_zh_CN.qm"
        "${WJN_QT6_DIR}/../../../translations/qtbase_ja.qm"
        "${WJN_QT6_DIR}/../../../translations/qtbase_ko.qm"
        "$<TARGET_FILE_DIR:WinJACKNexus>/locate/qt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PROJECT_SOURCE_DIR}/themes"
        "$<TARGET_FILE_DIR:WinJACKNexus>/themes"
    COMMAND "${CMAKE_COMMAND}" -E rm -f
        "$<TARGET_FILE_DIR:WinJACKNexus>/themes/winjacknexus-default.wjnskin"
    COMMENT "Deploying Qt runtime dependencies for WinJACKNexus.exe"
    VERBATIM
)
