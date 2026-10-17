// JSON edits and session summaries (core/include/facetry/core/session_json.hpp).
#include "facetry/core/session_json.hpp"

#include "facetry/core/error.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>
#include <nlohmann/json.hpp>

#include <string>

using namespace facetry::core;
using namespace facetry::test;
using nlohmann::json;

namespace {

void applyJson(Session& session, const std::string& edits) {
    session.edit([&](Document& document) { applyEdits(document, edits); });
}

std::vector<std::string> featureIds(const Session& session) {
    std::vector<std::string> ids;
    for (const Feature& feature : session.document().root.timeline) {
        ids.push_back(feature.id);
    }
    return ids;
}

const std::string kExtrude = R"({"type": "extrude", "profile": "f1", "distance": 2})";

}  // namespace

TEST_CASE("parameter edits") {
    Session session(rectangleBlock());
    applyJson(session, R"({"op": "setParameter", "name": "width", "value": 12})");
    CHECK(session.document().findParameter("width")->value == 12);
    applyJson(session, R"({"op": "addParameter", "name": "wall", "value": 2})");
    CHECK(session.document().findParameter("wall")->value == 2);
    applyJson(session, R"({"op": "deleteParameter", "name": "wall"})");
    CHECK(session.document().findParameter("wall") == nullptr);
    CHECK_THROWS_AS(applyJson(session, R"({"op": "addParameter", "name": "width", "value": 1})"),
                    Error);
    CHECK_THROWS_AS(applyJson(session, R"({"op": "setParameter", "name": "width", "value": "x"})"),
                    Error);
}

TEST_CASE("new features get an ID and a name and go to the rollback marker") {
    Session session(rectangleBlock());
    applyJson(session, R"({"op": "addFeature", "feature": )" + kExtrude + "}");
    const Feature& added = session.document().root.timeline.back();
    CHECK(added.id == "f5");
    CHECK(added.name == "Extrude 2");
    CHECK_FALSE(added.suppressed);
    CHECK(added.namingVersion == kNamingVersion);

    // With the marker after the sketch, a new feature lands there and the marker moves past it.
    applyJson(session, R"({"op": "setRollback", "index": 1})");
    applyJson(session, R"({"op": "addFeature", "feature": )" + kExtrude + "}");
    CHECK(featureIds(session) == std::vector<std::string>{"f1", "f6", "f2", "f5"});
    CHECK(session.document().rollback == 2u);

    applyJson(session, R"({"op": "addFeature", "index": 0, "feature": {"type": "sketch",
                       "points": [], "lines": []}})");
    CHECK(featureIds(session).front() == "f7");
    CHECK(session.document().rollback == 3u);

    applyJson(session, R"({"op": "setRollback", "index": null})");
    CHECK_FALSE(session.document().rollback);
    CHECK_THROWS_AS(
        applyJson(session, R"({"op": "addFeature", "index": 9, "feature": )" + kExtrude + "}"),
        Error);
    CHECK_THROWS_AS(applyJson(session, R"({"op": "setRollback", "index": 9})"), Error);
}

TEST_CASE("features are replaced, renamed, suppressed and deleted by ID") {
    Session session = cornerFilletSession();
    applyJson(session, R"({"op": "replaceFeature", "feature": {"id": "f2", "type": "extrude",
                       "profile": "f1", "distance": 7}})");
    const Feature& extrude = *session.document().findFeature("f2");
    CHECK(extrude.name == "Extrude 1");  // kept
    CHECK(std::get<Extrude>(extrude.data).distance == Value(7.0));

    applyJson(session, R"([{"op": "renameFeature", "id": "f2", "name": "Block"},
                       {"op": "suppressFeature", "id": "f5", "suppressed": true}])");
    CHECK(session.document().findFeature("f2")->name == "Block");
    CHECK(session.document().findFeature(kFillet)->suppressed);

    applyJson(session, R"({"op": "setRollback", "index": 3})");
    applyJson(session, R"({"op": "deleteFeature", "id": "f2"})");
    CHECK(featureIds(session) == std::vector<std::string>{"f1", "f5"});
    CHECK(session.document().rollback == 2u);

    CHECK_THROWS_AS(applyJson(session, R"({"op": "deleteFeature", "id": "f9"})"), Error);
    CHECK_THROWS_AS(
        applyJson(session, R"({"op": "replaceFeature", "feature": {"type": "sketch"}})"), Error);
    CHECK_THROWS_AS(applyJson(session, R"({"op": "explode"})"), Error);
    CHECK_THROWS_AS(applyJson(session, "[1]"), Error);
}

