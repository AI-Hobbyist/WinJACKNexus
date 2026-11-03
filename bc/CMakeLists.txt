# Standalone Rust services stay outside the default C++ build target set.
find_program(WJN_CARGO_EXECUTABLE NAMES cargo)
if(WJN_CARGO_EXECUTABLE)
    add_custom_target(
        wjn_bc_server
        COMMAND "${WJN_CARGO_EXECUTABLE}" build
                --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
                --package bc-server --release --locked
                --target-dir "${PROJECT_SOURCE_DIR}/build/bc-target"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM)
endif()
