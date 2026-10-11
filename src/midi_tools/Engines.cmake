# Locked library-only dependencies. SPDX-License-Identifier: AGPL-3.0-only
include(FetchContent)
function(wjn_midi_engines)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS ON)
    set(osal cpp11 CACHE STRING "FluidSynth OS abstraction" FORCE)
    foreach(feature alsa aufile dbus jack ladspa libinstpatch libsndfile midishare opensles oboe network oss dsound wasapi waveout winmidi sdl3 pulseaudio pipewire readline openmp native-dls)
        set(enable-${feature} OFF CACHE BOOL "MIDI Tools memory rendering only" FORCE)
    endforeach()
    set(CMAKE_DISABLE_FIND_PACKAGE_Doxygen ON)
    FetchContent_Declare(wjn_fluid
        URL https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.5.7.tar.gz
        URL_HASH SHA256=ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/fluidsynth-2.5.7")
    FetchContent_MakeAvailable(wjn_fluid)
    # Upstream 2.5.7 non-VLA macro fails to parenthesize an expression length.
    # MSVC/cpp11: 16-group rendering otherwise corrupts the stack (/GS fail-fast).
    set(vla_header "${wjn_fluid_SOURCE_DIR}/src/utils/fluidsynth_priv.h")
    file(READ "${vla_header}" vla_source)
    string(REPLACE "alloca(_len * sizeof(_type))" "alloca((_len) * sizeof(_type))" vla_fixed "${vla_source}")
    if(NOT vla_fixed STREQUAL vla_source)
        file(WRITE "${vla_header}" "${vla_fixed}")
    else()
        string(FIND "${vla_source}" "alloca((_len) * sizeof(_type))" vla_position)
        if(vla_position EQUAL -1)
            message(FATAL_ERROR "Unexpected FluidSynth VLA macro; re-audit locked source")
        endif()
    endif()
    # Embedded sfizz dependencies are static; only its public C API is shared.
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_CXX_STANDARD 17) # sfizz 1.2.3 uses pre-char8_t UTF-8 literals
    set(CMAKE_DISABLE_FIND_PACKAGE_OpenMP ON)
    foreach(feature JACK RENDER BENCHMARKS TESTS DEMOS DEVTOOLS USE_SNDFILE)
        set(SFIZZ_${feature} OFF CACHE BOOL "MIDI Tools library only" FORCE)
    endforeach()
    set(ENABLE_LTO OFF CACHE BOOL "Reproducible non-LTO library" FORCE)
    set(SFIZZ_SHARED ON CACHE BOOL "Shared C API" FORCE)
    FetchContent_Declare(wjn_sfizz
        URL https://github.com/sfztools/sfizz/releases/download/1.2.3/sfizz-1.2.3.tar.gz
        URL_HASH SHA256=a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/sfizz-1.2.3")
    FetchContent_MakeAvailable(wjn_sfizz)
    foreach(engine libfluidsynth sfizz_shared)
        foreach(config Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${config}" upper)
            set_target_properties(${engine} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${WJN_OUTPUT_DIR}" ARCHIVE_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/lib")
        endforeach()
    endforeach()
endfunction()
wjn_midi_engines()
