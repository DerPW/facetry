/*
 * Facetry viewport renderer C API (docs/adr/0004-viewport-renderer-shared-cpp-sokol.md).
 * Same rules as facetry_core.h: status codes, no exceptions across the API, plain data.
 *
 * Screen coordinates are in points (UIKit points / CSS pixels), origin top-left. The renderer
 * converts to framebuffer pixels with the pixel ratio given to facetry_renderer_set_viewport.
 * Only one renderer can exist at a time.
 */
#ifndef FACETRY_RENDER_H
#define FACETRY_RENDER_H

#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/em_macros.h>
#define FACETRY_RENDER_API EMSCRIPTEN_KEEPALIVE
#else
#define FACETRY_RENDER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum facetry_render_status {
    FACETRY_RENDER_OK = 0,
    FACETRY_RENDER_ERROR_INVALID_ARGUMENT = 1,
    FACETRY_RENDER_ERROR_GPU = 2,     /* the graphics device or context failed */
    FACETRY_RENDER_ERROR_INTERNAL = 3 /* unexpected failure; a bug in Facetry */
} facetry_render_status;

typedef struct facetry_renderer facetry_renderer;

/* Apple: renders with Metal on `metal_device` (id<MTLDevice>). `sample_count` must match the
 * MTKView (1 or 4). */
FACETRY_RENDER_API facetry_render_status facetry_renderer_create_metal(const void* metal_device,
                                                                       int sample_count,
                                                                       facetry_renderer** out);
/* Web: creates a WebGL2 context on the canvas found by the CSS selector. */
FACETRY_RENDER_API facetry_render_status facetry_renderer_create_webgl(const char* canvas_selector,
                                                                       int antialias,
                                                                       facetry_renderer** out);
FACETRY_RENDER_API void facetry_renderer_destroy(facetry_renderer* renderer);
/* Message of the last failed call, or "". */
FACETRY_RENDER_API const char* facetry_renderer_last_error(const facetry_renderer* renderer);

/* Mesh buffers exactly as the core returns them (facetry_mesh_buffer_kind). Counts are in
 * elements (floats or uint32s). The renderer copies the data. */
typedef struct facetry_render_mesh {
    const float* positions;
    uint32_t position_count;
    const float* normals; /* position_count elements */
    const uint32_t* triangles;
    uint32_t triangle_index_count;
    const uint32_t* triangle_face_ids; /* triangle_index_count / 3 elements */
    const float* edge_points;
    uint32_t edge_point_count;
    const uint32_t* edge_offsets;
    uint32_t edge_offset_count;
    const uint32_t* edge_ids; /* edge_offset_count - 1 elements */
    const float* vertex_positions;
    uint32_t vertex_position_count;
    const uint32_t* vertex_ids; /* vertex_position_count / 3 elements */
} facetry_render_mesh;

FACETRY_RENDER_API facetry_render_status facetry_renderer_set_mesh(facetry_renderer* renderer,
                                                                   const facetry_render_mesh* mesh);

FACETRY_RENDER_API void facetry_renderer_set_viewport(facetry_renderer* renderer, float width,
                                                      float height, float pixel_ratio);

/* ---- Camera ------------------------------------------------------------------------------- */

FACETRY_RENDER_API void facetry_camera_fit(facetry_renderer* renderer);
FACETRY_RENDER_API void facetry_camera_set_orthographic(facetry_renderer* renderer,
                                                        int orthographic);
/* Starts an orbit around the model point under (x, y), or the view center if there is none. */
FACETRY_RENDER_API void facetry_camera_orbit_begin(facetry_renderer* renderer, float x, float y);
FACETRY_RENDER_API void facetry_camera_orbit(facetry_renderer* renderer, float dx, float dy);
FACETRY_RENDER_API void facetry_camera_pan(facetry_renderer* renderer, float dx, float dy);
/* factor < 1 zooms in, toward the point under (x, y). */
FACETRY_RENDER_API void facetry_camera_zoom(facetry_renderer* renderer, float factor, float x,
                                            float y);

/* ---- Picking and highlights --------------------------------------------------------------- */

typedef enum facetry_pick_kind {
    FACETRY_PICK_NONE = 0,
    FACETRY_PICK_FACE = 1,
    FACETRY_PICK_EDGE = 2,
    FACETRY_PICK_VERTEX = 3
} facetry_pick_kind;

typedef struct facetry_pick_result {
    uint32_t kind; /* facetry_pick_kind */
    uint32_t id;   /* face, edge or vertex ID */
    float point[3];
    float screen_distance; /* points between the pointer and the element */
} facetry_pick_result;

/* Picks the element under (x, y). Vertices win over edges and edges over faces within
 * `radius` points; use a larger radius for fingers than for the Pencil or a mouse. */
FACETRY_RENDER_API facetry_render_status facetry_renderer_pick(facetry_renderer* renderer, float x,
                                                               float y, float radius,
                                                               facetry_pick_result* out);
FACETRY_RENDER_API void facetry_renderer_set_hover(facetry_renderer* renderer, uint32_t kind,
                                                   uint32_t id);
FACETRY_RENDER_API void facetry_renderer_set_selection(facetry_renderer* renderer, uint32_t kind,
                                                       uint32_t id);

/* ---- Drawing ------------------------------------------------------------------------------ */

/* Metal only (ignored on the web): the MTKView's current drawable, depth-stencil texture and,
 * with MSAA, multisample color texture. */
typedef struct facetry_render_target {
    const void* metal_drawable;
    const void* metal_depth_stencil_texture;
    const void* metal_msaa_color_texture;
} facetry_render_target;

FACETRY_RENDER_API facetry_render_status facetry_renderer_draw(facetry_renderer* renderer,
                                                               const facetry_render_target* target);

typedef struct facetry_render_stats {
    float cpu_milliseconds; /* CPU time of the last draw call */
    uint32_t triangles;
    uint32_t edge_segments;
    uint32_t vertices;
} facetry_render_stats;

FACETRY_RENDER_API void facetry_renderer_stats(const facetry_renderer* renderer,
                                               facetry_render_stats* out);

#ifdef __cplusplus
}
#endif

#endif /* FACETRY_RENDER_H */
