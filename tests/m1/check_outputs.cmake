# This file is part of WinJACKNexus.
# Licensed under the GNU Affero General Public License, version 3.
# SPDX-License-Identifier: AGPL-3.0-only
if(NOT DEFINED WJN_OUTPUT_DIR)
    message(FATAL_ERROR "WJN_OUTPUT_DIR was not provided")
endif()

foreach(output_name IN ITEMS common.dll ui.dll audio.dll plugin.dll)
    if(NOT EXISTS "${WJN_OUTPUT_DIR}/${output_name}")
        message(FATAL_ERROR "Missing M1 output: ${WJN_OUTPUT_DIR}/${output_name}")
    endif()
endforeach()

message(STATUS "M1 DLL outputs are present in ${WJN_OUTPUT_DIR}")