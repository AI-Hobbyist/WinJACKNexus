add_executable(wjn_node_tests
    NodeTests.cpp
)

target_include_directories(wjn_node_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/node/include")
target_compile_features(wjn_node_tests PRIVATE cxx_std_20)
target_compile_definitions(wjn_node_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(wjn_node_tests node wjn_example_node)

set_target_properties(
    wjn_node_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${WJN_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${WJN_OUTPUT_DIR}"
)

add_test(NAME node_unit_tests COMMAND wjn_node_tests)