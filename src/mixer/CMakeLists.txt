add_library(mod_mixer SHARED
    Library.cpp
)

target_link_libraries(mod_mixer
    PRIVATE
        WinJACKNexus::UI
        WinJACKNexus::Node
)

wjn_configure_shared_target(mod_mixer WJN_MIXER_MOD_BUILD)
target_compile_definitions(mod_mixer PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(mod_mixer PROPERTIES OUTPUT_NAME "mod_mixer")