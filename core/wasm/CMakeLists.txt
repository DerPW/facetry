# The WebAssembly module for the web app: the C API (facetry_core.h) as an ES module, without
# Embind (docs/adr/0003-core-api-boundary-c-abi.md). The API functions are EMSCRIPTEN_KEEPALIVE,
# so linking the core as a whole archive exports all of them.
add_executable(facetry-core-web module.cpp)
target_link_libraries(facetry-core-web PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,facetry::core>")
set_target_properties(facetry-core-web PROPERTIES OUTPUT_NAME facetry-core SUFFIX ".mjs")
target_link_options(facetry-core-web PRIVATE
    --no-entry
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createFacetryCore
    -sENVIRONMENT=web,worker,node
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8,HEAPU32,HEAPF32,HEAPF64,addFunction,removeFunction
    -sALLOW_TABLE_GROWTH=1)

# Copy the module into the web app (apps/web/src/core/wasm, not checked in).
set(FACETRY_WEB_CORE_DIR "${PROJECT_SOURCE_DIR}/apps/web/src/core/wasm")
add_custom_command(TARGET facetry-core-web POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FACETRY_WEB_CORE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:facetry-core-web>"
        "$<TARGET_FILE_DIR:facetry-core-web>/facetry-core.wasm"
        "${FACETRY_WEB_CORE_DIR}"
    COMMENT "Copying the core WebAssembly module into apps/web/src/core/wasm")
