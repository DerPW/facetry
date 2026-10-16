# Small header-only dependencies, downloaded at configure time and verified by hash.
include(FetchContent)

# nlohmann/json: JSON model descriptions (facetry-script, CLI, golden tests). Not used by the core
# library itself, so it never ends up in the app or the web module.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip
    URL_HASH SHA256=b8cb0ef2dd7f57f18933997c9934bb1fa962594f701cd5a8d3c2c80541559372
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
add_library(facetry-json INTERFACE)
target_include_directories(facetry-json SYSTEM INTERFACE
    "${nlohmann_json_SOURCE_DIR}" "${nlohmann_json_SOURCE_DIR}/include")

if(FACETRY_BUILD_TESTS)
    # doctest is used as a single header.
    FetchContent_Declare(doctest
        URL https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h
        URL_HASH SHA256=cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(doctest)
    add_library(facetry-doctest INTERFACE)
    target_include_directories(facetry-doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
endif()
