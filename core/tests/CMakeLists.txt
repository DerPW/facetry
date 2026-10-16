include(FetchContent)

# doctest is used as a single header, downloaded and verified by hash.
FetchContent_Declare(doctest
    URL https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h
    URL_HASH SHA256=cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(doctest)

add_library(facetry-doctest INTERFACE)
target_include_directories(facetry-doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

add_executable(facetry-core-tests
    main.cpp
    version_test.cpp)

target_link_libraries(facetry-core-tests PRIVATE facetry::core facetry-doctest)
facetry_set_warnings(facetry-core-tests)

# Each test binary checks that it was built for the platform the build targets.
if(EMSCRIPTEN)
    set(FACETRY_EXPECTED_PLATFORM "wasm")
    # Propagate main()'s return value as the process exit code under Node.
    target_link_options(facetry-core-tests PRIVATE -sEXIT_RUNTIME=1)
else()
    set(FACETRY_EXPECTED_PLATFORM "macos")
endif()
target_compile_definitions(facetry-core-tests PRIVATE
    FACETRY_EXPECTED_VERSION="${PROJECT_VERSION}"
    FACETRY_EXPECTED_PLATFORM="${FACETRY_EXPECTED_PLATFORM}")

add_test(NAME facetry-core-tests COMMAND facetry-core-tests)
