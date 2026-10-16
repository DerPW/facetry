# Small header-only dependencies, downloaded at configure time and verified by hash.
include(FetchContent)

# nlohmann/json: documents (facetry-core, privately) and JSON model descriptions (facetry-script,
# CLI, golden tests).
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/include.zip
    URL_HASH SHA256=b8cb0ef2dd7f57f18933997c9934bb1fa962594f701cd5a8d3c2c80541559372
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
add_library(facetry-json INTERFACE)
target_include_directories(facetry-json SYSTEM INTERFACE
    "${nlohmann_json_SOURCE_DIR}" "${nlohmann_json_SOURCE_DIR}/include")

# miniz: zip container of the native file format (docs/adr/0008-native-file-format.md).
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
    URL_HASH SHA256=f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniz)
add_library(facetry-miniz STATIC "${miniz_SOURCE_DIR}/miniz.c")
target_include_directories(facetry-miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}")
# No clock (entries get a fixed time, so saved files are reproducible) and no stdio (memory only).
target_compile_definitions(facetry-miniz PUBLIC MINIZ_NO_TIME MINIZ_NO_STDIO)

# sokol_gfx: the viewport's graphics abstraction (docs/adr/0004-viewport-renderer-shared-cpp-sokol.md).
set(FACETRY_SOKOL_COMMIT 5e94d1291fea)
FetchContent_Declare(sokol_gfx
    URL https://raw.githubusercontent.com/floooh/sokol/${FACETRY_SOKOL_COMMIT}/sokol_gfx.h
    URL_HASH SHA256=6af70de1a768ac3ff85f4c0a145c2e8e702abd5f101c2ad005b69ef69d5094f0
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_Declare(sokol_log
    URL https://raw.githubusercontent.com/floooh/sokol/${FACETRY_SOKOL_COMMIT}/sokol_log.h
    URL_HASH SHA256=a8d55de21694410740e752ea8e57c4103d5951d20339ba9712e24232adef9a42
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(sokol_gfx sokol_log)
add_library(facetry-sokol INTERFACE)
target_include_directories(facetry-sokol SYSTEM INTERFACE
    "${sokol_gfx_SOURCE_DIR}" "${sokol_log_SOURCE_DIR}")

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
