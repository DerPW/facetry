/*
 * Facetry core C API: the boundary between the shared C++ core and the app shells
 * (Swift on iPadOS/macOS, TypeScript on WebAssembly). See docs/adr/0003-core-api-boundary-c-abi.md.
 *
 * Rules:
 * - Every function that can fail returns facetry_status. On failure,
 *   facetry_last_error_message() describes the problem. No exception ever crosses this API.
 * - Objects are identified by handles (uint32_t) inside a context. 0 is never a valid handle.
 * - Topology IDs (faces, edges, vertices) are 1-based and only valid for the shape they came
 *   from. They are not persistent across modeling operations.
 * - Lengths are in millimeters, angles in radians.
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
#define FACETRY_API_VERSION_MINOR 1

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

#ifdef __cplusplus
}
#endif

#endif /* FACETRY_CORE_H */
