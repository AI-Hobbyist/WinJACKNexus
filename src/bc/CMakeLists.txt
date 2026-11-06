# SPDX-License-Identifier: AGPL-3.0-only
add_library(bc_state STATIC "${PROJECT_SOURCE_DIR}/src/common/src/BroadcastState.cpp")
target_include_directories(bc_state PUBLIC "${PROJECT_SOURCE_DIR}/src/common/include")
target_link_libraries(bc_state PUBLIC Qt6::Core)
target_compile_options(bc_state PRIVATE /utf-8)

add_library(bc_connection STATIC Connection.cpp Connection.hpp)
target_include_directories(bc_connection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bc_connection PUBLIC Qt6::Core PRIVATE advapi32)
target_compile_definitions(bc_connection PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(bc_connection PRIVATE /utf-8)

find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Network WebSockets)
add_library(bc_session STATIC PublishSession.cpp PublishSession.hpp)
target_link_libraries(bc_session PUBLIC bc_connection Qt6::Network Qt6::WebSockets)
target_compile_options(bc_session PRIVATE /utf-8)

add_library(bc_wire STATIC Wire.cpp Wire.hpp)
target_include_directories(bc_wire PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bc_wire PUBLIC Qt6::Core)
target_compile_options(bc_wire PRIVATE /utf-8)

add_library(bc_udp STATIC UdpTransport.cpp UdpTransport.hpp)
target_link_libraries(bc_udp PUBLIC bc_wire bc_session)
target_compile_options(bc_udp PRIVATE /utf-8)

include(MediaDependencies.cmake)
add_library(bc_encoder STATIC Encoder.cpp Encoder.hpp)
target_link_libraries(bc_encoder PUBLIC bc_state bc_wire PRIVATE opus samplerate)
target_compile_options(bc_encoder PRIVATE /utf-8)

add_library(broadcast_runtime SHARED Service.cpp Service.hpp)
set_target_properties(broadcast_runtime PROPERTIES AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}")
target_compile_definitions(broadcast_runtime PRIVATE WJN_BROADCAST_RUNTIME_BUILD)
target_compile_options(broadcast_runtime PRIVATE /utf-8)
target_link_libraries(broadcast_runtime PUBLIC bc_state bc_connection PRIVATE bc_udp bc_encoder)

add_library(mod_broadcast SHARED Module.cpp Node.cpp Node.hpp Window.cpp Window.hpp)
wjn_configure_shared_target(mod_broadcast WJN_NODE_IMPLEMENTATION)
target_include_directories(mod_broadcast PRIVATE "${PROJECT_SOURCE_DIR}/src/node/include")
target_link_libraries(mod_broadcast PRIVATE broadcast_runtime WinJACKNexus::Audio WinJACKNexus::UI)
target_compile_definitions(mod_broadcast PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(mod_broadcast PRIVATE /utf-8)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" upper)
    set_target_properties(mod_broadcast PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${upper} "${WJN_OUTPUT_DIR}/nodes/bc")
endforeach()
add_custom_command(TARGET mod_broadcast POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/locate"
        "$<TARGET_FILE_DIR:mod_broadcast>/locate")
