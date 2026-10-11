# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(
    patchbay_core SHARED
    src/Library.cpp
    src/Snapshot.cpp
    src/GraphModel.cpp
    src/SearchModel.cpp
    include/WinJACKNexus/Patchbay/Core/Export.hpp
    include/WinJACKNexus/Patchbay/Core/Types.hpp
    include/WinJACKNexus/Patchbay/Core/ClientInfo.hpp
    include/WinJACKNexus/Patchbay/Core/PortInfo.hpp
    include/WinJACKNexus/Patchbay/Core/ConnectionInfo.hpp
    include/WinJACKNexus/Patchbay/Core/Snapshot.hpp
    include/WinJACKNexus/Patchbay/Core/GraphModel.hpp
    include/WinJACKNexus/Patchbay/Core/SearchModel.hpp)

target_include_directories(
    patchbay_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include>)

target_link_libraries(patchbay_core PRIVATE WinJACKNexus::Common WinJACKNexus::Audio)

wjn_configure_shared_target(patchbay_core WJN_PATCHBAY_CORE_BUILD)
add_library(WinJACKNexus::PatchbayCore ALIAS patchbay_core)
