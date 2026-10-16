// Tests the C API contract that the Swift and TypeScript shells rely on.
#include "facetry_core.h"

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

namespace {

struct Context {
    facetry_context* ctx = nullptr;
    Context() {
        REQUIRE(facetry_context_create(&ctx) == FACETRY_OK);
    }
    ~Context() {
        facetry_context_destroy(ctx);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

template <typename T>
std::vector<T> buffer(facetry_context* ctx, facetry_handle mesh, facetry_mesh_buffer_kind kind) {
    const void* data = nullptr;
    size_t count = 0;
    REQUIRE(facetry_mesh_buffer(ctx, mesh, kind, &data, &count) == FACETRY_OK);
    const auto* typed = static_cast<const T*>(data);
    return std::vector<T>(typed, typed + count);
}

}  // namespace

TEST_CASE("api version and build info") {
    uint32_t major = 99;
    uint32_t minor = 99;
    facetry_api_version(&major, &minor);
    CHECK(major == FACETRY_API_VERSION_MAJOR);
    CHECK(minor == FACETRY_API_VERSION_MINOR);
    CHECK(std::string(facetry_core_version()) == FACETRY_EXPECTED_VERSION);
    CHECK(std::string(facetry_kernel_version()) == FACETRY_EXPECTED_OCCT_VERSION);
    CHECK(std::string(facetry_platform()) == FACETRY_EXPECTED_PLATFORM);
}

TEST_CASE("make box, fillet and tessellate through the C API") {
    Context c;
    facetry_handle box = 0;
    REQUIRE(facetry_shape_make_box(c.ctx, 20, 10, 5, &box) == FACETRY_OK);
    CHECK(box != 0);

    facetry_shape_properties props{};
    REQUIRE(facetry_shape_properties_get(c.ctx, box, &props) == FACETRY_OK);
    CHECK(props.volume == doctest::Approx(1000.0));
    CHECK(props.face_count == 6);
    CHECK(props.edge_count == 12);
    CHECK(props.vertex_count == 8);

    const std::array<uint32_t, 1> edges = {1};
    facetry_handle rounded = 0;
    REQUIRE(facetry_shape_fillet(c.ctx, box, edges.data(), edges.size(), 1.0, &rounded) ==
            FACETRY_OK);
    REQUIRE(facetry_shape_properties_get(c.ctx, rounded, &props) == FACETRY_OK);
    CHECK(props.face_count == 7);
    CHECK(props.volume < 1000.0);

    facetry_handle mesh = 0;
    REQUIRE(facetry_shape_tessellate(c.ctx, rounded, 0.05, 0.35, 0, &mesh) == FACETRY_OK);

    const auto positions = buffer<float>(c.ctx, mesh, FACETRY_MESH_POSITIONS);
    const auto normals = buffer<float>(c.ctx, mesh, FACETRY_MESH_NORMALS);
    const auto triangles = buffer<uint32_t>(c.ctx, mesh, FACETRY_MESH_TRIANGLES);
    const auto faceIds = buffer<uint32_t>(c.ctx, mesh, FACETRY_MESH_TRIANGLE_FACE_IDS);
    const auto edgeOffsets = buffer<uint32_t>(c.ctx, mesh, FACETRY_MESH_EDGE_OFFSETS);
    const auto edgeIds = buffer<uint32_t>(c.ctx, mesh, FACETRY_MESH_EDGE_IDS);
    const auto edgePoints = buffer<float>(c.ctx, mesh, FACETRY_MESH_EDGE_POINTS);
    const auto vertexIds = buffer<uint32_t>(c.ctx, mesh, FACETRY_MESH_VERTEX_IDS);

    CHECK(positions.size() % 3 == 0);
    CHECK(normals.size() == positions.size());
    CHECK(triangles.size() % 3 == 0);
    CHECK(faceIds.size() == triangles.size() / 3);
    CHECK(edgeOffsets.size() == edgeIds.size() + 1);
    CHECK(edgeOffsets.back() * 3 == edgePoints.size());
    CHECK(vertexIds.size() == static_cast<size_t>(props.vertex_count));
    for (const uint32_t index : triangles) {
        REQUIRE(index < positions.size() / 3);
    }

    CHECK(facetry_mesh_release(c.ctx, mesh) == FACETRY_OK);
    CHECK(facetry_shape_release(c.ctx, rounded) == FACETRY_OK);
    CHECK(facetry_shape_release(c.ctx, box) == FACETRY_OK);
}

TEST_CASE("errors are reported as status plus message") {
    Context c;
    facetry_handle shape = 0;

    CHECK(facetry_shape_make_box(c.ctx, -1, 10, 5, &shape) == FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(facetry_last_error_message(c.ctx)).find("positive") != std::string::npos);
    CHECK(facetry_last_error_status(c.ctx) == FACETRY_ERROR_INVALID_ARGUMENT);

    CHECK(facetry_shape_make_box(c.ctx, 1, 1, 1, &shape) == FACETRY_OK);
    CHECK(std::string(facetry_last_error_message(c.ctx)).empty());

    CHECK(facetry_shape_release(c.ctx, 12345) == FACETRY_ERROR_INVALID_HANDLE);
    CHECK(facetry_mesh_release(c.ctx, shape) == FACETRY_ERROR_INVALID_HANDLE);  // wrong kind
    CHECK(facetry_shape_make_box(c.ctx, 1, 1, 1, nullptr) == FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_shape_make_box(nullptr, 1, 1, 1, &shape) == FACETRY_ERROR_INVALID_ARGUMENT);

    const std::array<uint32_t, 1> missingEdge = {999};
    facetry_handle out = 0;
    CHECK(facetry_shape_fillet(c.ctx, shape, missingEdge.data(), 1, 0.1, &out) ==
          FACETRY_ERROR_INVALID_ARGUMENT);

    CHECK(facetry_shape_release(c.ctx, shape) == FACETRY_OK);
    CHECK(facetry_shape_release(c.ctx, shape) == FACETRY_ERROR_INVALID_HANDLE);  // released
}

TEST_CASE("repeated kernel failures leave the core usable") {
    // Guards against stack or memory corruption after caught exceptions, which other OCCT
    // WebAssembly builds ran into (docs/PRIOR-ART.md).
    Context c;
    facetry_handle box = 0;
    REQUIRE(facetry_shape_make_box(c.ctx, 20, 10, 5, &box) == FACETRY_OK);
    const std::array<uint32_t, 1> edges = {1};
    for (int i = 0; i < 200; ++i) {
        facetry_handle out = 0;
        REQUIRE(facetry_shape_fillet(c.ctx, box, edges.data(), 1, 50.0, &out) ==
                FACETRY_ERROR_KERNEL);
    }
    CHECK(std::string(facetry_last_error_message(c.ctx)).find("Fillet") != std::string::npos);

    facetry_handle rounded = 0;
    REQUIRE(facetry_shape_fillet(c.ctx, box, edges.data(), 1, 1.0, &rounded) == FACETRY_OK);
    facetry_shape_properties props{};
    REQUIRE(facetry_shape_properties_get(c.ctx, rounded, &props) == FACETRY_OK);
    CHECK(props.face_count == 7);
}

TEST_CASE("boolean cut through the C API") {
    Context c;
    facetry_handle box = 0;
    facetry_handle cylinder = 0;
    facetry_handle cut = 0;
    REQUIRE(facetry_shape_make_box(c.ctx, 20, 10, 5, &box) == FACETRY_OK);
    REQUIRE(facetry_shape_make_cylinder(c.ctx, 10, 5, 2, 5, &cylinder) == FACETRY_OK);
    REQUIRE(facetry_shape_boolean(c.ctx, FACETRY_BOOLEAN_CUT, box, cylinder, &cut) == FACETRY_OK);
    facetry_shape_properties props{};
    REQUIRE(facetry_shape_properties_get(c.ctx, cut, &props) == FACETRY_OK);
    CHECK(props.volume == doctest::Approx(1000.0 - std::numbers::pi * 4.0 * 5.0));
}

TEST_CASE("log callback receives messages at or above the level") {
    struct Collected {
        std::vector<std::string> messages;
    } collected;
    facetry_set_log_callback(
        [](facetry_log_level, const char* message, void* user) {
            static_cast<Collected*>(user)->messages.emplace_back(message);
        },
        &collected);
    facetry_set_log_level(FACETRY_LOG_DEBUG);

    Context c;
    facetry_handle shape = 0;
    CHECK(facetry_shape_make_box(c.ctx, 0, 1, 1, &shape) == FACETRY_ERROR_INVALID_ARGUMENT);

    facetry_set_log_callback(nullptr, nullptr);
    facetry_set_log_level(FACETRY_LOG_INFO);
    REQUIRE_FALSE(collected.messages.empty());
    CHECK(collected.messages.back().find("Box width") != std::string::npos);
}
