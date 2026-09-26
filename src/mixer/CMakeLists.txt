add_library(mod_digital_mixer SHARED
    Library.cpp
    EffectGraph.cpp
)

target_include_directories(mod_digital_mixer PRIVATE ${PROJECT_SOURCE_DIR}/src/native_fx/include)

target_link_libraries(mod_digital_mixer
    PRIVATE
        WinJACKNexus::UI
        WinJACKNexus::Node
        WinJACKNexus::Common
        WinJACKNexus::Audio
)

wjn_configure_shared_target(mod_digital_mixer WJN_MIXER_MOD_BUILD)
target_compile_definitions(mod_digital_mixer PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(mod_digital_mixer PROPERTIES
    OUTPUT_NAME "mod_digital_mixer"
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/mixer")
foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(mod_digital_mixer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/mixer")
endforeach()
