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

/* The ground grid on the XY plane, shown by default. Its spacing follows the zoom level in
 * document units; `unit_millimeters` is the size of one (1 for mm, 25.4 for inches). */
FACETRY_RENDER_API void facetry_renderer_set_grid(facetry_renderer* renderer, int visible,
                                                  float unit_millimeters);

/* ---- Camera ------------------------------------------------------------------------------- */

/* Frames the whole model from the current view direction (Fusion's Fit). */
FACETRY_RENDER_API void facetry_camera_fit(facetry_renderer* renderer);
/* Frames the selected element (facetry_renderer_set_selection), or the whole model. */
FACETRY_RENDER_API void facetry_camera_fit_selection(facetry_renderer* renderer);
/* The home view: the whole model from the front right, above. */
FACETRY_RENDER_API void facetry_camera_home(facetry_renderer* renderer, int animate);
/* Turns the camera around its target to look from direction (x, y, z), pointing from the model
 * toward the eye: (0, 0, 1) is the top view, (0, -1, 0) the front view, (1, -1, 1) a corner. */
FACETRY_RENDER_API void facetry_camera_view_from(facetry_renderer* renderer, float x, float y,
                                                 float z, int animate);
FACETRY_RENDER_API void facetry_camera_set_orthographic(facetry_renderer* renderer,
                                                        int orthographic);
/* Starts an orbit around the model point under (x, y), or the view center if there is none. */
FACETRY_RENDER_API void facetry_camera_orbit_begin(facetry_renderer* renderer, float x, float y);
FACETRY_RENDER_API void facetry_camera_orbit(facetry_renderer* renderer, float dx, float dy);
FACETRY_RENDER_API void facetry_camera_pan(facetry_renderer* renderer, float dx, float dy);
/* factor < 1 zooms in, toward the point under (x, y). */
FACETRY_RENDER_API void facetry_camera_zoom(facetry_renderer* renderer, float factor, float x,
                                            float y);

/* ---- Navigation presets (docs/INPUT.md) --------------------------------------------------- */

/* What a mouse button or scroll gesture does, the same on every platform. No renderer needed. */
typedef enum facetry_navigation_preset {
    FACETRY_NAVIGATION_FACETRY = 0, /* left drag orbits, right/middle drag pans, wheel zooms */
    FACETRY_NAVIGATION_FUSION = 1,  /* middle drag pans, Shift + middle orbits, left selects */
    FACETRY_NAVIGATION_TRACKPAD = 2 /* scrolling orbits, Shift + scrolling pans, everywhere */
} facetry_navigation_preset;

typedef enum facetry_navigation_action {
    FACETRY_NAVIGATE_NONE = 0,
    FACETRY_NAVIGATE_ORBIT = 1,
    FACETRY_NAVIGATE_PAN = 2,
    FACETRY_NAVIGATE_ZOOM = 3,
    FACETRY_NAVIGATE_SELECT = 4 /* a click selects; a drag will draw a selection box */
} facetry_navigation_action;

/* Buttons are numbered like the DOM's MouseEvent.button: 0 left, 1 middle, 2 right. */
typedef enum facetry_modifier {
    FACETRY_MODIFIER_SHIFT = 1,
    FACETRY_MODIFIER_CONTROL = 2,
    FACETRY_MODIFIER_ALT = 4,
    FACETRY_MODIFIER_COMMAND = 8
} facetry_modifier;

/* facetry_navigation_action for dragging with `button`; `modifiers` is a facetry_modifier mask. */
FACETRY_RENDER_API uint32_t facetry_navigation_drag(uint32_t preset, uint32_t button,
                                                    uint32_t modifiers);
/* facetry_navigation_action for scrolling. `trackpad` is 1 when the platform knows the scrolling
 * comes from a trackpad (precise deltas on Apple), 0 for a mouse wheel or when it cannot tell. */
FACETRY_RENDER_API uint32_t facetry_navigation_scroll(uint32_t preset, uint32_t modifiers,
                                                      int trackpad);

/* ---- ViewCube ---------------------------------------------------------------------------- */

typedef enum facetry_cube_face {
    FACETRY_CUBE_RIGHT = 0, /* +X */
    FACETRY_CUBE_LEFT = 1,  /* -X */
    FACETRY_CUBE_BACK = 2,  /* +Y */
    FACETRY_CUBE_FRONT = 3, /* -Y */
    FACETRY_CUBE_TOP = 4,   /* +Z */
    FACETRY_CUBE_BOTTOM = 5 /* -Z */
} facetry_cube_face;

/* One cell of the ViewCube. Each visible face is split into a 3 × 3 grid: the center, four edges
 * and four corners; a click turns the view to `direction` (facetry_camera_view_from). */
typedef struct facetry_viewcube_cell {
    float points[8];    /* x0 y0 … x3 y3 in points inside the cube's square, origin top-left */
    float direction[3]; /* a face normal, or the sum of two (edge) or three (corner) */
    float depth;        /* larger is nearer to the viewer */
    uint32_t face;      /* facetry_cube_face */
    uint32_t region;    /* 0 center, 1 edge, 2 corner */
} facetry_viewcube_cell;

/* The visible cells for a cube drawn in a square of `size` points, far to near (draw them in
 * this order). Writes at most `capacity` cells (27 are enough) and returns how many there are. */
FACETRY_RENDER_API uint32_t facetry_viewcube_cells(const facetry_renderer* renderer, float size,
                                                   facetry_viewcube_cell* out, uint32_t capacity);
/* Index of the cell under (x, y) in the square, in the order of facetry_viewcube_cells, or -1. */
FACETRY_RENDER_API int32_t facetry_viewcube_hit(const facetry_renderer* renderer, float size,
                                                float x, float y);

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
