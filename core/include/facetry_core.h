/*
 * Facetry core C API: the boundary between the shared C++ core and the app shells
 * (Swift on iPadOS/macOS, TypeScript on WebAssembly). See docs/adr/0003-core-api-boundary-c-abi.md.
 *
 * Rules:
 * - Every function that can fail returns facetry_status. On failure,
 *   facetry_last_error_message() describes the problem. No exception ever crosses this API.
 * - Objects are identified by handles (uint32_t) inside a context. 0 is never a valid handle.
 * - Topology IDs (faces, edges, vertices) are 1-based and only valid for the shape they came
 *   from. They are not persistent across modeling operations; persistent names come from
 *   sessions (see "Documents" below).
 * - Lengths are in millimeters, angles in radians.
 * - Strings are UTF-8. Strings returned through `const char**` are owned by the context and
 *   valid until the next call on the same context.
 */
#ifndef FACETRY_CORE_H
#define FACETRY_CORE_H

#include <stddef.h>
#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/em_macros.h>
#define FACETRY_API EMSCRIPTEN_KEEPALIVE
#else
#define FACETRY_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Versioning ------------------------------------------------------------------------- */

/* Shells must refuse to run against a core with a different major version. */
#define FACETRY_API_VERSION_MAJOR 0
#define FACETRY_API_VERSION_MINOR 13

FACETRY_API void facetry_api_version(uint32_t* major, uint32_t* minor);

/* Static, null-terminated strings. */
FACETRY_API const char* facetry_core_version(void);   /* e.g. "0.0.0" */
FACETRY_API const char* facetry_kernel_version(void); /* OpenCASCADE, e.g. "8.0.1" */
FACETRY_API const char* facetry_platform(void);       /* "macos", "ios", "ios-simulator", "wasm" */

/* ---- Status and errors ------------------------------------------------------------------ */

typedef enum facetry_status {
    FACETRY_OK = 0,
    FACETRY_ERROR_INVALID_ARGUMENT = 1, /* e.g. negative size, unknown edge ID */
    FACETRY_ERROR_INVALID_HANDLE = 2,   /* handle unknown, released, or of the wrong kind */
    FACETRY_ERROR_KERNEL = 3,           /* the geometry kernel could not perform the operation */
    FACETRY_ERROR_UNSUPPORTED = 4,
    FACETRY_ERROR_IO = 5,
    FACETRY_ERROR_OUT_OF_MEMORY = 6,
    FACETRY_ERROR_INTERNAL = 7 /* unexpected failure; a bug in Facetry */
} facetry_status;

typedef uint32_t facetry_handle;

typedef struct facetry_context facetry_context;

FACETRY_API facetry_status facetry_context_create(facetry_context** out_context);
/* Releases the context and every object it still owns. Accepts NULL. */
FACETRY_API void facetry_context_destroy(facetry_context* context);

/* Message of the last failed call on this context, or "" if the last call succeeded.
 * Valid until the next call on the same context. */
FACETRY_API const char* facetry_last_error_message(const facetry_context* context);
FACETRY_API facetry_status facetry_last_error_status(const facetry_context* context);

/* ---- Logging ---------------------------------------------------------------------------- */

typedef enum facetry_log_level {
    FACETRY_LOG_TRACE = 0,
    FACETRY_LOG_DEBUG = 1,
    FACETRY_LOG_INFO = 2,
    FACETRY_LOG_WARNING = 3,
    FACETRY_LOG_ERROR = 4
} facetry_log_level;

typedef void (*facetry_log_callback)(facetry_log_level level, const char* message, void* user_data);

/* Process-wide. NULL restores the default sink (standard error). */
FACETRY_API void facetry_set_log_callback(facetry_log_callback callback, void* user_data);
/* Messages below this level are dropped. Default: FACETRY_LOG_INFO. */
FACETRY_API void facetry_set_log_level(facetry_log_level level);

