cmake_minimum_required(VERSION 3.28)

project(Facetry
    VERSION 0.0.0
    DESCRIPTION "Parametric 3D CAD for iPad, Mac and the web"
    HOMEPAGE_URL "https://github.com/DerPW/facetry"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tests run natively and, for WebAssembly, in Node. iOS builds are build-only.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(FACETRY_TESTS_DEFAULT OFF)
else()
    set(FACETRY_TESTS_DEFAULT ON)
endif()
option(FACETRY_BUILD_TESTS "Build the test suites" ${FACETRY_TESTS_DEFAULT})
option(FACETRY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (enabled in CI)" OFF)

if(EMSCRIPTEN)
    # OpenCASCADE relies on C++ exceptions, so native WebAssembly exceptions are on everywhere.
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
endif()

include(cmake/FacetryWarnings.cmake)

if(FACETRY_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)
