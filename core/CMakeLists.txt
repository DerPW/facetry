configure_file(src/build_info.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp" @ONLY)

# The core library: C API (facetry_core.h), C++ API (facetry/core/*.hpp) and the kernel adapter.
add_library(facetry-core STATIC
    src/c_api.cpp
    src/cancellation.cpp
    src/log.cpp
    src/naming.cpp
    src/stl.cpp
    src/version.cpp
    src/document/document.cpp
    src/document/document_json.cpp
    src/document/file_format.cpp
    src/document/regeneration.cpp
    src/document/session.cpp
    src/document/session_json.cpp
    src/kernel/occt_common.cpp
    src/kernel/occt_mesh.cpp
    src/kernel/occt_modeling.cpp
    src/kernel/occt_naming.cpp
    src/kernel/occt_shape.cpp
    src/kernel/occt_version.cpp)
add_library(facetry::core ALIAS facetry-core)

target_include_directories(facetry-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated")

# OpenCASCADE is private: OCCT types and headers never leave the kernel adapter (src/kernel/).
# nlohmann/json and miniz read and write documents and files (src/document/).
target_link_libraries(facetry-core PRIVATE facetry::occt facetry-json facetry-miniz)

facetry_set_warnings(facetry-core)

# JSON model descriptions, used by the CLI and the golden-model tests.
add_library(facetry-script STATIC src/script/model_script.cpp)
add_library(facetry::script ALIAS facetry-script)
target_link_libraries(facetry-script PUBLIC facetry::core PRIVATE facetry-json)
facetry_set_warnings(facetry-script)

if(EMSCRIPTEN)
    add_subdirectory(wasm)
endif()

if(FACETRY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