/* ---- Shapes ------------------------------------------------------------------------------ */

/* Box with one corner at the origin. */
FACETRY_API facetry_status facetry_shape_make_box(facetry_context* context, double dx, double dy,
                                                  double dz, facetry_handle* out_shape);

/* Cylinder standing on the XY plane, centered at (x, y). */
FACETRY_API facetry_status facetry_shape_make_cylinder(facetry_context* context, double x, double y,
                                                       double radius, double height,
                                                       facetry_handle* out_shape);

typedef enum facetry_boolean_op {
    FACETRY_BOOLEAN_FUSE = 0,
    FACETRY_BOOLEAN_CUT = 1,
    FACETRY_BOOLEAN_COMMON = 2
} facetry_boolean_op;

FACETRY_API facetry_status facetry_shape_boolean(facetry_context* context, facetry_boolean_op op,
                                                 facetry_handle shape, facetry_handle tool,
                                                 facetry_handle* out_shape);

/* Rounds the given edges (1-based edge IDs of `shape`) with a constant radius. */
FACETRY_API facetry_status facetry_shape_fillet(facetry_context* context, facetry_handle shape,
                                                const uint32_t* edge_ids, size_t edge_count,
                                                double radius, facetry_handle* out_shape);

/* Volume, area and bounding box in model units. Every field is a double so that the layout is
 * trivial to read from other languages (12 doubles, 96 bytes). */
typedef struct facetry_shape_properties {
    double volume;
    double area;
    double bbox_min[3];
    double bbox_max[3];
    double solid_count;
    double face_count;
    double edge_count;
    double vertex_count;
} facetry_shape_properties;

FACETRY_API facetry_status facetry_shape_properties_get(facetry_context* context,
                                                        facetry_handle shape,
                                                        facetry_shape_properties* out_properties);

FACETRY_API facetry_status facetry_shape_release(facetry_context* context, facetry_handle shape);

/* ---- Tessellation ------------------------------------------------------------------------ */

/* Triangulates a shape for display. `linear_deflection` is the maximum distance between mesh
 * and surface (model units, or a fraction of the shape size if `relative` is non-zero);
 * `angular_deflection` is the maximum angle between neighboring facets (radians). */
FACETRY_API facetry_status facetry_shape_tessellate(facetry_context* context, facetry_handle shape,
                                                    double linear_deflection,
                                                    double angular_deflection, int relative,
                                                    facetry_handle* out_mesh);

typedef enum facetry_mesh_buffer_kind {
    /* float32 x, y, z per mesh vertex. Mesh vertices are not shared between faces. */
    FACETRY_MESH_POSITIONS = 0,
    /* float32 x, y, z unit normal per mesh vertex. */
    FACETRY_MESH_NORMALS = 1,
    /* uint32 three vertex indices per triangle, counter-clockwise seen from outside. */
    FACETRY_MESH_TRIANGLES = 2,
    /* uint32 face ID per triangle. */
    FACETRY_MESH_TRIANGLE_FACE_IDS = 3,
    /* float32 x, y, z points of all edge polylines, one after another. */
    FACETRY_MESH_EDGE_POINTS = 4,
    /* uint32 offsets into EDGE_POINTS (in points): edge i uses points
     * [offsets[i], offsets[i + 1]). Has edge count + 1 elements. */
    FACETRY_MESH_EDGE_OFFSETS = 5,
    /* uint32 edge ID per edge polyline. */
    FACETRY_MESH_EDGE_IDS = 6,
    /* float32 x, y, z per topological vertex. */
    FACETRY_MESH_VERTEX_POSITIONS = 7,
    /* uint32 vertex ID per topological vertex. */
    FACETRY_MESH_VERTEX_IDS = 8
} facetry_mesh_buffer_kind;

/* Returns a pointer to one buffer of the mesh and its number of elements (floats or uint32s,
 * not bytes). The data is owned by the mesh and valid until the mesh is released. */
