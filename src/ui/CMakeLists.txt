add_library(ui SHARED
    src/Library.cpp
    include/WinJACKNexus/UI/Export.hpp
)

target_include_directories(ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui
    PUBLIC Qt6::Widgets
    PRIVATE WinJACKNexus::Common
)

wjn_configure_shared_target(ui WJN_UI_BUILD)