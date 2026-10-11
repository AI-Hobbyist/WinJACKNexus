# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
find_package(JACK2 REQUIRED)

add_library(
    audio SHARED
    src/Library.cpp
    src/JackApi.cpp
    src/JackClient.cpp
    src/JackGraph.cpp
    src/JackPort.cpp
    src/JackRingBuffer.cpp
    src/JackServerGuard.cpp
    src/MidiDevice.cpp
    src/MidiEvent.cpp
    src/MidiStream.cpp
    src/WasapiDevice.cpp
    src/WasapiStream.cpp
    include/WinJACKNexus/Audio/Export.hpp
    include/WinJACKNexus/Audio/JackApi.hpp
    include/WinJACKNexus/Audio/JackClient.hpp
    include/WinJACKNexus/Audio/JackGraph.hpp
    include/WinJACKNexus/Audio/JackMidiBuffer.hpp
    include/WinJACKNexus/Audio/JackPort.hpp
    include/WinJACKNexus/Audio/JackRingBuffer.hpp
    include/WinJACKNexus/Audio/JackServerGuard.hpp
    include/WinJACKNexus/Audio/MidiDevice.hpp
    include/WinJACKNexus/Audio/MidiEvent.hpp
    include/WinJACKNexus/Audio/MidiStream.hpp
    include/WinJACKNexus/Audio/WasapiDevice.hpp
    include/WinJACKNexus/Audio/WasapiFormat.hpp
    include/WinJACKNexus/Audio/WasapiStream.hpp
    include/WinJACKNexus/Audio/JackVersion.hpp)

target_include_directories(audio PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                        $<INSTALL_INTERFACE:include> "${JACK2_INCLUDE_DIR}")

target_link_libraries(audio PRIVATE WinJACKNexus::Common)
target_compile_definitions(audio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(audio PRIVATE ole32 avrt uuid winmm)

if(WJN_LINK_JACK)
    if(NOT JACK2_LIBRARY)
        message(FATAL_ERROR "WJN_LINK_JACK=ON requires a JACK2 import library")
    endif()
    target_link_libraries(audio PRIVATE JACK2::jack)
    target_compile_definitions(audio PRIVATE WJN_LINK_JACK=1)
else()
    target_compile_definitions(audio PRIVATE WJN_RUNTIME_JACK=1)
endif()

wjn_configure_shared_target(audio WJN_AUDIO_BUILD)
add_library(WinJACKNexus::Audio ALIAS audio)
