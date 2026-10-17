// Implementation of facetry_render.h. Every entry point catches all exceptions.
#include "facetry/render/navigation.hpp"
#include "facetry/render/viewcube.hpp"

#include "facetry_render.h"
#include "renderer.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

using facetry::render::PickKind;
using facetry::render::Renderer;

struct facetry_renderer {
    std::unique_ptr<Renderer> renderer;
    std::string lastError;
};

namespace {

std::string lastCreateError;  // reported through facetry_renderer_last_error(NULL)
// How long a view change (ViewCube, named view, home) takes.
constexpr float kViewSeconds = 0.35F;

template <typename Function>
facetry_render_status run(facetry_renderer* r, Function&& function) {
    if (r == nullptr || !r->renderer) {
        return FACETRY_RENDER_ERROR_INVALID_ARGUMENT;
    }
    try {
        function(*r->renderer);
        r->lastError.clear();
        return FACETRY_RENDER_OK;
    } catch (const std::invalid_argument& error) {
        r->lastError = error.what();
        return FACETRY_RENDER_ERROR_INVALID_ARGUMENT;
    } catch (const std::exception& error) {
        r->lastError = error.what();
        return FACETRY_RENDER_ERROR_INTERNAL;
    } catch (...) {
        r->lastError = "Unknown error";
        return FACETRY_RENDER_ERROR_INTERNAL;
    }
}

template <typename Create>
facetry_render_status create(facetry_renderer** out, Create&& makeRenderer) {
    if (out == nullptr) {
        return FACETRY_RENDER_ERROR_INVALID_ARGUMENT;
    }
    *out = nullptr;
    try {
        auto handle = std::make_unique<facetry_renderer>();
        handle->renderer = makeRenderer();
        *out = handle.release();
        lastCreateError.clear();
        return FACETRY_RENDER_OK;
    } catch (const std::invalid_argument& error) {
        lastCreateError = error.what();
        return FACETRY_RENDER_ERROR_INVALID_ARGUMENT;
    } catch (const std::exception& error) {
        lastCreateError = error.what();
        return FACETRY_RENDER_ERROR_GPU;
    } catch (...) {
        lastCreateError = "Unknown error";
        return FACETRY_RENDER_ERROR_INTERNAL;
    }
}

PickKind toKind(uint32_t kind) {
    return kind <= 7 ? static_cast<PickKind>(kind) : PickKind::None;
}

template <typename T>
std::vector<T> copy(const T* data, uint32_t count) {
    if (count > 0 && data == nullptr) {
        throw std::invalid_argument("Mesh buffer pointer is null");
    }
    return count > 0 ? std::vector<T>(data, data + count) : std::vector<T>{};
}

// Copies and checks a mesh from the C API.
facetry::render::RenderMesh renderMesh(const facetry_render_mesh* mesh) {
    if (mesh == nullptr) {
        throw std::invalid_argument("Mesh must not be null");
    }
    if (mesh->position_count % 3 != 0 || mesh->triangle_index_count % 3 != 0 ||
        mesh->edge_point_count % 3 != 0 || mesh->vertex_position_count % 3 != 0 ||
        mesh->edge_offset_count == 0) {
        throw std::invalid_argument("Mesh buffer sizes are inconsistent");
    }
    facetry::render::RenderMesh m;
    m.positions = copy(mesh->positions, mesh->position_count);
    m.normals = copy(mesh->normals, mesh->position_count);
    m.triangles = copy(mesh->triangles, mesh->triangle_index_count);
    m.triangleFaceIds = copy(mesh->triangle_face_ids, mesh->triangle_index_count / 3);
    m.edgePoints = copy(mesh->edge_points, mesh->edge_point_count);
    m.edgeOffsets = copy(mesh->edge_offsets, mesh->edge_offset_count);
    m.edgeIds = copy(mesh->edge_ids, mesh->edge_offset_count - 1);
    m.vertexPositions = copy(mesh->vertex_positions, mesh->vertex_position_count);
    m.vertexIds = copy(mesh->vertex_ids, mesh->vertex_position_count / 3);
    const auto vertexCount = static_cast<uint32_t>(m.positions.size() / 3);
    for (const uint32_t index : m.triangles) {
        if (index >= vertexCount) {
            throw std::invalid_argument("Triangle index out of range");
        }
    }
    for (const uint32_t offset : m.edgeOffsets) {
        if (offset > m.edgePoints.size() / 3) {
            throw std::invalid_argument("Edge offset out of range");
        }
    }
    return m;
}

}  // namespace

