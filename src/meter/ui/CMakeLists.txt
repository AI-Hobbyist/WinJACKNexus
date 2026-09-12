# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(meterbridge_ui_model STATIC
    src/LiveMeterDataProvider.cpp
    src/MeterCsvWriter.cpp
    include/WinJACKNexus/Meter/LiveMeterDataProvider.hpp
    include/WinJACKNexus/Meter/MeterCsvWriter.hpp
    include/WinJACKNexus/Meter/MeterViewDataProvider.hpp
    include/WinJACKNexus/Meter/MeterViewSnapshot.hpp
)

target_include_directories(meterbridge_ui_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(meterbridge_ui_model
    PUBLIC
        WinJACKNexus::Common
        Qt6::Core
)

target_compile_features(meterbridge_ui_model PUBLIC cxx_std_20)
target_compile_definitions(meterbridge_ui_model PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(WinJACKNexus::MeterbridgeUiModel ALIAS meterbridge_ui_model)

add_library(meterbridge_preview_model STATIC EXCLUDE_FROM_ALL
    src/MockMeterDataProvider.cpp
    include/WinJACKNexus/Meter/MockMeterDataProvider.hpp
)

target_link_libraries(meterbridge_preview_model
    PUBLIC
        WinJACKNexus::MeterbridgeUiModel
)

target_compile_features(meterbridge_preview_model PUBLIC cxx_std_20)
target_compile_definitions(meterbridge_preview_model PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_library(WinJACKNexus::MeterbridgePreviewModel ALIAS meterbridge_preview_model)