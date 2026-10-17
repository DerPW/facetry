#include "facetry/render/selection.hpp"

#include "test_mesh.hpp"

#include <doctest.h>

using namespace facetry::render;

TEST_CASE("selections replace, toggle and clear") {
    Selection selection;
    const Element edge{PickKind::Edge, 3};
    const Element face{PickKind::Face, 2};
    selection.replace(edge);
    CHECK(selection.elements() == std::vector{edge});
    selection.toggle(face);
    CHECK(selection.elements() == std::vector{edge, face});
    selection.toggle(edge);  // clicked again: removed
    CHECK(selection.elements() == std::vector{face});
    const auto version = selection.version();
    selection.toggle({});  // clicking empty space with the modifier keeps the selection
    CHECK(selection.version() == version);
    selection.replace({});  // without it, clears
    CHECK(selection.elements().empty());
}

TEST_CASE("a selected body selects its faces") {
    Selection selection;
    selection.replace({PickKind::Body, 1});
    CHECK(selection.contains({PickKind::Face, 4}));
    CHECK_FALSE(selection.contains({PickKind::Edge, 4}));
}

TEST_CASE("pick filters restrict what is picked") {
    const PickIndex index(test::unitCube());
    Camera camera;
    camera.setViewport(400, 400);
    camera.setSceneBounds(index.bounds());
    camera.lookAt({0.5F, -4.0F, 1.5F}, {0.5F, 0.5F, 0.5F});
    // Near the middle of the front top edge: an edge with everything.
    const auto onEdge = camera.project({0.5F, 0.0F, 1.0F});
    CHECK(index.pick(camera, onEdge.x, onEdge.y, 6).kind == PickKind::Edge);
    // The same spot with the face filter: the face below the pointer.
    const auto face = index.pick(camera, onEdge.x, onEdge.y + 3, 6, false, PickFilter::Faces);
    CHECK(face.kind == PickKind::Face);
    CHECK(index.pick(camera, onEdge.x, onEdge.y + 3, 6, false, PickFilter::Bodies).kind ==
          PickKind::Body);
    // Vertices only: nothing near the middle of an edge.
    CHECK(index.pick(camera, onEdge.x, onEdge.y, 6, false, PickFilter::Vertices).kind ==
          PickKind::None);
    const auto corner = camera.project({0.0F, 0.0F, 1.0F});
    CHECK(index.pick(camera, corner.x, corner.y, 6, false, PickFilter::Vertices).kind ==
          PickKind::Vertex);
    // Edges only, away from the edges: nothing.
    const auto middle = camera.project({0.5F, 0.0F, 0.5F});
    CHECK(index.pick(camera, middle.x, middle.y, 6, false, PickFilter::Edges).kind ==
          PickKind::None);
}
