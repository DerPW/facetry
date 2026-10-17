// The document part of the C API (sessions), as the Swift and TypeScript shells use it.
#include "facetry_core.h"

#include <doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace {

using nlohmann::json;

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

std::string example() {
    std::ifstream in(std::filesystem::path(FACETRY_DOCUMENTS_DIR) / "corner-fillet.json",
                     std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

json state(facetry_context* ctx, facetry_handle session) {
    const char* text = nullptr;
    REQUIRE(facetry_session_state(ctx, session, &text) == FACETRY_OK);
    return json::parse(text);
}

double bodyVolume(facetry_context* ctx, facetry_handle session, const char* body) {
    facetry_handle shape = 0;
    REQUIRE(facetry_session_body_shape(ctx, session, body, &shape) == FACETRY_OK);
    facetry_shape_properties properties{};
    REQUIRE(facetry_shape_properties_get(ctx, shape, &properties) == FACETRY_OK);
    REQUIRE(facetry_shape_release(ctx, shape) == FACETRY_OK);
    return properties.volume;
}

// 10 × 6 × 4 block with one vertical edge rounded by r = 1.5.
double cornerFilletVolume(double width) {
    return width * 6.0 * 4.0 - (1.0 - std::numbers::pi / 4.0) * 1.5 * 1.5 * 4.0;
}

}  // namespace

TEST_CASE("a session opens, regenerates and reports the example document") {
    Context c;
    facetry_handle session = 0;
    REQUIRE(facetry_session_create(c.ctx, example().c_str(), &session) == FACETRY_OK);

    json before = state(c.ctx, session);
    CHECK(before["needsRegeneration"] == true);
    CHECK(before["features"][2]["status"] == "notRun");
    CHECK(before["bodies"].empty());

    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    const json after = state(c.ctx, session);
    CHECK(after["needsRegeneration"] == false);
    CHECK(after["regeneration"]["executed"] == 3);
    CHECK(after["bodies"] ==
          json::array({{{"id", "f2"}, {"name", "Body 1"}, {"visible", true}, {"opacity", 1.0}}}));
    REQUIRE(after["features"].size() == 3);
    for (const json& feature : after["features"]) {
        CHECK(feature["status"] == "ok");
    }
    CHECK(after["features"][2]["type"] == "fillet");
    CHECK(after["parameters"][0] == json({{"name", "width"}, {"value", 10.0}}));
    CHECK(bodyVolume(c.ctx, session, "f2") == doctest::Approx(cornerFilletVolume(10.0)));

    // Element names line up with the IDs of the body's shape and meshes.
    facetry_handle shape = 0;
    REQUIRE(facetry_session_body_shape(c.ctx, session, "f2", &shape) == FACETRY_OK);
    facetry_shape_properties properties{};
    REQUIRE(facetry_shape_properties_get(c.ctx, shape, &properties) == FACETRY_OK);
    const char* namesText = nullptr;
    REQUIRE(facetry_session_element_names(c.ctx, session, "f2", &namesText) == FACETRY_OK);
    const json names = json::parse(namesText);
    CHECK(names["faces"].size() == static_cast<std::size_t>(properties.face_count));
    CHECK(names["edges"].size() == static_cast<std::size_t>(properties.edge_count));
    CHECK(names["vertices"].size() == static_cast<std::size_t>(properties.vertex_count));
    CHECK(names["faces"][0].get<std::string>().starts_with("f"));

    // The sketch's regions: names in the state, faces in a shape of their own, ready to mesh.
    CHECK(after["features"][0]["regions"] == json::array({"region{f1/l1+|f1/l2+|f1/l3+|f1/l4+}"}));
    CHECK_FALSE(after["features"][1].contains("regions"));
    facetry_handle regions = 0;
    REQUIRE(facetry_session_sketch_regions(c.ctx, session, "f1", &regions) == FACETRY_OK);
    facetry_shape_properties regionProperties{};
    REQUIRE(facetry_shape_properties_get(c.ctx, regions, &regionProperties) == FACETRY_OK);
    CHECK(regionProperties.face_count == 1);
    CHECK(regionProperties.area == doctest::Approx(60));
    facetry_handle regionMesh = 0;
    CHECK(facetry_shape_tessellate(c.ctx, regions, 0.1, 0.5, 0, &regionMesh) == FACETRY_OK);
    CHECK(facetry_session_sketch_regions(c.ctx, session, "f2", &regions) ==
          FACETRY_ERROR_INVALID_ARGUMENT);

    // The sketch for display: four lines, four points.
    facetry_handle sketchMesh = 0;
    REQUIRE(facetry_session_sketch_mesh(c.ctx, session, "f1", 0.01, &sketchMesh) == FACETRY_OK);
    const void* ids = nullptr;
    size_t idCount = 0;
    REQUIRE(facetry_mesh_buffer(c.ctx, sketchMesh, FACETRY_MESH_EDGE_IDS, &ids, &idCount) ==
            FACETRY_OK);
    CHECK(idCount == 4);
    REQUIRE(facetry_mesh_buffer(c.ctx, sketchMesh, FACETRY_MESH_VERTEX_IDS, &ids, &idCount) ==
            FACETRY_OK);
    CHECK(idCount == 4);
    CHECK(facetry_session_sketch_mesh(c.ctx, session, "f1", 0.0, &sketchMesh) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(after["features"][0]["frame"]["normal"] == json::array({0.0, 0.0, 1.0}));

    // A picked edge becomes a reference with its persistent name.
    const char* referenceText = nullptr;
    REQUIRE(facetry_session_make_reference(c.ctx, session, "f2", FACETRY_ELEMENT_EDGE, 1,
                                           &referenceText) == FACETRY_OK);
    const json reference = json::parse(referenceText);
    CHECK(reference["body"] == "f2");
    CHECK(reference["kind"] == "edge");
    CHECK(reference["name"] == names["edges"][0]);
    CHECK(reference["shadow"]["id"] == 1);

    CHECK(facetry_shape_release(c.ctx, shape) == FACETRY_OK);
    CHECK(facetry_session_release(c.ctx, session) == FACETRY_OK);
}

TEST_CASE("edits, undo and redo through the C API") {
    Context c;
    facetry_handle session = 0;
    REQUIRE(facetry_session_create(c.ctx, example().c_str(), &session) == FACETRY_OK);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);

    REQUIRE(facetry_session_apply(c.ctx, session,
                                  R"({"op": "setParameter", "name": "width", "value": 14})") ==
            FACETRY_OK);
    CHECK(state(c.ctx, session)["needsRegeneration"] == true);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    CHECK(state(c.ctx, session)["regeneration"]["executed"] == 3);
    CHECK(bodyVolume(c.ctx, session, "f2") == doctest::Approx(cornerFilletVolume(14.0)));

    // An array of edits is one undo step.
    REQUIRE(facetry_session_apply(c.ctx, session, R"([
        {"op": "renameFeature", "id": "f5", "name": "Corner round"},
        {"op": "suppressFeature", "id": "f5", "suppressed": true}])") == FACETRY_OK);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    const json suppressed = state(c.ctx, session);
    CHECK(suppressed["features"][2]["name"] == "Corner round");
    CHECK(suppressed["features"][2]["status"] == "suppressed");
    CHECK(bodyVolume(c.ctx, session, "f2") == doctest::Approx(14.0 * 6.0 * 4.0));

    int changed = 0;
    REQUIRE(facetry_session_undo(c.ctx, session, &changed) == FACETRY_OK);
    CHECK(changed == 1);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    const json undone = state(c.ctx, session);
    CHECK(undone["features"][2]["name"] == "Fillet 1");
    CHECK(undone["features"][2]["status"] == "ok");
    CHECK(undone["canRedo"] == true);
    REQUIRE(facetry_session_undo(c.ctx, session, &changed) == FACETRY_OK);
    REQUIRE(facetry_session_undo(c.ctx, session, &changed) == FACETRY_OK);
    CHECK(changed == 0);  // nothing left to undo
    REQUIRE(facetry_session_redo(c.ctx, session, nullptr) == FACETRY_OK);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    CHECK(bodyVolume(c.ctx, session, "f2") == doctest::Approx(cornerFilletVolume(14.0)));

    // A failing edit reports an error and changes nothing.
    const char* before = nullptr;
    REQUIRE(facetry_session_save(c.ctx, session, &before) == FACETRY_OK);
    const std::string saved = before;
    CHECK(facetry_session_apply(c.ctx, session,
                                R"({"op": "setParameter", "name": "missing", "value": 1})") ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(facetry_last_error_message(c.ctx)).find("missing") != std::string::npos);
    CHECK(facetry_session_apply(c.ctx, session, "not json") == FACETRY_ERROR_INVALID_ARGUMENT);
    const char* after = nullptr;
    REQUIRE(facetry_session_save(c.ctx, session, &after) == FACETRY_OK);
    CHECK(saved == after);
}

TEST_CASE("the progress callback can cancel a regeneration") {
    Context c;
    facetry_handle session = 0;
    REQUIRE(facetry_session_create(c.ctx, example().c_str(), &session) == FACETRY_OK);
    std::vector<uint32_t> calls;
    // Stops once the sketch is done; it reports progress (done = 0) while it finds its profiles.
    const auto stopAfterSketch = [](uint32_t done, uint32_t total, void* data) -> int {
        auto& seen = *static_cast<std::vector<uint32_t>*>(data);
        seen.push_back(done);
        CHECK(total == 3);
        return done >= 1 ? 1 : 0;
    };
    REQUIRE(facetry_session_regenerate(c.ctx, session, stopAfterSketch, &calls) == FACETRY_OK);
    REQUIRE_FALSE(calls.empty());
    CHECK(calls.back() == 1);
    const json stopped = state(c.ctx, session);
    CHECK(stopped["regeneration"]["cancelled"] == true);
    CHECK(stopped["needsRegeneration"] == true);
    CHECK(stopped["features"][1]["status"] == "notRun");

    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    const json resumed = state(c.ctx, session);
    CHECK(resumed["regeneration"]["executed"] == 2);
    CHECK(resumed["needsRegeneration"] == false);
}

TEST_CASE("session calls validate their arguments") {
    Context c;
    facetry_handle session = 0;
    REQUIRE(facetry_session_create(c.ctx, nullptr, &session) == FACETRY_OK);
    CHECK(state(c.ctx, session)["features"].empty());

    facetry_handle other = 0;
    CHECK(facetry_session_create(c.ctx, R"({"format": "nope"})", &other) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_session_create(c.ctx, "{}", nullptr) == FACETRY_ERROR_INVALID_ARGUMENT);

    facetry_handle shape = 0;
    CHECK(facetry_session_body_shape(c.ctx, session, "f2", &shape) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_session_body_shape(c.ctx, session, nullptr, &shape) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_session_apply(c.ctx, session, nullptr) == FACETRY_ERROR_INVALID_ARGUMENT);
    const char* text = nullptr;
    CHECK(facetry_session_make_reference(c.ctx, session, "f2", static_cast<facetry_element_kind>(7),
                                         1, &text) == FACETRY_ERROR_INVALID_ARGUMENT);

    // Handles of one kind are not accepted as another.
    facetry_handle box = 0;
    REQUIRE(facetry_shape_make_box(c.ctx, 1, 1, 1, &box) == FACETRY_OK);
    CHECK(facetry_session_state(c.ctx, box, &text) == FACETRY_ERROR_INVALID_HANDLE);
    CHECK(facetry_shape_release(c.ctx, session) == FACETRY_ERROR_INVALID_HANDLE);
    CHECK(facetry_session_release(c.ctx, box) == FACETRY_ERROR_INVALID_HANDLE);
    CHECK(facetry_session_release(c.ctx, session) == FACETRY_OK);
    CHECK(facetry_session_release(c.ctx, session) == FACETRY_ERROR_INVALID_HANDLE);
}

TEST_CASE("sketch tools draw through the C API") {
    Context c;
    facetry_handle session = 0;
    REQUIRE(facetry_session_create(c.ctx, example().c_str(), &session) == FACETRY_OK);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    REQUIRE(facetry_session_begin_transaction(c.ctx, session, "Sketch") == FACETRY_OK);
    facetry_handle tool = 0;
    REQUIRE(facetry_sketch_tool_create(c.ctx, "f1", FACETRY_TOOL_CIRCLE_CENTER, 0, &tool) ==
            FACETRY_OK);
    int changed = -1;
    const auto pointer = [&](uint32_t event, double u, double v) {
        REQUIRE(facetry_sketch_tool_pointer(c.ctx, session, tool, event, u, v, 0.1, &changed) ==
                FACETRY_OK);
    };
    pointer(FACETRY_POINTER_DOWN, 5, 3);
    pointer(FACETRY_POINTER_UP, 5, 3);
    CHECK(changed == 0);
    pointer(FACETRY_POINTER_MOVE, 6, 3);
    facetry_handle preview = 0;
    REQUIRE(facetry_sketch_tool_preview(c.ctx, session, tool, 0.01, &preview) == FACETRY_OK);
    const void* ids = nullptr;
    size_t count = 0;
    REQUIRE(facetry_mesh_buffer(c.ctx, preview, FACETRY_MESH_EDGE_IDS, &ids, &count) == FACETRY_OK);
    CHECK(count == 1);
    REQUIRE(facetry_mesh_release(c.ctx, preview) == FACETRY_OK);
    pointer(FACETRY_POINTER_UP, 6, 3);
    CHECK(changed == 1);
    pointer(FACETRY_POINTER_MOVE, 5.05, 3);
    const char* feedback = nullptr;
    REQUIRE(facetry_sketch_tool_feedback(c.ctx, tool, &feedback) == FACETRY_OK);
    CHECK(json::parse(feedback)["snap"] == "center");  // the circle's center just drawn
    REQUIRE(facetry_sketch_tool_set_grid(c.ctx, tool, 2.0) == FACETRY_OK);
    int busy = -1;
    REQUIRE(facetry_sketch_tool_cancel(c.ctx, tool, &busy) == FACETRY_OK);
    CHECK(busy == 0);
    REQUIRE(facetry_session_end_transaction(c.ctx, session, 1) == FACETRY_OK);
    CHECK(state(c.ctx, session)["undoName"] == "Sketch");
    REQUIRE(facetry_sketch_tool_release(c.ctx, tool) == FACETRY_OK);
    CHECK(facetry_sketch_tool_release(c.ctx, tool) == FACETRY_ERROR_INVALID_HANDLE);
    CHECK(facetry_sketch_tool_create(c.ctx, "f1", 99, 0, &tool) == FACETRY_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("sketch editing tools work through the C API") {
    Context c;
    facetry_handle session = 0;
    REQUIRE(facetry_session_create(c.ctx, example().c_str(), &session) == FACETRY_OK);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    const double volume = bodyVolume(c.ctx, session, "f2");
    facetry_handle tool = 0;
    REQUIRE(facetry_sketch_tool_create(c.ctx, "f1", FACETRY_TOOL_MOVE, 0, &tool) == FACETRY_OK);
    CHECK(facetry_sketch_tool_set_selection(c.ctx, tool, "[\"l1\", 2]") ==
          FACETRY_ERROR_INVALID_ARGUMENT);
    CHECK(facetry_sketch_tool_set_selection(c.ctx, tool, "nope") == FACETRY_ERROR_INVALID_ARGUMENT);
    REQUIRE(facetry_sketch_tool_set_selection(c.ctx, tool, R"(["l1", "l2", "l3", "l4"])") ==
            FACETRY_OK);
    int changed = -1;
    const auto click = [&](double u, double v) {
        REQUIRE(facetry_sketch_tool_pointer(c.ctx, session, tool, FACETRY_POINTER_DOWN, u, v, 0.1,
                                            &changed) == FACETRY_OK);
        REQUIRE(facetry_sketch_tool_pointer(c.ctx, session, tool, FACETRY_POINTER_UP, u, v, 0.1,
                                            &changed) == FACETRY_OK);
    };
    click(0, 0);
    CHECK(changed == 0);
    click(2, 3);
    CHECK(changed == 1);
    const json moved = state(c.ctx, session);
    CHECK(moved["features"][0]["definition"]["points"][0]["x"] == 2.0);
    REQUIRE(facetry_session_regenerate(c.ctx, session, nullptr, nullptr) == FACETRY_OK);
    CHECK(bodyVolume(c.ctx, session, "f2") == doctest::Approx(volume));
    const char* feedback = nullptr;
    REQUIRE(facetry_sketch_tool_feedback(c.ctx, tool, &feedback) == FACETRY_OK);
    CHECK(json::parse(feedback)["radius"] == 0.0);
    REQUIRE(facetry_sketch_tool_release(c.ctx, tool) == FACETRY_OK);
    CHECK(facetry_sketch_tool_create(c.ctx, "f1", FACETRY_TOOL_COPY + 1, 0, &tool) ==
          FACETRY_ERROR_INVALID_ARGUMENT);
}
