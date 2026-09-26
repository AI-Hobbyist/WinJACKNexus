# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(ui SHARED
    src/Library.cpp
    src/ApplicationSettings.cpp
    src/AnimatedWidget.cpp
    src/AuxSettingsWindow.cpp
    src/FxSettingsWindow.cpp
    src/FxLevelMeterWidget.cpp
    src/HistoryChartWidget.cpp
    src/Controls.cpp
    src/HistoryLegendWidget.cpp
    src/MetricReadout.cpp
    src/MetricStatisticsWidget.cpp
    src/MeterLevelBar.cpp
    src/IconManager.cpp
    src/MeterWidget.cpp
    src/MixerWindow.cpp
    src/MixerPolarPlot.cpp
    src/NativeWindowUtils.cpp
    src/PaintableWidget.cpp
    src/ThemeManager.cpp
    src/ThemePalette.cpp
    src/TrayController.cpp
    include/WinJACKNexus/UI/Export.hpp
    include/WinJACKNexus/UI/ApplicationSettings.hpp
    include/WinJACKNexus/UI/AnimatedWidget.hpp
    include/WinJACKNexus/UI/AuxSettingsWindow.hpp
    include/WinJACKNexus/UI/FxSettingsWindow.hpp
    include/WinJACKNexus/UI/FxLevelMeterWidget.hpp
    include/WinJACKNexus/UI/HistoryChartWidget.hpp
    include/WinJACKNexus/UI/Controls.hpp
    include/WinJACKNexus/UI/HistoryLegendWidget.hpp
    include/WinJACKNexus/UI/MetricReadout.hpp
    include/WinJACKNexus/UI/MetricStatisticsWidget.hpp
    include/WinJACKNexus/UI/MeterLevelBar.hpp
    include/WinJACKNexus/UI/IconManager.hpp
    include/WinJACKNexus/UI/MeterWidget.hpp
    include/WinJACKNexus/UI/MixerWindow.hpp
    include/WinJACKNexus/UI/MixerPolarPlot.hpp
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
    PRIVATE ${PROJECT_SOURCE_DIR}/src/native_fx/include
)

target_link_libraries(ui
    PUBLIC Qt6::Widgets WinJACKNexus::Common
    PRIVATE WinJACKNexus::Node
)

wjn_configure_shared_target(ui WJN_UI_BUILD)
set_target_properties(ui PROPERTIES AUTOMOC ON AUTORCC ON)
add_library(WinJACKNexus::UI ALIAS ui)
