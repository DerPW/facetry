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

# Tests run natively, in the iOS simulator and, for WebAssembly, in Node.
# iOS device builds are build-only.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(FACETRY_TESTS_DEFAULT OFF)
else()
    set(FACETRY_TESTS_DEFAULT ON)
endif()
option(FACETRY_BUILD_TESTS "Build the test suites" ${FACETRY_TESTS_DEFAULT})
option(FACETRY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (enabled in CI)" OFF)

if(EMSCRIPTEN)
    # Must match the OpenCASCADE build (third_party/occt/build.sh): OCCT reports failures with
    # C++ exceptions, so native WebAssembly exceptions are on everywhere, and SIMD is enabled.
    add_compile_options(-fwasm-exceptions -msimd128)
    # 8 MB stack like other OCCT WebAssembly builds (see docs/PRIOR-ART.md): booleans and
    # fillets recurse deeply.
    add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB)
endif()

include(cmake/FacetryWarnings.cmake)
include(cmake/FacetryOCCT.cmake)
include(cmake/FacetryDependencies.cmake)

if(FACETRY_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

# The command-line tool runs on the development machine only.
if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_subdirectory(tools/cli)
endif()
