# The renderer's WebAssembly module for the web app: facetry_render.h on WebGL2, loaded on the UI
# thread (the geometry core runs in its own module in a Web Worker).
add_executable(facetry-render-web module.cpp)
target_link_libraries(facetry-render-web PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,facetry::render>")
set_target_properties(facetry-render-web PROPERTIES OUTPUT_NAME facetry-render SUFFIX ".mjs")
target_link_options(facetry-render-web PRIVATE
    --no-entry
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createFacetryRender
    -sENVIRONMENT=web
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2
    -sSTACK_SIZE=1MB
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPU32,HEAPF32)

set(FACETRY_WEB_RENDER_DIR "${PROJECT_SOURCE_DIR}/apps/web/src/render/wasm")
add_custom_command(TARGET facetry-render-web POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FACETRY_WEB_RENDER_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "$<TARGET_FILE:facetry-render-web>"
        "$<TARGET_FILE_DIR:facetry-render-web>/facetry-render.wasm"
        "${FACETRY_WEB_RENDER_DIR}"
    COMMENT "Copying the renderer WebAssembly module into apps/web/src/render/wasm")
