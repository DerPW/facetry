add_executable(facetry-cli main.cpp)
target_link_libraries(facetry-cli PRIVATE facetry::script facetry-json)
facetry_set_warnings(facetry-cli)

if(EMSCRIPTEN)
    # Runs in Node with access to the host file system, and returns main()'s exit code.
    target_link_options(facetry-cli PRIVATE -sEXIT_RUNTIME=1 -sNODERAWFS=1)
endif()

if(FACETRY_BUILD_TESTS)
    add_test(NAME facetry-cli-check
        COMMAND facetry-cli check "${PROJECT_SOURCE_DIR}/tests/models")
endif()
