add_library(mod_patchbay SHARED
    src/Library.cpp
    src/PatchbayCanvas.cpp
    include/mod_patchbay/Export.hpp
    include/mod_patchbay/PatchbayCanvas.hpp
    resources/patchbay.qrc
)

target_include_directories(mod_patchbay
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mod_patchbay
    PRIVATE
        WinJACKNexus::UI
        WinJACKNexus::Node
        patchbay_core
)

wjn_configure_shared_target(mod_patchbay WJN_PATCHBAY_MOD_BUILD)
target_compile_definitions(mod_patchbay PRIVATE WJN_NODE_IMPLEMENTATION)
set_target_properties(mod_patchbay PROPERTIES AUTOMOC ON AUTORCC ON)