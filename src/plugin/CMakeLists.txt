add_library(plugin SHARED
    src/ModuleApi.cpp
    include/WinJACKNexus/Plugin/Export.hpp
    include/WinJACKNexus/Plugin/ModuleApi.h
)

target_include_directories(plugin
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

wjn_configure_shared_target(plugin WJN_PLUGIN_BUILD)