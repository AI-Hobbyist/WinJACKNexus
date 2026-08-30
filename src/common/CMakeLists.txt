add_library(common SHARED
    src/Library.cpp
    include/WinJACKNexus/Common/Export.hpp
    include/WinJACKNexus/Common/Version.hpp
)

target_include_directories(common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

wjn_configure_shared_target(common WJN_COMMON_BUILD)
add_library(WinJACKNexus::Common ALIAS common)