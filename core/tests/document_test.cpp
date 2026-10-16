// The document model, its JSON form, undo/redo and the regeneration engine.
#include "facetry/core/error.hpp"

#include "corner_fillet.hpp"

#include <doctest.h>

#include <filesystem>
#include <fstream>
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

TEST_CASE("the checked-in example document still loads and regenerates") {
    // Written by an earlier version of this code; a format change must keep it readable.
    const std::string saved =
        readFile(std::filesystem::path(FACETRY_DOCUMENTS_DIR) / "corner-fillet.json");
    REQUIRE_FALSE(saved.empty());
    CHECK(toJson(parseDocument(saved)) == saved);
    Session session = Session::load(saved);
    expectRoundAt(session.regenerate(), kFillet, kWidth, kDepth, kHeight, kRadius,
                  kWidth * kDepth * kHeight);
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
    CHECK(errorCode(load(replaced("\"formatVersion\": 1", "\"formatVersion\": 2"))) ==
          ErrorCode::Unsupported);
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

    // An open sketch fails; the extrude has no profile and fails; the fillet has no body.
    session.edit([](Document& document) {
        document.root.timeline[2].suppressed = false;
        std::get<Sketch>(document.root.timeline[0].data).lines.pop_back();
    });
    const Regeneration& failed = session.regenerate();
    CHECK(statuses(failed) ==
          std::vector{FeatureStatus::Error, FeatureStatus::Error, FeatureStatus::Error});
    CHECK(failed.model.bodies.empty());
    for (const FeatureResult& result : failed.features) {
        CHECK_FALSE(result.messages.empty());
    }

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
            cancel.cancel();  // as if the user pressed Cancel while the sketch ran
        });
    CHECK(stopped.cancelled);
    CHECK(stopped.executed == 1);
    CHECK(progress == std::vector<std::size_t>{1});
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