extern "C" {

facetry_render_status facetry_renderer_create_metal(const void* metal_device, int sample_count,
                                                    facetry_renderer** out) {
    return create(out, [&] { return Renderer::createMetal(metal_device, sample_count); });
}

facetry_render_status facetry_renderer_create_webgl(const char* canvas_selector, int antialias,
                                                    facetry_renderer** out) {
    return create(out, [&] { return Renderer::createWebGL(canvas_selector, antialias != 0); });
}

void facetry_renderer_destroy(facetry_renderer* renderer) {
    delete renderer;
}

const char* facetry_renderer_last_error(const facetry_renderer* renderer) {
    return renderer != nullptr ? renderer->lastError.c_str() : lastCreateError.c_str();
}

facetry_render_status facetry_renderer_set_mesh(facetry_renderer* renderer,
                                                const facetry_render_mesh* mesh) {
    return run(renderer, [&](Renderer& r) { r.setMesh(renderMesh(mesh)); });
}

facetry_render_status facetry_renderer_set_regions(facetry_renderer* renderer,
                                                   const facetry_render_mesh* mesh) {
    return run(renderer, [&](Renderer& r) {
        r.setRegions(mesh == nullptr ? facetry::render::RenderMesh{} : renderMesh(mesh));
    });
}

facetry_render_status facetry_renderer_set_sketch(facetry_renderer* renderer,
                                                  const facetry_render_mesh* mesh,
                                                  const uint32_t* construction_edge_ids,
                                                  uint32_t construction_count) {
    return run(renderer, [&](Renderer& r) {
        const auto construction = copy(construction_edge_ids, construction_count);
        r.setSketch(mesh == nullptr ? facetry::render::RenderMesh{} : renderMesh(mesh),
                    construction);
    });
}

facetry_render_status facetry_renderer_set_sketch_preview(facetry_renderer* renderer,
                                                          const facetry_render_mesh* mesh) {
    return run(renderer, [&](Renderer& r) {
        r.setSketchPreview(mesh == nullptr ? facetry::render::RenderMesh{} : renderMesh(mesh));
    });
}

facetry_render_status facetry_renderer_sketch_point(facetry_renderer* renderer, float x, float y,
                                                    const float* plane, float* out) {
    return run(renderer, [&](Renderer& r) {
        if (plane == nullptr || out == nullptr) {
            throw std::invalid_argument("plane and out must not be null");
        }
        const auto point =
            r.camera().planePoint(x, y, {plane[0], plane[1], plane[2]},
                                  {plane[3], plane[4], plane[5]}, {plane[6], plane[7], plane[8]});
        if (!point) {
            throw std::invalid_argument("The sketch plane is seen edge-on or lies behind");
        }
        out[0] = point->u;
        out[1] = point->v;
        out[2] = point->perPoint;
    });
}

facetry_render_status facetry_renderer_project(facetry_renderer* renderer, float x, float y,
                                               float z, float* out) {
    return run(renderer, [&](Renderer& r) {
        if (out == nullptr) {
            throw std::invalid_argument("out must not be null");
        }
        const auto projected = r.camera().project({x, y, z});
        if (projected.w <= 0.0F) {
            throw std::invalid_argument("The point lies behind the camera");
        }
        out[0] = projected.x;
        out[1] = projected.y;
    });
}

float facetry_renderer_grid_spacing(const facetry_renderer* renderer) {
    if (renderer == nullptr) {
        return 0.0F;
    }
    return renderer->renderer->gridSpacing();
}

void facetry_renderer_set_viewport(facetry_renderer* renderer, float width, float height,
                                   float pixel_ratio) {
    run(renderer, [&](Renderer& r) { r.setViewport(width, height, pixel_ratio); });
}

void facetry_renderer_set_style(facetry_renderer* renderer, uint32_t style, float opacity) {
    run(renderer, [&](Renderer& r) {
        if (style > 3) {
            throw std::invalid_argument("Unknown visual style");
        }
        r.setStyle(static_cast<facetry::render::VisualStyle>(style), opacity);
    });
}

void facetry_renderer_set_grid(facetry_renderer* renderer, int visible, float unit_millimeters) {
    run(renderer, [&](Renderer& r) { r.setGrid(visible != 0, unit_millimeters); });
}

void facetry_camera_fit(facetry_renderer* renderer) {
    run(renderer, [](Renderer& r) { r.camera().fit(); });
}

void facetry_camera_fit_selection(facetry_renderer* renderer) {
    run(renderer, [](Renderer& r) { r.fitSelection(); });
}

void facetry_camera_home(facetry_renderer* renderer, int animate) {
    run(renderer, [&](Renderer& r) { r.camera().home(animate != 0 ? kViewSeconds : 0.0F); });
}

void facetry_camera_view_from(facetry_renderer* renderer, float x, float y, float z, int animate) {
    run(renderer,
        [&](Renderer& r) { r.camera().viewFrom({x, y, z}, animate != 0 ? kViewSeconds : 0.0F); });
}

uint32_t facetry_viewcube_cells(const facetry_renderer* renderer, float size,
                                facetry_viewcube_cell* out, uint32_t capacity) {
    if (renderer == nullptr || !renderer->renderer) {
        return 0;
    }
    try {
        const auto cells = facetry::render::viewCubeCells(renderer->renderer->camera(), size);
        const auto count = static_cast<uint32_t>(cells.size());
        for (uint32_t i = 0; out != nullptr && i < count && i < capacity; ++i) {
            const auto& cell = cells[i];
            facetry_viewcube_cell& result = out[i];
            std::copy(cell.points.begin(), cell.points.end(), result.points);
            result.direction[0] = cell.direction.x;
            result.direction[1] = cell.direction.y;
            result.direction[2] = cell.direction.z;
            result.depth = cell.depth;
            result.face = static_cast<uint32_t>(cell.face);
            result.region = cell.region;
        }
        return count;
    } catch (...) {
        return 0;
    }
}

int32_t facetry_viewcube_hit(const facetry_renderer* renderer, float size, float x, float y) {
    if (renderer == nullptr || !renderer->renderer) {
        return -1;
    }
    try {
        return facetry::render::viewCubeHit(renderer->renderer->camera(), size, x, y);
    } catch (...) {
        return -1;
    }
}

void facetry_camera_set_orthographic(facetry_renderer* renderer, int orthographic) {
    run(renderer, [&](Renderer& r) {
        r.camera().setProjection(orthographic != 0
                                     ? facetry::render::Camera::Projection::Orthographic
                                     : facetry::render::Camera::Projection::Perspective);
    });
}

void facetry_camera_orbit_begin(facetry_renderer* renderer, float x, float y) {
    run(renderer, [&](Renderer& r) { r.orbitBegin(x, y); });
}

void facetry_camera_orbit(facetry_renderer* renderer, float dx, float dy) {
    run(renderer, [&](Renderer& r) { r.orbit(dx, dy); });
}

void facetry_camera_pan(facetry_renderer* renderer, float dx, float dy) {
    run(renderer, [&](Renderer& r) { r.camera().pan(dx, dy); });
}

void facetry_camera_zoom(facetry_renderer* renderer, float factor, float x, float y) {
    run(renderer,
        [&](Renderer& r) { r.camera().zoom(factor, r.pickIndex().anchorAt(r.camera(), x, y)); });
}

facetry_render_status facetry_renderer_pick(facetry_renderer* renderer, float x, float y,
                                            float radius, facetry_pick_result* out) {
    return run(renderer, [&](Renderer& r) {
        if (out == nullptr) {
            throw std::invalid_argument("out must not be null");
        }
        const auto result = r.pick(x, y, radius);
        out->kind = static_cast<uint32_t>(result.kind);
        out->id = result.id;
        out->point[0] = result.point.x;
        out->point[1] = result.point.y;
        out->point[2] = result.point.z;
        out->screen_distance = result.screenDistance;
    });
}

void facetry_renderer_set_hover(facetry_renderer* renderer, uint32_t kind, uint32_t id) {
    run(renderer, [&](Renderer& r) {
        r.highlight().hoverKind = toKind(kind);
        r.highlight().hoverId = id;
    });
}

void facetry_renderer_set_selection(facetry_renderer* renderer, uint32_t kind, uint32_t id) {
    run(renderer, [&](Renderer& r) { r.selection().replace({toKind(kind), id}); });
}

void facetry_renderer_select(facetry_renderer* renderer, uint32_t kind, uint32_t id,
                             uint32_t mode) {
    run(renderer, [&](Renderer& r) {
        if (mode == FACETRY_SELECT_TOGGLE) {
            r.selection().toggle({toKind(kind), id});
        } else {
            r.selection().replace({toKind(kind), id});
        }
    });
}

facetry_render_status facetry_renderer_click(facetry_renderer* renderer, float x, float y,
                                             float radius, uint32_t mode,
                                             facetry_pick_result* out) {
    return run(renderer, [&](Renderer& r) {
        const auto result = r.click(x, y, radius, mode == FACETRY_SELECT_TOGGLE);
        if (out != nullptr) {
            out->kind = static_cast<uint32_t>(result.kind);
            out->id = result.id;
            out->point[0] = result.point.x;
            out->point[1] = result.point.y;
            out->point[2] = result.point.z;
            out->screen_distance = result.screenDistance;
        }
    });
}

uint32_t facetry_renderer_box_select(facetry_renderer* renderer, float x0, float y0, float x1,
                                     float y1, uint32_t mode) {
    uint32_t count = 0;
    run(renderer, [&](Renderer& r) {
        count = static_cast<uint32_t>(r.boxSelect(x0, y0, x1, y1, mode != FACETRY_SELECT_REPLACE));
    });
    return count;
}

uint32_t facetry_renderer_selection(const facetry_renderer* renderer, uint32_t* out,
                                    uint32_t capacity) {
    if (renderer == nullptr || !renderer->renderer) {
        return 0;
    }
    const auto& elements = renderer->renderer->selection().elements();
    for (uint32_t i = 0; out != nullptr && i < elements.size() && i < capacity; ++i) {
        out[i * 2] = static_cast<uint32_t>(elements[i].kind);
        out[i * 2 + 1] = elements[i].id;
    }
    return static_cast<uint32_t>(elements.size());
}

void facetry_renderer_set_pick_filter(facetry_renderer* renderer, uint32_t filter) {
    run(renderer, [&](Renderer& r) {
        if (filter > 5) {
            throw std::invalid_argument("Unknown pick filter");
        }
        r.setPickFilter(static_cast<facetry::render::PickFilter>(filter));
    });
}

facetry_render_status facetry_renderer_draw(facetry_renderer* renderer,
                                            const facetry_render_target* target) {
    return run(renderer, [&](Renderer& r) {
        facetry::render::FrameTarget frame;
        if (target != nullptr) {
            frame.metalDrawable = target->metal_drawable;
            frame.metalDepthStencilTexture = target->metal_depth_stencil_texture;
            frame.metalMsaaColorTexture = target->metal_msaa_color_texture;
        }
        r.draw(frame);
    });
}

void facetry_renderer_stats(const facetry_renderer* renderer, facetry_render_stats* out) {
    if (renderer == nullptr || !renderer->renderer || out == nullptr) {
        return;
    }
    const auto stats = renderer->renderer->stats();
    out->cpu_milliseconds = stats.cpuMilliseconds;
    out->triangles = stats.triangles;
    out->edge_segments = stats.edgeSegments;
    out->vertices = stats.vertices;
}

uint32_t facetry_navigation_drag(uint32_t preset, uint32_t button, uint32_t modifiers) {
    using namespace facetry::render;
    if (preset > 2 || button > 2) {
        return FACETRY_NAVIGATE_NONE;
    }
    return static_cast<uint32_t>(dragAction(static_cast<NavigationPreset>(preset),
                                            static_cast<MouseButton>(button), modifiers));
}

uint32_t facetry_navigation_scroll(uint32_t preset, uint32_t modifiers, int trackpad) {
    using namespace facetry::render;
    if (preset > 2) {
        return FACETRY_NAVIGATE_ZOOM;
    }
    return static_cast<uint32_t>(
        scrollAction(static_cast<NavigationPreset>(preset), modifiers, trackpad != 0));
}

}  // extern "C"
