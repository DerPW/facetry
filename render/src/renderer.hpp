// GPU side of the viewport (sokol_gfx). Internal to facetry-render; shells use facetry_render.h.
#pragma once

#include "facetry/render/camera.hpp"
#include "facetry/render/pick.hpp"
#include "facetry/render/selection.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace facetry::render {

struct Highlight {
    PickKind hoverKind = PickKind::None;
    std::uint32_t hoverId = 0;
};

// How bodies are drawn (facetry_visual_style).
enum class VisualStyle : std::uint8_t {
    Shaded = 0,             // faces only; hovered and selected edges and vertices still show
    ShadedEdges = 1,        // faces, edges and vertices
    ShadedHiddenEdges = 2,  // and the edges behind faces, dashed
    Wireframe = 3,          // edges and vertices only
};

struct FrameTarget {
    const void* metalDrawable = nullptr;
    const void* metalDepthStencilTexture = nullptr;
    const void* metalMsaaColorTexture = nullptr;
};

struct FrameStats {
    float cpuMilliseconds = 0.0F;
    std::uint32_t triangles = 0;
    std::uint32_t edgeSegments = 0;
    std::uint32_t vertices = 0;
};

class Renderer {
  public:
    // Throws std::runtime_error if the graphics device or context cannot be set up.
    static std::unique_ptr<Renderer> createMetal(const void* device, int sampleCount);
    static std::unique_ptr<Renderer> createWebGL(const char* canvasSelector, bool antialias);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void setMesh(const RenderMesh& mesh);
    // Sketch regions (profiles) to choose from: drawn over the model and picked before it, with
    // the triangles' face IDs as region IDs. An empty mesh hides them.
    void setRegions(const RenderMesh& mesh);
    // The sketch being edited, drawn over everything: its curves (edge polylines), the ones in
    // `construction` dashed, and its points (vertices) as dots. An empty mesh hides it.
    void setSketch(const RenderMesh& mesh, std::span<const std::uint32_t> construction);
    void setViewport(float width, float height, float pixelRatio);

    Camera& camera() {
        return camera_;
    }
    const PickIndex& pickIndex() const {
        return pick_;
    }

    void orbitBegin(float x, float y);
    void orbit(float dx, float dy);
    // Frames the selected element, or the whole model if nothing is selected.
    void fitSelection();
    // Visual style, and the opacity of the faces (1 opaque).
    void setStyle(VisualStyle style, float opacity);
    // Picking reaches hidden edges and vertices when faces do not hide them.
    [[nodiscard]] bool picksThroughFaces() const;

    // The ground grid; `unitMillimeters` is the size of one document unit (25.4 for inches).
    void setGrid(bool visible, float unitMillimeters) {
        gridVisible_ = visible;
        gridUnit_ = unitMillimeters;
    }

    Highlight& highlight() {
        return highlight_;
    }
    Selection& selection() {
        return selection_;
    }
    // What picking and hovering look for.
    void setPickFilter(PickFilter filter) {
        filter_ = filter;
    }
    [[nodiscard]] PickResult pick(float x, float y, float radius) const;
    // Selects what a click at (x, y) picks and returns it. Clicking the same spot again, with the
    // selection unchanged since, picks the next candidate there. `toggle` adds or removes the
    // first candidate instead.
    PickResult click(float x, float y, float radius, bool toggle);
    // Selects what lies in the box dragged from (x0, y0) to (x1, y1): a window from left to
    // right, a crossing box from right to left. `add` keeps the current selection. Returns how
    // many elements the box took.
    std::size_t boxSelect(float x0, float y0, float x1, float y1, bool add);

    void draw(const FrameTarget& target);
    [[nodiscard]] FrameStats stats() const {
        return stats_;
    }

  private:
    struct Gpu;
    Renderer(int sampleCount, bool zeroToOneDepth);
    void updateSelectionFlags();

    std::unique_ptr<Gpu> gpu_;
    Camera camera_;
    PickIndex pick_;
    PickIndex regions_;
    Vec3 orbitPivot_;
    Highlight highlight_;
    Selection selection_;
    std::uint64_t shownSelection_ = ~std::uint64_t{0};
    PickFilter filter_ = PickFilter::Everything;
    ClickCycle clickCycle_;
    FrameStats stats_;
    float pixelRatio_ = 1.0F;
    std::chrono::steady_clock::time_point lastDraw_;
    VisualStyle style_ = VisualStyle::ShadedEdges;
    float opacity_ = 1.0F;
    bool gridVisible_ = true;
    float gridUnit_ = 1.0F;
    int sampleCount_ = 1;
    bool zeroToOneDepth_ = false;
};

}  // namespace facetry::render
