# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
include(FindPackageHandleStandardArgs)

set(WJN_JACK2_ROOT "${PROJECT_SOURCE_DIR}/third_party/JACK2" CACHE PATH "JACK2 SDK root")

find_path(
    JACK2_INCLUDE_DIR
    NAMES jack/jack.h
    HINTS "${WJN_JACK2_ROOT}/include"
)

find_library(
    JACK2_LIBRARY
    NAMES libjack64 jack64 jack
    HINTS "${WJN_JACK2_ROOT}/lib" "${WJN_JACK2_ROOT}/lib64"
)

find_package_handle_standard_args(JACK2 REQUIRED_VARS JACK2_INCLUDE_DIR)

if(JACK2_FOUND AND JACK2_LIBRARY AND NOT TARGET JACK2::jack)
    add_library(JACK2::jack UNKNOWN IMPORTED)
    if(WIN32)
        set_target_properties(
            JACK2::jack
            PROPERTIES
                IMPORTED_IMPLIB "${JACK2_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${JACK2_INCLUDE_DIR}"
        )
    else()
        set_target_properties(
            JACK2::jack
            PROPERTIES
                IMPORTED_LOCATION "${JACK2_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${JACK2_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(JACK2_INCLUDE_DIR JACK2_LIBRARY)