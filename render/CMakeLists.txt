# facetry-render: the viewport renderer (docs/adr/0004-viewport-renderer-shared-cpp-sokol.md).
# It does not depend on OpenCASCADE; it renders the mesh buffers the core produces.
add_library(facetry-render STATIC
    src/camera.cpp
    src/math.cpp
    src/navigation.cpp
    src/pick.cpp)
add_library(facetry::render ALIAS facetry-render)
target_include_directories(facetry-render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
facetry_set_warnings(facetry-render)

# The GPU part: Metal on Apple platforms, WebGL2 on the web. Other hosts (none yet) build only
# the camera and picking code, which is all the tests need.
if(APPLE OR EMSCRIPTEN)
    target_sources(facetry-render PRIVATE src/c_api.cpp src/renderer.cpp)
    target_include_directories(facetry-render PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(facetry-render PRIVATE facetry-sokol)
    if(APPLE)
        enable_language(OBJCXX)
        target_sources(facetry-render PRIVATE src/sokol_metal.mm)
        set_source_files_properties(src/sokol_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-w")
        target_compile_definitions(facetry-render PRIVATE SOKOL_METAL)
        target_link_libraries(facetry-render PUBLIC "-framework Metal" "-framework QuartzCore")
    else()
        target_sources(facetry-render PRIVATE src/sokol_gles3.cpp)
        set_source_files_properties(src/sokol_gles3.cpp PROPERTIES COMPILE_OPTIONS "-w")
        target_compile_definitions(facetry-render PRIVATE SOKOL_GLES3)
    endif()
endif()

if(EMSCRIPTEN)
    add_subdirectory(wasm)
endif()

if(FACETRY_BUILD_TESTS)
    add_executable(facetry-render-tests
        tests/main.cpp
        tests/camera_test.cpp
        tests/navigation_test.cpp
        tests/pick_test.cpp)
    target_link_libraries(facetry-render-tests PRIVATE facetry::render facetry-doctest)
    facetry_set_warnings(facetry-render-tests)
    if(EMSCRIPTEN)
        target_link_options(facetry-render-tests PRIVATE -sEXIT_RUNTIME=1)
    endif()
    add_test(NAME facetry-render-tests COMMAND facetry-render-tests)
endif()