TEST_CASE("a failing edit in an array leaves the document unchanged") {
    Session session(rectangleBlock());
    const Document before = session.document();
    CHECK_THROWS_AS(applyJson(session, R"([{"op": "setParameter", "name": "width", "value": 20},
                                       {"op": "deleteFeature", "id": "f9"}])"),
                    Error);
    CHECK(session.document() == before);
    CHECK_FALSE(session.canUndo());
}

TEST_CASE("a definition from the summary can be edited and sent back") {
    Session session = cornerFilletSession();
    json definition = json::parse(sessionStateJson(session))["features"][1]["definition"];
    definition["distance"] = 7;
    applyJson(session, json({{"op", "replaceFeature"}, {"feature", definition}}).dump());
    CHECK(std::get<Extrude>(session.document().findFeature("f2")->data).distance == Value(7.0));
}

TEST_CASE("the session summary reflects document and regeneration") {
    Session session = cornerFilletSession();
    json state = json::parse(sessionStateJson(session));
    CHECK(state["needsRegeneration"] == true);
    CHECK(state["features"][2]["status"] == "notRun");
    CHECK(state["canUndo"] == true);  // adding the fillet

    session.regenerate();
    state = json::parse(sessionStateJson(session));
    CHECK(state["needsRegeneration"] == false);
    CHECK(state["bodies"] == json::array({{{"id", "f2"}, {"name", "Body 1"}, {"visible", true}}}));
    json fillet = state["features"][2];
    CHECK(fillet["definition"]["radius"] == "radius");
    CHECK(fillet["definition"]["id"] == kFillet);
    fillet.erase("definition");
    CHECK(fillet == json({{"id", kFillet},
                          {"name", "Fillet 1"},
                          {"type", "fillet"},
                          {"suppressed", false},
                          {"status", "ok"},
                          {"messages", json::array()},
                          {"brokenReferences", json::array()},
                          {"dependents", json::array()}}));
    // The sketch feeds the extrude, which the fillet changes: deleting the sketch breaks both.
    CHECK(state["features"][0]["dependents"] == json::array({"f2", kFillet}));
    CHECK(state["features"][1]["dependents"] == json::array({kFillet}));

    const json names = json::parse(elementNamesJson(*session.regeneration().model.bodies.at("f2")));
    CHECK(names["faces"].size() == 7);
    CHECK(std::find(names["faces"].begin(), names["faces"].end(),
                    "f5:fillet[edge{f2:side[f1/l2]|f2:side[f1/l3]}]") != names["faces"].end());
}

TEST_CASE("bodies can be renamed and hidden, origin items shown") {
    Session session = cornerFilletSession();
    const std::string before = session.save();
    applyJson(session, R"([{"op": "renameBody", "id": "f2", "name": "Block"},
                           {"op": "setBodyVisible", "id": "f2", "visible": false},
                           {"op": "setOriginVisible", "id": "xy", "visible": true}])");
    session.regenerate();
    json state = json::parse(sessionStateJson(session));
    CHECK(state["bodies"] == json::array({{{"id", "f2"}, {"name", "Block"}, {"visible", false}}}));
    CHECK(state["origin"].size() == 7);
    CHECK(state["origin"][0] ==
          json({{"id", "xy"}, {"name", "XY plane"}, {"kind", "plane"}, {"visible", true}}));
    CHECK(state["origin"][6]["kind"] == "point");

    // Settings survive saving and loading.
    const Document reloaded = parseDocument(session.save());
    CHECK(reloaded == session.document());
    CHECK(reloaded.bodyName("f2") == "Block");

    // Back to the defaults: the document is as before.
    applyJson(session, R"([{"op": "renameBody", "id": "f2", "name": ""},
                           {"op": "setBodyVisible", "id": "f2", "visible": true},
                           {"op": "setOriginVisible", "id": "xy", "visible": false}])");
    CHECK(session.save() == before);

    CHECK_THROWS_AS(applyJson(session, R"({"op": "renameBody", "id": "f1", "name": "x"})"), Error);
    CHECK_THROWS_AS(applyJson(session, R"({"op": "setOriginVisible", "id": "w", "visible": true})"),
                    Error);
}
