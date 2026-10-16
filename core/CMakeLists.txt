configure_file(src/build_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp" @ONLY)

add_library(facetry-core STATIC
    src/version.cpp)
add_library(facetry::core ALIAS facetry-core)

target_include_directories(facetry-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

facetry_set_warnings(facetry-core)

if(FACETRY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
