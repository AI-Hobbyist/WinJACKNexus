# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(node SHARED
    src/NodeApi.cpp
    include/WinJACKNexus/Node/Export.hpp
    include/WinJACKNexus/Node/INode.hpp
    include/WinJACKNexus/Node/NodeApi.h
    include/WinJACKNexus/Node/NodeInfo.hpp
)

target_include_directories(node
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

wjn_configure_shared_target(node WJN_NODE_BUILD)
add_library(WinJACKNexus::Node ALIAS node)

add_library(wjn_example_node SHARED
    ${PROJECT_SOURCE_DIR}/Develop/Node/ExampleNode.cpp
)

target_include_directories(wjn_example_node
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

wjn_configure_shared_target(wjn_example_node WJN_NODE_IMPLEMENTATION)
set_target_properties(wjn_example_node PROPERTIES OUTPUT_NAME "wjn_example_node")
