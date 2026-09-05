# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(ui SHARED
    src/Library.cpp
    src/AnimatedWidget.cpp
    src/IconManager.cpp
    src/MeterWidget.cpp
    src/NativeWindowUtils.cpp
    src/PaintableWidget.cpp
    src/ThemeManager.cpp
    src/ThemePalette.cpp
    src/TrayController.cpp
    include/WinJACKNexus/UI/Export.hpp
    include/WinJACKNexus/UI/AnimatedWidget.hpp
    include/WinJACKNexus/UI/IconManager.hpp
    include/WinJACKNexus/UI/MeterWidget.hpp
    include/WinJACKNexus/UI/NativeWindowUtils.hpp
    include/WinJACKNexus/UI/PaintableWidget.hpp
    include/WinJACKNexus/UI/ThemeManager.hpp
    include/WinJACKNexus/UI/ThemePalette.hpp
    include/WinJACKNexus/UI/TrayController.hpp
    resources/ui.qrc
)

target_include_directories(ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui
    PUBLIC Qt6::Widgets
    PRIVATE WinJACKNexus::Common
)

wjn_configure_shared_target(ui WJN_UI_BUILD)
set_target_properties(ui PROPERTIES AUTOMOC ON AUTORCC ON)
add_library(WinJACKNexus::UI ALIAS ui)