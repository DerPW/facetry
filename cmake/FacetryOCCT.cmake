# Imports the prebuilt OpenCASCADE artifact as the target facetry::occt.
#
# The artifact comes from third_party/occt/fetch.sh (download) or build.sh + package.sh (local
# build) and lives in third_party/_install/occt-<version>-r<rev>/{apple,wasm}.
#   Apple: OCCT.xcframework, linked dynamically (one framework per platform slice)
#   WebAssembly: static libraries

file(STRINGS "${PROJECT_SOURCE_DIR}/third_party/occt/occt.env" _facetry_occt_env
    REGEX "^OCCT_(VERSION|BUILD_REV)=")
foreach(_line IN LISTS _facetry_occt_env)
    if(_line MATCHES "^(OCCT_[A-Z_]+)=(.*)$")
        set(FACETRY_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()

set(FACETRY_OCCT_ARTIFACT "occt-${FACETRY_OCCT_VERSION}-r${FACETRY_OCCT_BUILD_REV}")
set(FACETRY_OCCT_ROOT "${PROJECT_SOURCE_DIR}/third_party/_install/${FACETRY_OCCT_ARTIFACT}"
    CACHE PATH "Directory containing the prebuilt OpenCASCADE artifact")

if(EMSCRIPTEN)
    set(_facetry_occt_kind wasm)
elseif(APPLE)
    set(_facetry_occt_kind apple)
else()
    message(FATAL_ERROR "No prebuilt OpenCASCADE artifact for ${CMAKE_SYSTEM_NAME}")
endif()

set(_facetry_occt_dir "${FACETRY_OCCT_ROOT}/${_facetry_occt_kind}")
if(NOT EXISTS "${_facetry_occt_dir}/BUILDINFO.txt")
    message(FATAL_ERROR
        "OpenCASCADE ${FACETRY_OCCT_ARTIFACT} (${_facetry_occt_kind}) not found in ${_facetry_occt_dir}.\n"
        "Run: third_party/occt/fetch.sh ${_facetry_occt_kind}\n"
        "or build it locally, see third_party/occt/README.md")
endif()

if(EMSCRIPTEN)
    file(GLOB _facetry_occt_libs "${_facetry_occt_dir}/lib/*.a")
    add_library(facetry-occt INTERFACE IMPORTED GLOBAL)
    set_target_properties(facetry-occt PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_facetry_occt_dir}/include/opencascade"
        INTERFACE_LINK_LIBRARIES "${_facetry_occt_libs}")
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            set(_facetry_occt_slice ios-arm64-simulator)
        else()
            set(_facetry_occt_slice ios-arm64)
        endif()
        set(_facetry_occt_binary OCCT.framework/OCCT)
    else()
        set(_facetry_occt_slice macos-arm64)
        set(_facetry_occt_binary OCCT.framework/Versions/A/OCCT)
    endif()
    set(FACETRY_OCCT_FRAMEWORK_DIR "${_facetry_occt_dir}/OCCT.xcframework/${_facetry_occt_slice}")

    add_library(facetry-occt SHARED IMPORTED GLOBAL)
    set_target_properties(facetry-occt PROPERTIES
        IMPORTED_LOCATION "${FACETRY_OCCT_FRAMEWORK_DIR}/${_facetry_occt_binary}"
        INTERFACE_INCLUDE_DIRECTORIES "${_facetry_occt_dir}/include/opencascade")
    # Executables built in this tree (tests, tools) find the framework through the build rpath.
    # The apps embed the framework instead.
    list(APPEND CMAKE_BUILD_RPATH "${FACETRY_OCCT_FRAMEWORK_DIR}")
endif()

add_library(facetry::occt ALIAS facetry-occt)
message(STATUS "OpenCASCADE: ${FACETRY_OCCT_ARTIFACT} (${_facetry_occt_kind}) from ${_facetry_occt_dir}")
