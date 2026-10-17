// The document model, its JSON form, undo/redo and the regeneration engine.
#include "facetry/core/error.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace facetry::core;
using namespace facetry::test;

namespace {

ErrorCode errorCode(const auto& action) {
    try {
        action();
    } catch (const Error& error) {
        return error.code();
    }
    FAIL("expected an error");
    return ErrorCode::Internal;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<FeatureStatus> statuses(const Regeneration& regeneration) {
    std::vector<FeatureStatus> result;
    for (const FeatureResult& feature : regeneration.features) {
        result.push_back(feature.status);
    }
    return result;
}

}  // namespace

TEST_CASE("documents round-trip through JSON") {
    Session session = cornerFilletSession();
    session.edit([](Document& document) {
        document.root.timeline[1].suppressed = true;
        document.rollback = 2;
    });
    const std::string saved = session.save();
    const Document loaded = parseDocument(saved);
    CHECK(loaded == session.document());
    CHECK(toJson(loaded) == saved);
    CHECK(saved.find("\"format\": \"facetry-document\"") != std::string::npos);
}

TEST_CASE("the checked-in example documents still load and regenerate") {
    // Written by earlier versions of this code; a format change must keep them readable.
    const auto example = [](const char* name) {
        std::string saved = readFile(std::filesystem::path(FACETRY_DOCUMENTS_DIR) / name);
        REQUIRE_FALSE(saved.empty());
        return saved;
    };
    // Format version 1 has no units: millimeters.
    const std::string version1 = example("corner-fillet.json");
    const Document migrated = parseDocument(version1);
    CHECK(migrated.units == LengthUnit::Millimeter);
    CHECK(parseDocument(toJson(migrated)) == migrated);
    Session session = Session::load(version1);
    expectRoundAt(session.regenerate(), kFillet, kWidth, kDepth, kHeight, kRadius,
                  kWidth * kDepth * kHeight);

    // Version 2, in inches: 1 × 0.5 × 0.25 in, rounded by 0.125 in. Its sketch moves to XY.
    const std::string inches = example("corner-fillet-inch.json");
    const Document inchDocument = parseDocument(inches);
    CHECK(std::get<Sketch>(inchDocument.root.timeline[0].data).plane == "xy");
    CHECK(parseDocument(toJson(inchDocument)) == inchDocument);
    Session inchSession = Session::load(inches);
    CHECK(inchSession.document().units == LengthUnit::Inch);
    expectRoundAt(inchSession.regenerate(), kFillet, 25.4, 12.7, 6.35, 3.175, 25.4 * 12.7 * 6.35);

    // Version 3: a 20 × 10 slot of arcs and lines on XZ, 8 deep; a boss of radius 3 on its end
    // face, 4 high; a pin of radius 2 on YZ, 30 long, with a construction circle around it.
    const std::string sketches = example("sketch-planes.json");
    CHECK(toJson(parseDocument(sketches)) == sketches);
    Session sketchSession = Session::load(sketches);
    const Regeneration& regenerated = sketchSession.regenerate();
    CHECK(statuses(regenerated) == std::vector<FeatureStatus>(6, FeatureStatus::Ok));
    const auto volume = [&](const char* body) {
        return regenerated.model.bodies.at(body)->shape.properties().volume;
    };
    const double pi = std::numbers::pi;
    CHECK(volume("f2") == doctest::Approx((20 * 10 + pi * 25) * 8));
    CHECK(volume("f4") == doctest::Approx(pi * 9 * 4));
    CHECK(volume("f6") == doctest::Approx(pi * 4 * 30));
    const BoundingBox boss = regenerated.model.bodies.at("f4")->shape.properties().bounds;
    CHECK(boss.min.y == doctest::Approx(-12));
    CHECK(boss.max.y == doctest::Approx(-8));

    // Also version 3: a 40 × 30 plate, 5 thick, extruded from the region around a circle of
    // radius 8 (ADR-0014), so it has a hole.
    const std::string plate = example("plate-with-hole.json");
    CHECK(toJson(parseDocument(plate)) == plate);
    Session plateSession = Session::load(plate);
    const Regeneration& holed = plateSession.regenerate();
    CHECK(statuses(holed) == std::vector<FeatureStatus>(2, FeatureStatus::Ok));
    CHECK(holed.model.bodies.at("f2")->shape.properties().volume ==
          doctest::Approx((40 * 30 - pi * 64) * 5));
}

TEST_CASE("a document in inches regenerates the same geometry as its millimeter equivalent") {
    Document inches = Session::load(readFile(std::filesystem::path(FACETRY_DOCUMENTS_DIR) /
                                             "corner-fillet-inch.json"))
                          .document();
    Document millimeters = inches;
    convertUnits(millimeters, LengthUnit::Millimeter);
    CHECK(millimeters.units == LengthUnit::Millimeter);
    CHECK(millimeters.findParameter("width")->value == doctest::Approx(25.4));
    CHECK(millimeters.findParameter("radius")->value == doctest::Approx(3.175));

    Session inchSession(inches);
    Session millimeterSession(millimeters);
    const auto& inchBody = *inchSession.regenerate().model.bodies.at("f2");
    const auto& millimeterBody = *millimeterSession.regenerate().model.bodies.at("f2");
    CHECK(inchBody.shape.properties().volume ==
          doctest::Approx(millimeterBody.shape.properties().volume));
    CHECK(inchBody.faces == millimeterBody.faces);
}

TEST_CASE("changing units converts every length and keeps the geometry") {
    for (const LengthUnit unit : {LengthUnit::Centimeter, LengthUnit::Meter, LengthUnit::Inch}) {
        CAPTURE(unitSymbol(unit));
        Session session = cornerFilletSession();
        session.edit([&](Document& document) {
            // Literals as well as parameters.
            std::get<Extrude>(document.root.timeline[1].data).distance = kHeight;
            convertUnits(document, unit);
        });
        const Document& converted = session.document();
        CHECK(converted.units == unit);
        CHECK(converted.findParameter("width")->value ==
              doctest::Approx(kWidth / millimetersPer(unit)));
        CHECK(std::get<Extrude>(converted.root.timeline[1].data).distance.number ==
              doctest::Approx(kHeight / millimetersPer(unit)));
        expectRoundAt(session.regenerate(), kFillet, kWidth, kDepth, kHeight, kRadius,
                      kWidth * kDepth * kHeight);
        CHECK(parseDocument(toJson(converted)) == converted);
    }
    CHECK(unitFromSymbol("in") == LengthUnit::Inch);
    CHECK_FALSE(unitFromSymbol("ft"));
}

TEST_CASE("malformed and newer documents are rejected") {
    const std::string valid = toJson(rectangleBlock());
    const auto load = [](std::string text) {
        return [text] { static_cast<void>(parseDocument(text)); };
    };
    const auto replaced = [&](const std::string& from, const std::string& to) {
        std::string text = valid;
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        return text.replace(at, from.size(), to);
    };
    CHECK(errorCode(load("not json")) == ErrorCode::InvalidArgument);
    CHECK(errorCode(load(R"({"format": "something-else"})")) == ErrorCode::InvalidArgument);
    CHECK(errorCode(load(replaced("\"formatVersion\": 3", "\"formatVersion\": 4"))) ==
          ErrorCode::Unsupported);
    CHECK(errorCode(load(replaced("\"units\": \"mm\"", "\"units\": \"ft\""))) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode(load(replaced(",\n  \"units\": \"mm\"", ""))) == ErrorCode::InvalidArgument);
    CHECK(errorCode(load(replaced("\"namingVersion\": 1", "\"namingVersion\": 9"))) ==
          ErrorCode::Unsupported);
    CHECK(errorCode(load(replaced("\"type\": \"extrude\"", "\"type\": \"loft\""))) ==
          ErrorCode::Unsupported);
    CHECK(errorCode(load(replaced("\"distance\": \"height\"", "\"distance\": true"))) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode(load(replaced("\"id\": \"f2\"", "\"id\": \"f1\""))) ==
          ErrorCode::InvalidArgument);
}

TEST_CASE("structural rules are checked on every edit") {
    Session session(rectangleBlock());
    const Document before = session.document();
    CHECK(errorCode([&] {
              session.edit([](Document& document) {
                  std::get<Sketch>(document.root.timeline[0].data).lines[0].end = "p9";
              });
          }) == ErrorCode::InvalidArgument);
    CHECK(errorCode([&] { session.edit([](Document& document) { document.rollback = 3; }); }) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode([&] {
              session.edit([](Document& document) { document.parameters.push_back({"width", 1}); });
          }) == ErrorCode::InvalidArgument);
    CHECK(session.document() == before);
    CHECK_FALSE(session.canUndo());
}

TEST_CASE("IDs are never reused") {
    Document document = rectangleBlock();
    const std::string first = document.newId("f");
    CHECK(first == "f5");
    document.root.timeline.push_back(feature(first, "Sketch", Sketch{}));
    document.root.timeline.pop_back();
    CHECK(document.newId("f") == "f6");
    CHECK(document.newId("l") == "l7");
}

TEST_CASE("undo and redo walk the edit history") {
    Session session(rectangleBlock());
    CHECK_FALSE(session.undo());
    setParameter(session, "width", 20);
    setParameter(session, "width", 30);
    CHECK(session.document().findParameter("width")->value == 30);
    REQUIRE(session.undo());
    CHECK(session.document().findParameter("width")->value == 20);
    REQUIRE(session.undo());
    CHECK(session.document().findParameter("width")->value == kWidth);
    CHECK_FALSE(session.canUndo());
    REQUIRE(session.redo());
    CHECK(session.document().findParameter("width")->value == 20);
    // A new edit drops the redo history.
    setParameter(session, "depth", 1);
    CHECK_FALSE(session.canRedo());
}

TEST_CASE("only features after a change run again") {
    Session session = cornerFilletSession();
    CHECK(session.regenerate().executed == 1);
    CHECK(session.regenerate().executed == 0);

    session.edit([](Document& document) { document.root.timeline[2].name = "Rounded corner"; });
    CHECK(session.regenerate().executed == 0);

    setParameter(session, "radius", 1.0);
    const Regeneration& radius = session.regenerate();
    CHECK(radius.executed == 1);
    CHECK(radius.features[0].fromCache);
    CHECK(radius.features[1].fromCache);
    CHECK_FALSE(radius.features[2].fromCache);
    expectRoundAt(radius, kFillet, kWidth, kDepth, kHeight, 1.0, kWidth * kDepth * kHeight);

    setParameter(session, "width", 12);
    CHECK(session.regenerate().executed == 3);
}

TEST_CASE("the rollback marker skips later features and keeps their cache") {
    Session session = cornerFilletSession();
    session.regenerate();
    session.edit([](Document& document) { document.rollback = 2; });
    const Regeneration& rolledBack = session.regenerate();
    CHECK(statuses(rolledBack) ==
          std::vector{FeatureStatus::Ok, FeatureStatus::Ok, FeatureStatus::RolledBack});
    CHECK(roundFace(*rolledBack.model.bodies.at("f2"), kFillet) == 0);
    CHECK(rolledBack.executed == 0);

    session.edit([](Document& document) { document.rollback.reset(); });
    const Regeneration& forward = session.regenerate();
    CHECK(forward.executed == 0);
    expectRoundAt(forward, kFillet, kWidth, kDepth, kHeight, kRadius, kWidth * kDepth * kHeight);
}

TEST_CASE("suppressed and failing features pass the model through") {
    Session session = cornerFilletSession();
    session.edit([](Document& document) { document.root.timeline[2].suppressed = true; });
    const Regeneration& suppressed = session.regenerate();
    CHECK(statuses(suppressed) ==
          std::vector{FeatureStatus::Ok, FeatureStatus::Ok, FeatureStatus::Suppressed});
    CHECK(roundFace(*suppressed.model.bodies.at("f2"), kFillet) == 0);

    // An open sketch is fine on its own, but the extrude finds no closed profile and fails, and
    // the fillet has no body.
    session.edit([](Document& document) {
        document.root.timeline[2].suppressed = false;
        std::get<Sketch>(document.root.timeline[0].data).lines.pop_back();
    });
    const Regeneration& failed = session.regenerate();
    CHECK(statuses(failed) ==
          std::vector{FeatureStatus::Ok, FeatureStatus::Error, FeatureStatus::Error});
    CHECK(failed.model.bodies.empty());
    CHECK(failed.features[1].messages.at(0).find("no closed profile") != std::string::npos);
    CHECK_FALSE(failed.features[2].messages.empty());

    REQUIRE(session.undo());
    REQUIRE(session.undo());
    setParameter(session, "radius", 50.0);  // far too large for the block
    const Regeneration& tooLarge = session.regenerate();
    CHECK(tooLarge.features[2].status == FeatureStatus::Error);
    CHECK(tooLarge.model.bodies.at("f2")->shape.properties().volume ==
          doctest::Approx(kWidth * kDepth * kHeight));

    session.edit([](Document& document) {
        std::get<Extrude>(document.root.timeline[1].data).distance = Value::of("missing");
    });
    CHECK(session.regenerate().features[1].status == FeatureStatus::Error);
}

TEST_CASE("regeneration can be cancelled between features and continues later") {
    Session session = cornerFilletSession();
    setParameter(session, "width", 12);  // nothing is cached any more
    const CancelToken cancel;
    std::vector<std::size_t> progress;
    const Regeneration& stopped =
        session.regenerate(cancel, [&](std::size_t done, std::size_t total) {
            progress.push_back(done);
            CHECK(total == 3);
            if (done == 1) {
                cancel.cancel();  // as if the user pressed Cancel once the sketch was done
            }
        });
    CHECK(stopped.cancelled);
    CHECK(stopped.executed == 1);
    // The sketch reports progress (done = 0) while it finds its profiles, too.
    CHECK(progress.back() == 1);
    CHECK(std::count(progress.begin(), progress.end(), 1) == 1);
    CHECK(statuses(stopped) ==
          std::vector{FeatureStatus::Ok, FeatureStatus::NotRun, FeatureStatus::NotRun});

    const Regeneration& resumed = session.regenerate();
    CHECK_FALSE(resumed.cancelled);
    CHECK(resumed.executed == 2);
    expectRoundAt(resumed, kFillet, 12, kDepth, kHeight, kRadius, 12 * kDepth * kHeight);
}

TEST_CASE("references are created from a selection") {
    Session session(rectangleBlock());
    const Regeneration& regeneration = session.regenerate();
    const NamedShape& body = *regeneration.model.bodies.at("f2");
    const std::uint32_t edge = edgeAt(body, 0, 0);
    const Reference reference = makeReference(regeneration.model, "f2", ElementKind::Edge, edge);
    CHECK(reference.name == "edge{f2:side[f1/l1]|f2:side[f1/l4]}");
    REQUIRE(reference.shadow);
    CHECK(reference.shadow->id == edge);
    CHECK(reference.shadow->geometry.size == doctest::Approx(kHeight));
    CHECK(errorCode([&] {
              static_cast<void>(makeReference(regeneration.model, "f9", ElementKind::Edge, 1));
          }) == ErrorCode::InvalidArgument);
    CHECK(errorCode([&] {
              static_cast<void>(makeReference(regeneration.model, "f2", ElementKind::Face, 99));
          }) == ErrorCode::InvalidArgument);
}
