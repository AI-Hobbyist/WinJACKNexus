find_package(JACK2 REQUIRED)

add_library(audio SHARED
    src/Library.cpp
    include/WinJACKNexus/Audio/Export.hpp
    include/WinJACKNexus/Audio/JackVersion.hpp
)

target_include_directories(audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${JACK2_INCLUDE_DIR}"
)

target_link_libraries(audio PRIVATE WinJACKNexus::Common)

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