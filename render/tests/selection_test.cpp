#include "facetry/render/selection.hpp"

#include "test_mesh.hpp"

#include <doctest.h>

#include <algorithm>

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

namespace {

struct BoxScene {
    PickIndex index{test::unitCube()};
    Camera camera;

    BoxScene() {
        camera.setViewport(400, 400);
        camera.setSceneBounds(index.bounds());
        // Slightly from above: the front and top faces show; the back bottom edge (2) hides.
        camera.lookAt({0.5F, -4.0F, 1.5F}, {0.5F, 0.5F, 0.5F});
    }

    [[nodiscard]] std::vector<Element> box(float x0, float y0, float x1, float y1, BoxMode mode,
                                           PickFilter filter, bool throughFaces = false) const {
        return index.boxSelect(camera, x0, y0, x1, y1, mode, filter, throughFaces);
    }
};

bool has(const std::vector<Element>& elements, Element element) {
    return std::find(elements.begin(), elements.end(), element) != elements.end();
}

}  // namespace

TEST_CASE("a window around the model takes its visible edges, through faces all of them") {
    const BoxScene scene;
    const auto edges = scene.box(0, 0, 400, 400, BoxMode::Window, PickFilter::Everything);
    CHECK(edges.size() >= 5);
    CHECK(edges.size() < 12);
    CHECK_FALSE(has(edges, {PickKind::Edge, 2}));  // hidden behind the front face
    for (const Element& edge : edges) {
        CHECK(edge.kind == PickKind::Edge);
    }
    CHECK(scene.box(0, 0, 400, 400, BoxMode::Window, PickFilter::Edges, true).size() == 12);
    CHECK(scene.box(0, 0, 400, 400, BoxMode::Window, PickFilter::Vertices, true).size() == 8);
}

TEST_CASE("a crossing box takes what it touches, a window only what lies inside") {
    const BoxScene scene;
    // A small box over the middle of the front top edge.
    const auto middle = scene.camera.project({0.5F, 0.0F, 1.0F});
    const auto edge = scene.index.pick(scene.camera, middle.x, middle.y, 3);
    REQUIRE(edge.kind == PickKind::Edge);
    const auto crossing = scene.box(middle.x + 3, middle.y - 3, middle.x - 3, middle.y + 3,
                                    BoxMode::Crossing, PickFilter::Edges);
    CHECK(crossing == std::vector<Element>{{PickKind::Edge, edge.id}});
    CHECK(scene
              .box(middle.x - 3, middle.y - 3, middle.x + 3, middle.y + 3, BoxMode::Window,
                   PickFilter::Edges)
              .empty());
    // The same box touches the front and top faces, and the body.
    const auto faces = scene.box(middle.x + 3, middle.y - 3, middle.x - 3, middle.y + 3,
                                 BoxMode::Crossing, PickFilter::Faces);
    CHECK(faces.size() == 2);
    CHECK(scene.box(middle.x + 3, middle.y - 3, middle.x - 3, middle.y + 3, BoxMode::Crossing,
                    PickFilter::Bodies) == std::vector<Element>{{PickKind::Body, 1}});
    CHECK(scene
              .box(middle.x - 3, middle.y - 3, middle.x + 3, middle.y + 3, BoxMode::Window,
                   PickFilter::Bodies)
              .empty());
}

TEST_CASE("a window around the model takes the visible faces") {
    const BoxScene scene;
    const auto faces = scene.box(0, 0, 400, 400, BoxMode::Window, PickFilter::Faces);
    CHECK(faces.size() == 2);  // front (3) and top (6)
    CHECK(has(faces, {PickKind::Face, 3}));
    CHECK(has(faces, {PickKind::Face, 6}));
}

TEST_CASE("candidates list everything under the pointer, best first") {
    const BoxScene scene;
    // At the front top left corner: the vertex, then its edges, then the faces along the ray.
    const auto corner = scene.camera.project({0.0F, 0.0F, 1.0F});
    const auto all = scene.index.candidates(scene.camera, corner.x, corner.y, 8);
    REQUIRE(all.size() >= 3);
    CHECK(all[0].kind == PickKind::Vertex);
    CHECK(all[1].kind == PickKind::Edge);
    const auto first = scene.index.pick(scene.camera, corner.x, corner.y, 8);
    CHECK(first.kind == all[0].kind);
    CHECK(first.id == all[0].id);
    // In the middle of the front face: the front face, then the back face behind it.
    const auto middle = scene.camera.project({0.5F, 0.0F, 0.5F});
    const auto faces = scene.index.candidates(scene.camera, middle.x, middle.y, 6);
    REQUIRE(faces.size() == 2);
    CHECK(faces[0].id == 3);  // -Y, the front
    CHECK(faces[1].id == 4);  // +Y, hidden behind it
}

TEST_CASE("clicking the same spot again cycles through the candidates") {
    ClickCycle cycle;
    Selection selection;
    const auto click = [&](float x, float y, std::size_t count) {
        const std::size_t index = cycle.choose(x, y, count, selection.version());
        selection.replace({PickKind::Face, static_cast<std::uint32_t>(index + 1)});
        cycle.chose(x, y, index, selection.version());
        return index;
    };
    CHECK(click(100, 100, 3) == 0);
    CHECK(click(101, 99, 3) == 1);  // the same spot
    CHECK(click(100, 100, 3) == 2);
    CHECK(click(100, 100, 3) == 0);  // around again
    CHECK(click(200, 100, 3) == 0);  // elsewhere: the first again
    CHECK(click(200, 100, 3) == 1);
    selection.toggle({PickKind::Edge, 7});  // the selection changed in between
    CHECK(click(200, 100, 3) == 0);
}
