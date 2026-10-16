configure_file(src/build_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp" @ONLY)

add_library(facetry-core STATIC
    src/version.cpp
    src/kernel/occt_version.cpp)
add_library(facetry::core ALIAS facetry-core)

target_include_directories(facetry-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

# OpenCASCADE is private: OCCT types and headers never leave the kernel adapter.
target_link_libraries(facetry-core PRIVATE facetry::occt)

facetry_set_warnings(facetry-core)

if(FACETRY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
