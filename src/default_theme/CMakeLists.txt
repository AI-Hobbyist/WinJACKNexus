add_library(wjn_default_theme SHARED DefaultTheme.cpp resources/default_theme.qrc)
target_link_libraries(wjn_default_theme PRIVATE Qt6::Core)
target_compile_features(wjn_default_theme PRIVATE cxx_std_20)
set_target_properties(
    wjn_default_theme
    PROPERTIES AUTOMOC ON
               AUTORCC ON
               RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}")
foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        wjn_default_theme PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}
                                     "${WJN_OUTPUT_DIR}")
endforeach()