FACETRY_API facetry_status facetry_mesh_buffer(facetry_context* context, facetry_handle mesh,
                                               facetry_mesh_buffer_kind kind, const void** out_data,
                                               size_t* out_count);

FACETRY_API facetry_status facetry_mesh_release(facetry_context* context, facetry_handle mesh);

/* ---- Documents -------------------------------------------------------------------------- */

/* A session is an open document: its description, undo/redo history and regeneration cache.
 * Edits and summaries cross the API as JSON; geometry comes out as shapes and meshes
 * (docs/adr/0011-document-api-json-over-c.md). The JSON formats are documented in
 * core/include/facetry/core/session_json.hpp and core/include/facetry/core/document.hpp. */

/* Opens a document from its JSON description, or an empty document if `document_json` is NULL.
 * The document is not regenerated yet. */
FACETRY_API facetry_status facetry_session_create(facetry_context* context,
                                                  const char* document_json,
                                                  facetry_handle* out_session);

/* The document's JSON description. */
FACETRY_API facetry_status facetry_session_save(facetry_context* context, facetry_handle session,
                                                const char** out_json);

/* Opens a .facetry file (docs/adr/0008-native-file-format.md). Also accepts a bare document.json
 * and the provisional files of the M0.5 Apple app, which open as an empty document. */
FACETRY_API facetry_status facetry_session_open_file(facetry_context* context, const uint8_t* data,
                                                     size_t size, facetry_handle* out_session);

/* The document as a .facetry file. `thumbnail_png` (may be NULL) is stored as its thumbnail. The
 * bytes are owned by the context and valid until the next call on it. */
FACETRY_API facetry_status facetry_session_save_file(facetry_context* context,
                                                     facetry_handle session,
                                                     const uint8_t* thumbnail_png,
                                                     size_t thumbnail_size,
                                                     const uint8_t** out_data, size_t* out_size);

/* Applies one edit, or a JSON array of edits, as one undo step. An edit that fails changes
 * nothing. */
FACETRY_API facetry_status facetry_session_apply(facetry_context* context, facetry_handle session,
                                                 const char* edit_json);

/* Groups the edits until facetry_session_end_transaction() into one undo step named `name`, e.g.
 * everything a tool does until the user confirms it. `commit` = 0 cancels: the document returns to
 * how it was before the transaction. Undo and redo fail while a transaction is open. */
FACETRY_API facetry_status facetry_session_begin_transaction(facetry_context* context,
                                                             facetry_handle session,
                                                             const char* name);
FACETRY_API facetry_status facetry_session_end_transaction(facetry_context* context,
                                                           facetry_handle session, int commit);

/* `out_changed` (optional) is set to 1 if there was something to undo or redo, else 0. */
FACETRY_API facetry_status facetry_session_undo(facetry_context* context, facetry_handle session,
                                                int* out_changed);
FACETRY_API facetry_status facetry_session_redo(facetry_context* context, facetry_handle session,
                                                int* out_changed);

/* Called after each feature during regeneration, and from time to time with the same `done`
 * while a long kernel operation runs. Return non-zero to cancel the rest: features that finished
 * stay cached, and the next regeneration continues from there. Operations that support it stop
 * early (since API 0.5); the others run to the end. The callback must not call into the same
 * context. */
typedef int (*facetry_progress_callback)(uint32_t done, uint32_t total, void* user_data);

/* Regenerates the document. Feature failures are not call failures: they are reported per
 * feature in facetry_session_state(). `progress` may be NULL. */
FACETRY_API facetry_status facetry_session_regenerate(facetry_context* context,
                                                      facetry_handle session,
                                                      facetry_progress_callback progress,
                                                      void* user_data);

/* Summary as JSON: units, parameters, features with their status and messages, bodies, undo/redo
 * state, and whether the document changed since the last regeneration. */
FACETRY_API facetry_status facetry_session_state(facetry_context* context, facetry_handle session,
                                                 const char** out_json);

