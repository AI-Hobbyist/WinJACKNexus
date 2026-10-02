# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
add_library(common SHARED
    src/Library.cpp
    src/AuxMixerState.cpp
    src/DigitalMixerState.cpp
    src/DigitalMixerProcessor.cpp
    src/MixerPolarAnalysis.cpp
    src/MixerPolarCurve.cpp
    src/GraphicsView.cpp
    src/LoudnessEngine.cpp
    src/MeterCustomLoudnessPresets.cpp
    src/MeterProbeRegistry.cpp
    src/Metadata.cpp
    src/PathUtils.cpp
    src/Thread.cpp
    src/Timer.cpp
    src/Version.cpp
    src/Logger.cpp
    src/Window.cpp
    include/WinJACKNexus/Common/Atomic.hpp
    include/WinJACKNexus/Common/AuxMixerState.hpp
    include/WinJACKNexus/Common/DigitalMixerState.hpp
    include/WinJACKNexus/Common/DigitalMixerProcessor.hpp
    include/WinJACKNexus/Common/MixerPolarAnalysis.hpp
    include/WinJACKNexus/Common/MixerPolarCurve.hpp
    include/WinJACKNexus/Common/AudioRingBuffer.hpp
    include/WinJACKNexus/Common/StandardChannelNames.hpp
    include/WinJACKNexus/Common/ChannelNames.hpp
    include/WinJACKNexus/Common/ChannelLayout.hpp
    include/WinJACKNexus/Common/Export.hpp
    include/WinJACKNexus/Common/GraphicsView.hpp
    include/WinJACKNexus/Common/LockFreeSpscQueue.hpp
    include/WinJACKNexus/Common/Logger.hpp
    include/WinJACKNexus/Common/LoudnessEngine.hpp
    include/WinJACKNexus/Common/MeterSnapshot.hpp
    include/WinJACKNexus/Common/MeterProbeRegistry.hpp
    include/WinJACKNexus/Common/Metadata.hpp
    include/WinJACKNexus/Common/PathUtils.hpp
    include/WinJACKNexus/Common/RealtimeObject.hpp
    include/WinJACKNexus/Common/ScopedGuard.hpp
    include/WinJACKNexus/Common/SilenceResetController.hpp
    include/WinJACKNexus/Common/Thread.hpp
    include/WinJACKNexus/Common/Timer.hpp
    include/WinJACKNexus/Common/Version.hpp
    include/WinJACKNexus/Common/Window.hpp
)

target_include_directories(common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

wjn_configure_shared_target(common WJN_COMMON_BUILD)
target_compile_definitions(common PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    set_property(SOURCE src/LoudnessEngine.cpp APPEND PROPERTY COMPILE_OPTIONS
        "$<$<CONFIG:Debug>:/O2>"
        "$<$<CONFIG:Debug>:/Ob2>"
        "$<$<CONFIG:Debug>:/fp:precise>")
    set_property(SOURCE src/LoudnessEngine.cpp APPEND PROPERTY VS_SETTINGS
        "BasicRuntimeChecks=Default")
endif()
target_link_libraries(common
    PUBLIC
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
    PRIVATE
        avrt
        shell32
        user32
)
add_library(WinJACKNexus::Common ALIAS common)
