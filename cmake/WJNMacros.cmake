# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
include_guard(GLOBAL)

function(wjn_configure_shared_target target export_definition)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PRIVATE ${export_definition})
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )

    foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(
            ${target}
            PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}"
                LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${WJN_OUTPUT_DIR}"
                ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper} "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
        )
    endforeach()
endfunction()