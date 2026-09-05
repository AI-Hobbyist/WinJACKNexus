add_library(mod_adapter SHARED
    src/Library.cpp
)

target_link_libraries(mod_adapter
    PRIVATE
    WinJACKNexus::UI
    WinJACKNexus::Audio
    WinJACKNexus::Common
        WinJACKNexus::Node
)

wjn_configure_shared_target(mod_adapter WJN_ADAPTER_BUILD)
target_compile_definitions(mod_adapter PRIVATE WJN_NODE_IMPLEMENTATION WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(mod_adapter PROPERTIES
    OUTPUT_NAME "mod_adapter"
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter"
    LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}/nodes/adapter"
)

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(mod_adapter PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter"
        LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}/nodes/adapter"
    )
endforeach()