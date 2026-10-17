// GPU side of the viewport (sokol_gfx). Internal to facetry-render; shells use facetry_render.h.
#pragma once

#include "facetry/render/camera.hpp"
#include "facetry/render/pick.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace facetry::render {

struct Highlight {
    PickKind hoverKind = PickKind::None;
    std::uint32_t hoverId = 0;
    PickKind selectKind = PickKind::None;
    std::uint32_t selectId = 0;
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

    void draw(const FrameTarget& target);
    [[nodiscard]] FrameStats stats() const {
        return stats_;
    }

  private:
    struct Gpu;
    Renderer(int sampleCount, bool zeroToOneDepth);

    std::unique_ptr<Gpu> gpu_;
    Camera camera_;
    PickIndex pick_;
    Vec3 orbitPivot_;
    Highlight highlight_;
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