/* A new shape handle for a body of the last regeneration (`body_id` is the ID of the feature that
 * created it). Release it with facetry_shape_release(). */
FACETRY_API facetry_status facetry_session_body_shape(facetry_context* context,
                                                      facetry_handle session, const char* body_id,
                                                      facetry_handle* out_shape);

/* A new shape handle for the closed regions (profiles) of a sketch of the last regeneration: face
 * ID i + 1 is the sketch's region i in facetry_session_state() ("regions"). It has no faces if
 * the sketch encloses nothing. Release it with facetry_shape_release(). */
FACETRY_API facetry_status facetry_session_sketch_regions(facetry_context* context,
                                                          facetry_handle session,
                                                          const char* sketch_id,
                                                          facetry_handle* out_shape);

/* A display mesh of a sketch of the last regeneration (facetry_mesh_buffer): edge ID i + 1 is its
 * curve i (its lines, then arcs, then circles, in the order of its definition), sampled within
 * `linear_deflection` millimeters; vertex ID i + 1 is its point i. It has no triangles. Release it
 * with facetry_mesh_release(). */
FACETRY_API facetry_status facetry_session_sketch_mesh(facetry_context* context,
                                                       facetry_handle session,
                                                       const char* sketch_id,
                                                       double linear_deflection,
                                                       facetry_handle* out_mesh);

/* Persistent names of the body's faces, edges and vertices as JSON:
 * {"faces": [...], "edges": [...], "vertices": [...]}; entry i belongs to element ID i + 1. */
FACETRY_API facetry_status facetry_session_element_names(facetry_context* context,
                                                         facetry_handle session,
                                                         const char* body_id,
                                                         const char** out_json);

typedef enum facetry_element_kind {
    FACETRY_ELEMENT_FACE = 0,
    FACETRY_ELEMENT_EDGE = 1,
    FACETRY_ELEMENT_VERTEX = 2
} facetry_element_kind;

/* Turns a picked element (an ID from a mesh of the body's shape) into a persistent reference, as
 * JSON to put into a feature edit, e.g. a fillet's "edges". */
FACETRY_API facetry_status facetry_session_make_reference(
    facetry_context* context, facetry_handle session, const char* body_id,
    facetry_element_kind kind, uint32_t element_id, const char** out_json);

FACETRY_API facetry_status facetry_session_release(facetry_context* context,
                                                   facetry_handle session);

/* ---- Sketch tools (#49, docs/adr/0015-sketch-tools.md) --------------------------------- */

/* Drawing tools: they turn pointer events on the sketch plane into sketch entities, the same on
 * every platform. Run them inside a transaction (facetry_session_begin_transaction) so a whole
 * sketch is one undo step. */
typedef enum facetry_sketch_tool_kind {
    FACETRY_TOOL_LINE = 0, /* a polyline; dragging back from its last point draws a tangent arc */
    FACETRY_TOOL_RECTANGLE_TWO_POINT = 1,   /* opposite corners, sides along the sketch axes */
    FACETRY_TOOL_RECTANGLE_THREE_POINT = 2, /* one side, then the width */
    FACETRY_TOOL_RECTANGLE_CENTER = 3,      /* the center, then a corner */
    FACETRY_TOOL_CIRCLE_CENTER = 4,         /* the center, then a point on the circle */
    FACETRY_TOOL_CIRCLE_TWO_POINT = 5,      /* the ends of a diameter */
    FACETRY_TOOL_CIRCLE_THREE_POINT = 6,    /* three points on the circle */
    FACETRY_TOOL_ARC_THREE_POINT = 7,       /* start, end, then a point on the arc */
    FACETRY_TOOL_ARC_CENTER = 8,  /* the center, the start, the end; the way round follows the
                                     pointer */
    FACETRY_TOOL_ARC_TANGENT = 9, /* from the end of a line or arc, tangent to it */
    FACETRY_TOOL_POINT = 10,
    /* Editing (#51) */
    FACETRY_TOOL_TRIM = 11,   /* removes the piece of a curve under the pointer up to the curves
                                 crossing it; dragging trims every curve it crosses */
    FACETRY_TOOL_EXTEND = 12, /* runs the line clicked near one end on to the next curve */
    FACETRY_TOOL_FILLET = 13, /* a corner of two lines, then how far from it the arc starts */
    FACETRY_TOOL_MOVE = 14,   /* the selection, or what is under the first click, from a base
                                 point to a target */
    FACETRY_TOOL_COPY = 15    /* like move, keeping the originals */
} facetry_sketch_tool_kind;

