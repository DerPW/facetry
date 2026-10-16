add_executable(facetry-core-tests
    main.cpp
    c_api_session_test.cpp
    c_api_test.cpp
    document_test.cpp
    golden_test.cpp
    kernel_test.cpp
    naming_scenarios_test.cpp
    naming_test.cpp
    occt_smoke_test.cpp
    session_json_test.cpp
    version_test.cpp)

# The OCCT smoke tests use OpenCASCADE directly, so the test binary links it explicitly.
target_link_libraries(facetry-core-tests PRIVATE
    facetry::script facetry::occt facetry-doctest facetry-json)
facetry_set_warnings(facetry-core-tests)

# Each test binary checks that it was built for the platform the build targets.
if(EMSCRIPTEN)
    set(FACETRY_EXPECTED_PLATFORM "wasm")
    # Propagate main()'s return value as the exit code under Node, and give the tests access
    # to the host file system (golden models).
    target_link_options(facetry-core-tests PRIVATE -sEXIT_RUNTIME=1 -sNODERAWFS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(FACETRY_EXPECTED_PLATFORM "ios-simulator")
else()
    set(FACETRY_EXPECTED_PLATFORM "macos")
endif()
target_compile_definitions(facetry-core-tests PRIVATE
    FACETRY_EXPECTED_VERSION="${PROJECT_VERSION}"
    FACETRY_EXPECTED_PLATFORM="${FACETRY_EXPECTED_PLATFORM}"
    FACETRY_EXPECTED_OCCT_VERSION="${FACETRY_OCCT_VERSION}"
    FACETRY_MODELS_DIR="${PROJECT_SOURCE_DIR}/tests/models"
    FACETRY_DOCUMENTS_DIR="${PROJECT_SOURCE_DIR}/tests/documents")

add_test(NAME facetry-core-tests COMMAND facetry-core-tests)
