# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(meterbridge_ui_model STATIC
    src/MockMeterDataProvider.cpp
    include/WinJACKNexus/Meter/MeterViewDataProvider.hpp
    include/WinJACKNexus/Meter/MeterViewSnapshot.hpp
    include/WinJACKNexus/Meter/MockMeterDataProvider.hpp
)

target_include_directories(meterbridge_ui_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(meterbridge_ui_model
    PUBLIC
        Qt6::Core
)

target_compile_features(meterbridge_ui_model PUBLIC cxx_std_20)
target_compile_definitions(meterbridge_ui_model PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_library(WinJACKNexus::MeterbridgeUiModel ALIAS meterbridge_ui_model)