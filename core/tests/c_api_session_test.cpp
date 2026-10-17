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
    const auto stopAfterFirst = [](uint32_t done, uint32_t total, void* data) -> int {
        auto& seen = *static_cast<std::vector<uint32_t>*>(data);
        seen.push_back(done);
        CHECK(total == 3);
        return 1;
    };
    REQUIRE(facetry_session_regenerate(c.ctx, session, stopAfterFirst, &calls) == FACETRY_OK);
    CHECK(calls == std::vector<uint32_t>{1});
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