typedef enum facetry_pointer_event {
    FACETRY_POINTER_DOWN = 0,
    FACETRY_POINTER_MOVE = 1,
    FACETRY_POINTER_UP = 2
} facetry_pointer_event;

/* A tool for the sketch feature `sketch_id`; new curves are construction curves if
 * `construction` is non-zero. Release it with facetry_sketch_tool_release(). */
FACETRY_API facetry_status facetry_sketch_tool_create(facetry_context* context,
                                                      const char* sketch_id, uint32_t kind,
                                                      int construction, facetry_handle* out_tool);
/* A pointer event at (u, v) on the sketch plane, in millimeters along the sketch's axes
 * (facetry_renderer_sketch_point). A click is down then up at about the same place; a drag counts
 * as two clicks. Positions within `snap_radius` millimeters of a sketch point use that point.
 * `out_changed` (may be NULL) is 1 when the tool changed the document: regenerate then. */
FACETRY_API facetry_status facetry_sketch_tool_pointer(facetry_context* context,
                                                       facetry_handle session, facetry_handle tool,
                                                       uint32_t event, double u, double v,
                                                       double snap_radius, int* out_changed);
/* Esc: drops the shape in progress or ends the polyline. `out_was_busy` (may be NULL) is 0 when
 * nothing was in progress; the shell then leaves the tool. */
FACETRY_API facetry_status facetry_sketch_tool_cancel(facetry_context* context, facetry_handle tool,
                                                      int* out_was_busy);
/* What the tool would draw next, as a mesh like facetry_session_sketch_mesh (the curves as edges,
 * the points placed so far and the pointer as vertices). Edges from ID 1001 on are guides
 * (alignment and inference lines): draw them dashed. Release it with facetry_mesh_release(). */
FACETRY_API facetry_status facetry_sketch_tool_preview(facetry_context* context,
                                                       facetry_handle session, facetry_handle tool,
                                                       double linear_deflection,
                                                       facetry_handle* out_mesh);
FACETRY_API facetry_status facetry_sketch_tool_release(facetry_context* context,
                                                       facetry_handle tool);
/* What the pointer snapped to and what the next click infers, as JSON:
 * {"snap": "midpoint", "inferences": ["horizontal"], "radius": 0}. "snap" is "point", "center",
 * "midpoint", "quadrant", "intersection", "origin", "onCurve", "grid" or "" (free). "radius" is
 * the fillet tool's radius at the pointer in millimeters, 0 for other tools. */
FACETRY_API facetry_status facetry_sketch_tool_feedback(facetry_context* context,
                                                        facetry_handle tool, const char** out_json);
/* Grid snap: free positions round to multiples of `spacing` millimeters; 0 turns it off. */
FACETRY_API facetry_status facetry_sketch_tool_set_grid(facetry_context* context,
                                                        facetry_handle tool, double spacing);
/* The move and copy tools' selection: a JSON array of point and curve IDs, e.g. ["l2", "p7"].
 * With an empty one, the first click takes the point or curve under the pointer. */
FACETRY_API facetry_status facetry_sketch_tool_set_selection(facetry_context* context,
                                                             facetry_handle tool,
                                                             const char* entities_json);

#ifdef __cplusplus
}
#endif

#endif /* FACETRY_CORE_H */
