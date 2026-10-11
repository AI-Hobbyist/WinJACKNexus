# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(
    node SHARED
    src/NodeApi.cpp include/WinJACKNexus/Node/Export.hpp include/WinJACKNexus/Node/INode.hpp
    include/WinJACKNexus/Node/NodeApi.h include/WinJACKNexus/Node/NodeInfo.hpp
    include/WinJACKNexus/Node/ProcessingApi.h
    include/WinJACKNexus/Node/ModuleCompatibility.hpp
    include/WinJACKNexus/Node/JackOnlyModuleV4.inc)

target_include_directories(
    node
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/common/include> $<INSTALL_INTERFACE:include>)

wjn_configure_shared_target(node WJN_NODE_BUILD)
add_library(WinJACKNexus::Node ALIAS node)

add_library(wjn_example_node SHARED ${PROJECT_SOURCE_DIR}/Develop/Node/ExampleNode.cpp)

target_include_directories(wjn_example_node PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                                    ${PROJECT_SOURCE_DIR}/src/common/include)

wjn_configure_shared_target(wjn_example_node WJN_NODE_IMPLEMENTATION)
target_link_libraries(wjn_example_node PRIVATE Qt6::Core)
set_target_properties(wjn_example_node PROPERTIES OUTPUT_NAME "wjn_example_node")
foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        wjn_example_node PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}
                                    "${WJN_OUTPUT_DIR}/examples/node")
endforeach()
add_custom_command(
    TARGET wjn_example_node
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Develop/Node/locate"
            "$<TARGET_FILE_DIR:wjn_example_node>/locate")
