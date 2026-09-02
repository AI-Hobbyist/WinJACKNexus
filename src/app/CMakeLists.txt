add_executable(WinJACKNexus WIN32
    main.cpp
)

target_link_libraries(WinJACKNexus
    PRIVATE
        WinJACKNexus::UI
    WinJACKNexus::Node
        Qt6::Widgets
)

target_compile_features(WinJACKNexus PRIVATE cxx_std_20)
target_compile_definitions(WinJACKNexus PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

set_target_properties(WinJACKNexus PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
)

foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(WinJACKNexus PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}"
    )
endforeach()

set(WJN_WINDEPLOYQT "${WJN_QT6_DIR}/../../../bin/windeployqt.exe")

add_custom_command(TARGET WinJACKNexus POST_BUILD
    COMMAND "${WJN_WINDEPLOYQT}"
        --no-translations
        --dir "$<TARGET_FILE_DIR:WinJACKNexus>"
        "$<TARGET_FILE:WinJACKNexus>"
    COMMENT "Deploying Qt runtime dependencies for WinJACKNexus.exe"
    VERBATIM
)