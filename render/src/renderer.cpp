#include "renderer.hpp"

#include "facetry/render/grid.hpp"

#include "sokol.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif

namespace facetry::render {
namespace {

// Style in points; converted to pixels with the pixel ratio.
constexpr float kEdgeHalfWidth = 0.6F;
constexpr float kEdgeHalfWidthHighlighted = 1.4F;
constexpr float kVertexRadius = 2.0F;
constexpr float kVertexRadiusHighlighted = 4.5F;

constexpr std::array<float, 4> kBackground = {0.925F, 0.933F, 0.945F, 1.0F};
constexpr std::array<float, 4> kBaseColor = {0.70F, 0.75F, 0.82F, 1.0F};
constexpr std::array<float, 4> kEdgeColor = {0.13F, 0.15F, 0.18F, 1.0F};
constexpr std::array<float, 4> kVertexColor = {0.13F, 0.15F, 0.18F, 1.0F};
constexpr std::array<float, 4> kHoverColor = {0.20F, 0.55F, 1.00F, 1.0F};
constexpr std::array<float, 4> kSelectColor = {0.00F, 0.40F, 0.95F, 1.0F};
constexpr std::array<float, 4> kFaceHoverTint = {0.30F, 0.60F, 1.00F, 0.30F};
constexpr std::array<float, 4> kFaceSelectTint = {0.05F, 0.42F, 0.98F, 0.50F};
constexpr std::array<float, 4> kHiddenEdgeColor = {0.13F, 0.15F, 0.18F, 0.45F};
// The first edge ID of guides in a sketch tool's preview (kPreviewGuideId in the core).
constexpr std::uint32_t kPreviewGuideId = 1001;
// Dashes of hidden edges, in points.
constexpr float kDashPeriod = 7.0F;
// Below this the faces count as transparent.
constexpr float kOpaque = 0.999F;
// Sketch regions: a flat translucent fill, stronger when hovered or selected, and outlines.
constexpr std::array<float, 4> kRegionFill = {0.22F, 0.52F, 0.95F, 0.18F};
constexpr std::array<float, 4> kRegionHover = {0.22F, 0.52F, 0.95F, 0.0F};
constexpr std::array<float, 4> kRegionSelect = {0.02F, 0.36F, 0.92F, 0.35F};
constexpr float kRegionHighlightAlpha = 0.5F;
constexpr std::array<float, 4> kRegionOutline = {0.08F, 0.32F, 0.80F, 0.9F};
// The sketch being edited: curves, construction curves (dashed) and points.
constexpr std::array<float, 4> kSketchCurve = {0.05F, 0.22F, 0.70F, 1.0F};
constexpr std::array<float, 4> kSketchConstruction = {0.90F, 0.45F, 0.10F, 1.0F};
constexpr std::array<float, 4> kSketchPoint = {0.05F, 0.10F, 0.30F, 1.0F};
constexpr std::array<float, 4> kSketchPreview = {0.30F, 0.55F, 1.00F, 1.0F};
constexpr float kSketchCurveHalfWidth = 0.9F;
constexpr float kSketchPointRadius = 2.5F;
constexpr std::array<float, 4> kGridMinor = {0.0F, 0.0F, 0.0F, 0.07F};
constexpr std::array<float, 4> kGridMajor = {0.0F, 0.0F, 0.0F, 0.16F};
constexpr std::array<float, 4> kGridXAxis = {0.85F, 0.20F, 0.20F, 0.55F};
constexpr std::array<float, 4> kGridYAxis = {0.20F, 0.65F, 0.25F, 0.55F};

template <std::size_t N>
void copy4(float (&target)[N], const std::array<float, 4>& source) {
    static_assert(N == 4);
    std::memcpy(target, source.data(), sizeof(target));
}

void copyMatrix(float (&target)[16], const Mat4& matrix) {
    std::memcpy(target, matrix.m.data(), sizeof(target));
}

float idFor(PickKind kind, PickKind wanted, std::uint32_t id) {
    return kind == wanted ? static_cast<float>(id) : 0.0F;
}

template <typename T>
sg_buffer makeBuffer(const std::vector<T>& data, bool index, const char* label) {
    sg_buffer_desc desc{};
    desc.data.ptr = data.data();
    desc.data.size = data.size() * sizeof(T);
    desc.usage.vertex_buffer = !index;
    desc.usage.index_buffer = index;
    desc.label = label;
    return sg_make_buffer(&desc);
}

void destroy(sg_buffer& buffer) {
    if (buffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(buffer);
        buffer = {};
    }
}

// A sketch drawn over everything: segments of its curves and of its construction curves (dashed)
// and its points, each with the curve or point ID (1-based) and a selection flag.
struct SketchLayer {
    sg_buffer curves{};
    sg_buffer construction{};
    sg_buffer points{};
    sg_buffer curveFlags{};
    sg_buffer constructionFlags{};
    sg_buffer pointFlags{};
    std::vector<std::uint32_t> curveIds;
    std::vector<std::uint32_t> constructionIds;
    std::vector<std::uint32_t> pointIds;

    void upload(const RenderMesh& mesh, std::span<const std::uint32_t> constructionCurves);
    // Flags the curves and points in `selection` (none for a preview).
    void flag(const Selection* selection);

    void release() {
        for (sg_buffer* buffer :
             {&curves, &construction, &points, &curveFlags, &constructionFlags, &pointFlags}) {
            destroy(*buffer);
        }
        curveIds.clear();
        constructionIds.clear();
        pointIds.clear();
    }
};

// Triangles on the GPU: positions, normals, a face ID per mesh vertex and indices.
struct TriangleBuffers {
    sg_buffer positions{};
    sg_buffer normals{};
    sg_buffer faceIds{};
    sg_buffer triangles{};
    sg_buffer selected{};                      // 1 per mesh vertex whose face is selected
    std::vector<std::uint32_t> vertexFaceIds;  // the face IDs on the CPU, for the flags
    std::uint32_t indexCount = 0;

    void upload(const RenderMesh& mesh, const char* label) {
        const std::size_t vertexCount = mesh.positions.size() / 3;
        if (vertexCount == 0 || mesh.triangles.empty()) {
            return;
        }
        // Mesh vertices belong to exactly one face, so the face ID can be a vertex attribute.
        std::vector<float> ids(vertexCount, 0.0F);
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            ids[mesh.triangles[i]] = static_cast<float>(mesh.triangleFaceIds[i / 3]);
        }
        positions = makeBuffer(mesh.positions, false, label);
        normals = makeBuffer(mesh.normals, false, label);
        faceIds = makeBuffer(ids, false, label);
        triangles = makeBuffer(mesh.triangles, true, label);
        indexCount = static_cast<std::uint32_t>(mesh.triangles.size());
        vertexFaceIds.reserve(vertexCount);
        for (const float id : ids) {
            vertexFaceIds.push_back(static_cast<std::uint32_t>(id));
        }
    }

    void release() {
        destroy(positions);
        destroy(normals);
        destroy(faceIds);
        destroy(triangles);
        destroy(selected);
        vertexFaceIds.clear();
        indexCount = 0;
    }
};

// The mesh's edge polylines as segments for the edge shader: p0, p1 and the edge ID each.
std::vector<float> edgeSegments(const RenderMesh& mesh,
                                std::vector<std::uint32_t>& segmentEdgeIds) {
    std::vector<float> segments;
    for (std::size_t e = 0; e + 1 < mesh.edgeOffsets.size() && e < mesh.edgeIds.size(); ++e) {
        for (std::uint32_t k = mesh.edgeOffsets[e]; k + 1 < mesh.edgeOffsets[e + 1]; ++k) {
            const float* a = &mesh.edgePoints[std::size_t{k} * 3];
            segments.insert(segments.end(), {a[0], a[1], a[2], a[3], a[4], a[5],
                                             static_cast<float>(mesh.edgeIds[e])});
            segmentEdgeIds.push_back(mesh.edgeIds[e]);
        }
    }
    return segments;
}

}  // namespace

struct Renderer::Gpu {
    sg_shader meshShader{};
    sg_shader edgeShader{};
    sg_shader pointShader{};
    sg_shader gridShader{};
    sg_pipeline meshPipeline{};
    // Transparent faces: back faces first, then front faces, without writing depth.
    sg_pipeline meshBackPipeline{};
    sg_pipeline meshFrontPipeline{};
    sg_pipeline hiddenEdgePipeline{};
    sg_pipeline gridPipeline{};
    sg_pipeline edgePipeline{};
    sg_pipeline pointPipeline{};
    // Sketch regions over everything: fills, then outlines; the sketch's curves use the outline
    // pipeline too, its points their own.
    sg_pipeline regionPipeline{};
    sg_pipeline regionOutlinePipeline{};
    sg_pipeline sketchPointPipeline{};

    // Shared unit quads for instanced edges and vertex dots.
    sg_buffer edgeQuad{};
    sg_buffer pointQuad{};
    sg_buffer quadIndices{};

    TriangleBuffers faces;
    sg_buffer segments{};
    sg_buffer vertices{};
    // Selection flags (1 selected): per mesh vertex, per edge segment and per vertex dot.
    // Buffers are immutable, so they are made anew when the selection changes (rarely, and
    // small next to the mesh). The IDs behind them stay on the CPU.
    sg_buffer segmentSelected{};
    sg_buffer vertexSelected{};
    std::vector<std::uint32_t> segmentEdgeIds;
    std::vector<std::uint32_t> dotVertexIds;
    std::uint32_t segmentCount = 0;
    std::uint32_t vertexCount = 0;

    TriangleBuffers regions;
    sg_buffer regionSegments{};
    sg_buffer regionSegmentFlags{};  // all 0: outlines are never highlighted
    std::uint32_t regionSegmentCount = 0;

    void releaseMesh() {
        faces.release();
        destroy(segments);
        destroy(vertices);
        destroy(segmentSelected);
        destroy(vertexSelected);
        segmentEdgeIds.clear();
        dotVertexIds.clear();
        segmentCount = vertexCount = 0;
    }

    // The sketch being edited, and what the sketch tool would draw next.
    SketchLayer sketch;
    SketchLayer preview;

    void releaseRegions() {
        regions.release();
        destroy(regionSegments);
        destroy(regionSegmentFlags);
        regionSegmentCount = 0;
    }
};

namespace {

bool gSokolActive = false;

void setupSokol(const sg_environment& environment) {
    if (gSokolActive) {
        throw std::runtime_error("Only one renderer can exist at a time");
    }
    sg_desc desc{};
    desc.environment = environment;
    desc.logger.func = slog_func;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        throw std::runtime_error("Setting up the graphics backend failed");
    }
    gSokolActive = true;
}

}  // namespace

Renderer::Renderer(int sampleCount, bool zeroToOneDepth)
    : gpu_(std::make_unique<Gpu>()), sampleCount_(sampleCount), zeroToOneDepth_(zeroToOneDepth) {
    const sg_backend backend = sg_query_backend();
    Gpu& g = *gpu_;
    g.meshShader = sg_make_shader(mesh_shader_desc(backend));
    g.edgeShader = sg_make_shader(edge_shader_desc(backend));
    g.pointShader = sg_make_shader(point_shader_desc(backend));
    g.gridShader = sg_make_shader(grid_shader_desc(backend));

    {
        sg_pipeline_desc desc{};
        desc.shader = g.meshShader;
        desc.layout.buffers[0].stride = 12;
        desc.layout.buffers[1].stride = 12;
        desc.layout.buffers[2].stride = 4;
        desc.layout.attrs[ATTR_mesh_position].buffer_index = 0;
        desc.layout.attrs[ATTR_mesh_position].format = SG_VERTEXFORMAT_FLOAT3;
        desc.layout.attrs[ATTR_mesh_normal].buffer_index = 1;
        desc.layout.attrs[ATTR_mesh_normal].format = SG_VERTEXFORMAT_FLOAT3;
        desc.layout.attrs[ATTR_mesh_face_id].buffer_index = 2;
        desc.layout.attrs[ATTR_mesh_face_id].format = SG_VERTEXFORMAT_FLOAT;
        desc.layout.buffers[3].stride = 4;
        desc.layout.attrs[ATTR_mesh_selected].buffer_index = 3;
        desc.layout.attrs[ATTR_mesh_selected].format = SG_VERTEXFORMAT_FLOAT;
        desc.index_type = SG_INDEXTYPE_UINT32;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = true;
        // Push faces back slightly so edges on their boundaries stay visible.
        desc.depth.bias = 1.0F;
        desc.depth.bias_slope_scale = 1.0F;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.face_winding = SG_FACEWINDING_CCW;
        desc.label = "faces";
        g.meshPipeline = sg_make_pipeline(&desc);

        desc.depth.write_enabled = false;
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.cull_mode = SG_CULLMODE_FRONT;
        desc.label = "transparent back faces";
        g.meshBackPipeline = sg_make_pipeline(&desc);
        desc.cull_mode = SG_CULLMODE_BACK;
        desc.label = "transparent front faces";
        g.meshFrontPipeline = sg_make_pipeline(&desc);

        desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        desc.depth.bias = 0.0F;
        desc.depth.bias_slope_scale = 0.0F;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.label = "sketch regions";
        g.regionPipeline = sg_make_pipeline(&desc);
    }
    {
        sg_pipeline_desc desc{};
        desc.shader = g.edgeShader;
        desc.layout.buffers[0].stride = 8;
        desc.layout.buffers[1].stride = 28;
        desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        desc.layout.attrs[ATTR_edge_corner].buffer_index = 0;
        desc.layout.attrs[ATTR_edge_corner].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_edge_p0] = {1, 0, SG_VERTEXFORMAT_FLOAT3};
        desc.layout.attrs[ATTR_edge_p1] = {1, 12, SG_VERTEXFORMAT_FLOAT3};
        desc.layout.attrs[ATTR_edge_edge_id] = {1, 24, SG_VERTEXFORMAT_FLOAT};
        desc.layout.buffers[2].stride = 4;
        desc.layout.buffers[2].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        desc.layout.attrs[ATTR_edge_edge_selected] = {2, 0, SG_VERTEXFORMAT_FLOAT};
        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = true;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.label = "edges";
        g.edgePipeline = sg_make_pipeline(&desc);

        // Edges behind the faces, for "shaded with hidden edges".
        desc.depth.compare = SG_COMPAREFUNC_GREATER;
        desc.depth.write_enabled = false;
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.label = "hidden edges";
        g.hiddenEdgePipeline = sg_make_pipeline(&desc);

        desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        desc.label = "sketch region outlines";
        g.regionOutlinePipeline = sg_make_pipeline(&desc);
    }
    {
        sg_pipeline_desc desc{};
        desc.shader = g.pointShader;
        desc.layout.buffers[0].stride = 8;
        desc.layout.buffers[1].stride = 16;
        desc.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        desc.layout.attrs[ATTR_point_corner].buffer_index = 0;
        desc.layout.attrs[ATTR_point_corner].format = SG_VERTEXFORMAT_FLOAT2;
        desc.layout.attrs[ATTR_point_center] = {1, 0, SG_VERTEXFORMAT_FLOAT3};
        desc.layout.attrs[ATTR_point_vertex_id] = {1, 12, SG_VERTEXFORMAT_FLOAT};
        desc.layout.buffers[2].stride = 4;
        desc.layout.buffers[2].step_func = SG_VERTEXSTEP_PER_INSTANCE;
        desc.layout.attrs[ATTR_point_vertex_selected] = {2, 0, SG_VERTEXFORMAT_FLOAT};
        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = true;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.label = "vertices";
        g.pointPipeline = sg_make_pipeline(&desc);

        desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        desc.depth.write_enabled = false;
        desc.label = "sketch points";
        g.sketchPointPipeline = sg_make_pipeline(&desc);
    }

    {
        // Transparent, tested against the faces but not hiding the edges drawn after it.
        sg_pipeline_desc desc{};
        desc.shader = g.gridShader;
        desc.layout.buffers[0].stride = 8;
        desc.layout.attrs[ATTR_grid_corner].buffer_index = 0;
        desc.layout.attrs[ATTR_grid_corner].format = SG_VERTEXFORMAT_FLOAT2;
        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = false;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        desc.label = "grid";
        g.gridPipeline = sg_make_pipeline(&desc);
    }

    const std::vector<float> edgeQuad = {0, -1, 0, 1, 1, -1, 1, 1};
    const std::vector<float> pointQuad = {-1, -1, -1, 1, 1, -1, 1, 1};
    const std::vector<std::uint16_t> quadIndices = {0, 1, 2, 2, 1, 3};
    g.edgeQuad = makeBuffer(edgeQuad, false, "edge quad");
    g.pointQuad = makeBuffer(pointQuad, false, "point quad");
    g.quadIndices = makeBuffer(quadIndices, true, "quad indices");

    if (sg_query_pipeline_state(g.meshPipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.edgePipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.pointPipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.gridPipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.meshBackPipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.meshFrontPipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.hiddenEdgePipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.regionPipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.regionOutlinePipeline) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(g.sketchPointPipeline) != SG_RESOURCESTATE_VALID) {
        throw std::runtime_error("Creating the viewport shaders failed");
    }
}

Renderer::~Renderer() {
    gpu_->sketch.release();
    gpu_->preview.release();
    gpu_.reset();
    if (gSokolActive) {
        sg_shutdown();
        gSokolActive = false;
    }
}

std::unique_ptr<Renderer> Renderer::createMetal(const void* device, int sampleCount) {
#if defined(SOKOL_METAL)
    if (device == nullptr) {
        throw std::invalid_argument("Metal device must not be null");
    }
    sg_environment environment{};
    environment.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    environment.defaults.sample_count = sampleCount;
    environment.metal.device = device;
    setupSokol(environment);
    try {
        return std::unique_ptr<Renderer>(new Renderer(sampleCount, /*zeroToOneDepth=*/true));
    } catch (...) {
        sg_shutdown();
        gSokolActive = false;
        throw;
    }
#else
    (void)device;
    (void)sampleCount;
    throw std::runtime_error("This build has no Metal backend");
#endif
}

std::unique_ptr<Renderer> Renderer::createWebGL(const char* canvasSelector, bool antialias) {
#if defined(__EMSCRIPTEN__)
    if (canvasSelector == nullptr) {
        throw std::invalid_argument("Canvas selector must not be null");
    }
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.antialias = antialias ? 1 : 0;
    attributes.depth = 1;
    attributes.stencil = 1;
    attributes.alpha = 0;
    attributes.premultipliedAlpha = 0;
    attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context =
        emscripten_webgl_create_context(canvasSelector, &attributes);
    if (context <= 0 ||
        emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS) {
        throw std::runtime_error(std::string("Could not create a WebGL2 context on ") +
                                 canvasSelector);
    }
    const int sampleCount = antialias ? 4 : 1;
    sg_environment environment{};
    environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    environment.defaults.sample_count = sampleCount;
    setupSokol(environment);
    try {
        return std::unique_ptr<Renderer>(new Renderer(sampleCount, /*zeroToOneDepth=*/false));
    } catch (...) {
        sg_shutdown();
        gSokolActive = false;
        throw;
    }
#else
    (void)canvasSelector;
    (void)antialias;
    throw std::runtime_error("This build has no WebGL backend");
#endif
}

void Renderer::setMesh(const RenderMesh& mesh) {
    Gpu& g = *gpu_;
    g.releaseMesh();
    pick_ = PickIndex(mesh);
    camera_.setSceneBounds(pick_.bounds());

    g.faces.upload(mesh, "faces");

    const std::vector<float> segments = edgeSegments(mesh, g.segmentEdgeIds);
    if (!segments.empty()) {
        g.segments = makeBuffer(segments, false, "edge segments");
        g.segmentCount = static_cast<std::uint32_t>(segments.size() / 7);
    }

    std::vector<float> vertices;
    for (std::size_t v = 0; v < mesh.vertexIds.size(); ++v) {
        const float* p = &mesh.vertexPositions[v * 3];
        vertices.insert(vertices.end(), {p[0], p[1], p[2], static_cast<float>(mesh.vertexIds[v])});
        g.dotVertexIds.push_back(mesh.vertexIds[v]);
    }
    if (!vertices.empty()) {
        g.vertices = makeBuffer(vertices, false, "vertices");
        g.vertexCount = static_cast<std::uint32_t>(mesh.vertexIds.size());
    }
    // Element IDs change with the geometry, so an old selection would point elsewhere.
    selection_.clear();
    shownSelection_ = ~std::uint64_t{0};

    stats_.triangles = g.faces.indexCount / 3;
    stats_.edgeSegments = g.segmentCount;
    stats_.vertices = g.vertexCount;
}

void Renderer::setRegions(const RenderMesh& mesh) {
    Gpu& g = *gpu_;
    g.releaseRegions();
    regions_ = PickIndex(mesh);
    g.regions.upload(mesh, "sketch regions");
    std::vector<std::uint32_t> edgeIds;
    const std::vector<float> segments = edgeSegments(mesh, edgeIds);
    if (!segments.empty()) {
        g.regionSegments = makeBuffer(segments, false, "sketch region outlines");
        g.regionSegmentCount = static_cast<std::uint32_t>(edgeIds.size());
        g.regionSegmentFlags = makeBuffer(std::vector<float>(edgeIds.size(), 0.0F), false,
                                          "sketch region outline flags");
    }
    // Region IDs change with the regions, so selected regions are forgotten.
    const std::vector<Element> kept = selection_.elements();
    selection_.clear();
    for (const Element& element : kept) {
        if (element.kind != PickKind::Region) {
            selection_.add(element);
        }
    }
    if (highlight_.hoverKind == PickKind::Region) {
        highlight_ = {};
    }
}

void SketchLayer::upload(const RenderMesh& mesh,
                         std::span<const std::uint32_t> constructionCurves) {
    release();
    std::vector<std::uint32_t> edgeIds;
    const std::vector<float> segments = edgeSegments(mesh, edgeIds);
    std::vector<float> solid;
    std::vector<float> dashed;
    for (std::size_t i = 0; i < edgeIds.size(); ++i) {
        const bool isConstruction = std::find(constructionCurves.begin(), constructionCurves.end(),
                                              edgeIds[i]) != constructionCurves.end();
        auto& target = isConstruction ? dashed : solid;
        target.insert(target.end(), segments.begin() + static_cast<std::ptrdiff_t>(i * 7),
                      segments.begin() + static_cast<std::ptrdiff_t>(i * 7 + 7));
        (isConstruction ? constructionIds : curveIds).push_back(edgeIds[i]);
    }
    std::vector<float> dots;
    for (std::size_t v = 0; v < mesh.vertexIds.size(); ++v) {
        const float* p = &mesh.vertexPositions[v * 3];
        dots.insert(dots.end(), {p[0], p[1], p[2], static_cast<float>(mesh.vertexIds[v])});
        pointIds.push_back(mesh.vertexIds[v]);
    }
    if (!solid.empty()) {
        curves = makeBuffer(solid, false, "sketch curves");
    }
    if (!dashed.empty()) {
        construction = makeBuffer(dashed, false, "sketch construction curves");
    }
    if (!dots.empty()) {
        points = makeBuffer(dots, false, "sketch points");
    }
    flag(nullptr);
}

void SketchLayer::flag(const Selection* selection) {
    const auto update = [&](sg_buffer& buffer, const std::vector<std::uint32_t>& ids,
                            PickKind kind) {
        destroy(buffer);
        if (ids.empty()) {
            return;
        }
        std::vector<float> flags(ids.size(), 0.0F);
        for (std::size_t i = 0; selection != nullptr && i < ids.size(); ++i) {
            flags[i] = selection->contains({kind, ids[i]}) ? 1.0F : 0.0F;
        }
        buffer = makeBuffer(flags, false, "sketch flags");
    };
    update(curveFlags, curveIds, PickKind::SketchCurve);
    update(constructionFlags, constructionIds, PickKind::SketchCurve);
    update(pointFlags, pointIds, PickKind::SketchPoint);
}

void Renderer::setSketch(const RenderMesh& mesh, std::span<const std::uint32_t> construction) {
    gpu_->sketch.upload(mesh, construction);
    sketch_ = PickIndex(mesh);
    // Curve and point IDs change with the sketch, so selected ones are forgotten.
    const std::vector<Element> kept = selection_.elements();
    selection_.clear();
    for (const Element& element : kept) {
        if (element.kind != PickKind::SketchCurve && element.kind != PickKind::SketchPoint) {
            selection_.add(element);
        }
    }
    if (highlight_.hoverKind == PickKind::SketchCurve ||
        highlight_.hoverKind == PickKind::SketchPoint) {
        highlight_ = {};
    }
}

void Renderer::setSketchPreview(const RenderMesh& mesh) {
    // Guides (alignment and inference lines) come from edge ID 1001 on and are dashed.
    std::vector<std::uint32_t> guides;
    for (const std::uint32_t id : mesh.edgeIds) {
        if (id >= kPreviewGuideId) {
            guides.push_back(id);
        }
    }
    gpu_->preview.upload(mesh, guides);
}

float Renderer::gridSpacing() const {
    return gridLayout(camera_, gridUnit_).minor;
}

void Renderer::setViewport(float width, float height, float pixelRatio) {
    camera_.setViewport(width, height);
    pixelRatio_ = pixelRatio > 0.0F ? pixelRatio : 1.0F;
}

void Renderer::orbitBegin(float x, float y) {
    orbitPivot_ = pick_.anchorAt(camera_, x, y);
}

void Renderer::orbit(float dx, float dy) {
    camera_.orbit(dx, dy, orbitPivot_);
}

PickResult Renderer::pick(float x, float y, float radius) const {
    return pickWithOverlays(pick_, {regions_, sketch_}, camera_, x, y, radius, picksThroughFaces(),
                            filter_);
}

PickResult Renderer::click(float x, float y, float radius, bool toggle) {
    const auto candidates = candidatesWithOverlays(pick_, {regions_, sketch_}, camera_, x, y,
                                                   radius, picksThroughFaces(), filter_);
    if (toggle) {
        clickCycle_.reset();
        const PickResult pick = candidates.empty() ? PickResult{} : candidates.front();
        selection_.toggle({pick.kind, pick.id});
        return pick;
    }
    const std::size_t index = clickCycle_.choose(x, y, candidates.size(), selection_.version());
    const PickResult pick = candidates.empty() ? PickResult{} : candidates[index];
    selection_.replace({pick.kind, pick.id});
    clickCycle_.chose(x, y, index, selection_.version());
    return pick;
}

std::size_t Renderer::boxSelect(float x0, float y0, float x1, float y1, bool add) {
    const BoxMode mode = x1 >= x0 ? BoxMode::Window : BoxMode::Crossing;
    const auto elements =
        filter_ == PickFilter::Regions
            ? std::vector<Element>{}
            : pick_.boxSelect(camera_, x0, y0, x1, y1, mode, filter_, picksThroughFaces());
    if (!add) {
        selection_.clear();
    }
    for (const Element& element : elements) {
        selection_.add(element);
    }
    return elements.size();
}

void Renderer::updateSelectionFlags() {
    Gpu& g = *gpu_;
    const auto update = [this](sg_buffer& buffer, const std::vector<std::uint32_t>& ids,
                               PickKind kind, const char* label) {
        destroy(buffer);
        if (ids.empty()) {
            return;
        }
        std::vector<float> flags(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) {
            flags[i] = selection_.contains({kind, ids[i]}) ? 1.0F : 0.0F;
        }
        buffer = makeBuffer(flags, false, label);
    };
    update(g.faces.selected, g.faces.vertexFaceIds, PickKind::Face, "selected faces");
    update(g.regions.selected, g.regions.vertexFaceIds, PickKind::Region, "selected regions");
    g.sketch.flag(&selection_);
    update(g.segmentSelected, g.segmentEdgeIds, PickKind::Edge, "selected edges");
    update(g.vertexSelected, g.dotVertexIds, PickKind::Vertex, "selected vertices");
}

void Renderer::setStyle(VisualStyle style, float opacity) {
    style_ = style;
    opacity_ = std::clamp(opacity, 0.0F, 1.0F);
}

bool Renderer::picksThroughFaces() const {
    return style_ == VisualStyle::Wireframe || opacity_ < kOpaque;
}

void Renderer::fitSelection() {
    Bounds bounds;
    for (const Element& element : selection_.elements()) {
        const Bounds element_bounds = [&] {
            switch (element.kind) {
                case PickKind::Region:
                    return regions_.elementBounds(PickKind::Face, element.id);
                case PickKind::SketchCurve:
                    return sketch_.elementBounds(PickKind::Edge, element.id);
                case PickKind::SketchPoint:
                    return sketch_.elementBounds(PickKind::Vertex, element.id);
                default:
                    return pick_.elementBounds(element.kind, element.id);
            }
        }();
        if (!element_bounds.isEmpty()) {
            bounds.merge(element_bounds);
        }
    }
    if (bounds.isEmpty()) {
        camera_.fit();
        return;
    }
    // A vertex or a short edge would fill the view; keep some of the model around it.
    const float minimum = pick_.bounds().radius() * 0.1F;
    if (bounds.radius() < minimum) {
        const Vec3 center = bounds.center();
        bounds.add(center + Vec3{minimum, minimum, minimum} * 0.5F);
        bounds.add(center - Vec3{minimum, minimum, minimum} * 0.5F);
    }
    camera_.fit(bounds);
}

void Renderer::draw(const FrameTarget& target) {
    const auto started = std::chrono::steady_clock::now();
    // Camera animations (view changes) advance with the time between frames.
    if (lastDraw_.time_since_epoch().count() != 0) {
        const std::chrono::duration<float> elapsed = started - lastDraw_;
        camera_.advance(std::min(elapsed.count(), 0.1F));
    }
    lastDraw_ = started;
    Gpu& g = *gpu_;
    const int width = static_cast<int>(camera_.viewportWidth() * pixelRatio_ + 0.5F);
    const int height = static_cast<int>(camera_.viewportHeight() * pixelRatio_ + 0.5F);

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {kBackground[0], kBackground[1], kBackground[2],
                                         kBackground[3]};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0F;
    pass.swapchain.width = width;
    pass.swapchain.height = height;
    pass.swapchain.sample_count = sampleCount_;
    pass.swapchain.metal.current_drawable = target.metalDrawable;
    pass.swapchain.metal.depth_stencil_texture = target.metalDepthStencilTexture;
    pass.swapchain.metal.msaa_color_texture = target.metalMsaaColorTexture;
    pass.swapchain.gl.framebuffer = 0;
    sg_begin_pass(&pass);

    // The grid reaches past the model, so the far plane has to include it.
    const GridLayout grid = gridLayout(camera_, gridUnit_);
    camera_.setExtraDepth(gridVisible_ ? grid.radius * 1.5F : 0.0F);
    const Mat4 viewProjection = camera_.viewProjection(zeroToOneDepth_);
    const float viewport[4] = {static_cast<float>(width), static_cast<float>(height), 0.0F, 0.0F};
    const Highlight& h = highlight_;

    if (selection_.version() != shownSelection_) {
        updateSelectionFlags();
        shownSelection_ = selection_.version();
    }

    const bool drawFaces = style_ != VisualStyle::Wireframe && opacity_ > 0.001F;
    const bool transparent = opacity_ < kOpaque;

    const auto faces = [&](sg_pipeline pipeline, const TriangleBuffers& buffers, bool regions) {
        mesh_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        mesh_fs_params_t fs{};
        const Vec3 light =
            normalize(camera_.up() * 0.8F - camera_.forward() * 0.6F - camera_.right() * 0.3F);
        fs.key_light[0] = light.x;
        fs.key_light[1] = light.y;
        fs.key_light[2] = light.z;
        if (regions) {
            copy4(fs.base_color, kRegionFill);
            copy4(fs.hover_color, kRegionHover);
            copy4(fs.select_color, kRegionSelect);
            fs.ids[0] = idFor(h.hoverKind, PickKind::Region, h.hoverId);
            fs.ids[1] = 1.0F;  // flat
            fs.ids[3] = kRegionHighlightAlpha;
        } else {
            copy4(fs.base_color, kBaseColor);
            fs.base_color[3] = opacity_;
            copy4(fs.hover_color, kFaceHoverTint);
            copy4(fs.select_color, kFaceSelectTint);
            fs.ids[0] = idFor(h.hoverKind, PickKind::Face, h.hoverId);
            fs.ids[2] = h.hoverKind == PickKind::Body ? 1.0F : 0.0F;
        }

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = buffers.positions;
        bindings.vertex_buffers[1] = buffers.normals;
        bindings.vertex_buffers[2] = buffers.faceIds;
        bindings.vertex_buffers[3] = buffers.selected;
        bindings.index_buffer = buffers.triangles;
        sg_apply_pipeline(pipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_mesh_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_mesh_fs_params, SG_RANGE(fs));
        sg_draw(0, static_cast<int>(buffers.indexCount), 1);
    };

    // `hidden`: the edges behind the faces, faint and dashed. `onlyHighlighted`: just the
    // hovered and selected edges (the shaded style).
    const auto edges = [&](bool hidden, bool onlyHighlighted) {
        edge_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        std::memcpy(vs.viewport, viewport, sizeof(viewport));
        vs.viewport[2] = kEdgeHalfWidth * pixelRatio_;
        vs.viewport[3] = kEdgeHalfWidthHighlighted * pixelRatio_;
        vs.ids[0] = idFor(h.hoverKind, PickKind::Edge, h.hoverId);
        edge_fs_params_t fs{};
        copy4(fs.color, hidden ? kHiddenEdgeColor : kEdgeColor);
        copy4(fs.hover_color, kHoverColor);
        copy4(fs.select_color, kSelectColor);
        fs.ids[0] = vs.ids[0];
        fs.ids[2] = hidden ? kDashPeriod * pixelRatio_ : 0.0F;
        fs.ids[3] = onlyHighlighted ? 1.0F : 0.0F;

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.edgeQuad;
        bindings.vertex_buffers[1] = g.segments;
        bindings.vertex_buffers[2] = g.segmentSelected;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(hidden ? g.hiddenEdgePipeline : g.edgePipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_edge_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_edge_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, static_cast<int>(g.segmentCount));
    };

    if (g.faces.indexCount > 0 && drawFaces && !transparent) {
        faces(g.meshPipeline, g.faces, /*regions=*/false);
    }

    if (gridVisible_) {
        const Vec3 center = camera_.target();
        grid_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        vs.area[0] = center.x;
        vs.area[1] = center.y;
        vs.area[2] = grid.radius;
        grid_fs_params_t fs{};
        fs.spacing[0] = grid.minor;
        fs.spacing[1] = grid.major;
        fs.spacing[2] = grid.minorOpacity;
        fs.spacing[3] = grid.radius;
        fs.area[0] = center.x;
        fs.area[1] = center.y;
        const Vec3 eye = camera_.eye();
        const Vec3 forward = camera_.forward();
        fs.eye[0] = eye.x;
        fs.eye[1] = eye.y;
        fs.eye[2] = eye.z;
        fs.eye[3] = camera_.projection() == Camera::Projection::Orthographic ? 1.0F : 0.0F;
        fs.forward[0] = forward.x;
        fs.forward[1] = forward.y;
        fs.forward[2] = forward.z;
        copy4(fs.minor_color, kGridMinor);
        copy4(fs.major_color, kGridMajor);
        copy4(fs.x_axis_color, kGridXAxis);
        copy4(fs.y_axis_color, kGridYAxis);

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.pointQuad;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(g.gridPipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_grid_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_grid_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, 1);
    }

    // Transparent faces after the grid, so the grid shows through them.
    if (g.faces.indexCount > 0 && drawFaces && transparent) {
        faces(g.meshBackPipeline, g.faces, /*regions=*/false);
        faces(g.meshFrontPipeline, g.faces, /*regions=*/false);
    }

    if (g.segmentCount > 0) {
        if (style_ == VisualStyle::ShadedHiddenEdges && drawFaces && !transparent) {
            edges(/*hidden=*/true, /*onlyHighlighted=*/false);
        }
        edges(/*hidden=*/false, /*onlyHighlighted=*/style_ == VisualStyle::Shaded);
    }

    if (g.vertexCount > 0) {
        point_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        std::memcpy(vs.viewport, viewport, sizeof(viewport));
        vs.viewport[2] = kVertexRadius * pixelRatio_;
        vs.viewport[3] = kVertexRadiusHighlighted * pixelRatio_;
        vs.ids[0] = idFor(h.hoverKind, PickKind::Vertex, h.hoverId);
        point_fs_params_t fs{};
        copy4(fs.color, kVertexColor);
        copy4(fs.hover_color, kHoverColor);
        copy4(fs.select_color, kSelectColor);
        fs.ids[0] = vs.ids[0];
        fs.ids[2] = style_ == VisualStyle::Shaded ? 1.0F : 0.0F;

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.pointQuad;
        bindings.vertex_buffers[1] = g.vertices;
        bindings.vertex_buffers[2] = g.vertexSelected;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(g.pointPipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_point_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_point_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, static_cast<int>(g.vertexCount));
    }

    // Sketch regions last, over the model, so they can be chosen wherever they lie.
    if (g.regions.indexCount > 0) {
        faces(g.regionPipeline, g.regions, /*regions=*/true);
    }
    if (g.regionSegmentCount > 0) {
        edge_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        std::memcpy(vs.viewport, viewport, sizeof(viewport));
        vs.viewport[2] = kEdgeHalfWidth * pixelRatio_;
        vs.viewport[3] = kEdgeHalfWidth * pixelRatio_;
        edge_fs_params_t fs{};
        copy4(fs.color, kRegionOutline);

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.edgeQuad;
        bindings.vertex_buffers[1] = g.regionSegments;
        bindings.vertex_buffers[2] = g.regionSegmentFlags;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(g.regionOutlinePipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_edge_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_edge_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, static_cast<int>(g.regionSegmentCount));
    }

    // The sketch being edited and the tool's preview, over everything. Picked sketch curves
    // and points highlight like edges and vertices.
    const auto sketchLayer = [&](const SketchLayer& layer, bool preview) {
        const float hoverCurve =
            preview ? 0.0F : idFor(h.hoverKind, PickKind::SketchCurve, h.hoverId);
        const auto curves = [&](sg_buffer segments, sg_buffer flags, std::size_t count,
                                bool construction) {
            if (count == 0) {
                return;
            }
            edge_vs_params_t vs{};
            copyMatrix(vs.view_proj, viewProjection);
            std::memcpy(vs.viewport, viewport, sizeof(viewport));
            vs.viewport[2] = kSketchCurveHalfWidth * pixelRatio_;
            vs.viewport[3] = kEdgeHalfWidthHighlighted * pixelRatio_;
            vs.ids[0] = hoverCurve;
            edge_fs_params_t fs{};
            copy4(fs.color, preview        ? kSketchPreview
                            : construction ? kSketchConstruction
                                           : kSketchCurve);
            copy4(fs.hover_color, kHoverColor);
            copy4(fs.select_color, kSelectColor);
            fs.ids[0] = hoverCurve;
            fs.ids[2] = construction ? kDashPeriod * pixelRatio_ : 0.0F;

            sg_bindings bindings{};
            bindings.vertex_buffers[0] = g.edgeQuad;
            bindings.vertex_buffers[1] = segments;
            bindings.vertex_buffers[2] = flags;
            bindings.index_buffer = g.quadIndices;
            sg_apply_pipeline(g.regionOutlinePipeline);
            sg_apply_bindings(&bindings);
            sg_apply_uniforms(UB_edge_vs_params, SG_RANGE(vs));
            sg_apply_uniforms(UB_edge_fs_params, SG_RANGE(fs));
            sg_draw(0, 6, static_cast<int>(count));
        };
        curves(layer.construction, layer.constructionFlags, layer.constructionIds.size(), true);
        curves(layer.curves, layer.curveFlags, layer.curveIds.size(), false);
        if (layer.pointIds.empty()) {
            return;
        }
        point_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        std::memcpy(vs.viewport, viewport, sizeof(viewport));
        vs.viewport[2] = kSketchPointRadius * pixelRatio_;
        vs.viewport[3] = kVertexRadiusHighlighted * pixelRatio_;
        vs.ids[0] = preview ? 0.0F : idFor(h.hoverKind, PickKind::SketchPoint, h.hoverId);
        point_fs_params_t fs{};
        copy4(fs.color, preview ? kSketchPreview : kSketchPoint);
        copy4(fs.hover_color, kHoverColor);
        copy4(fs.select_color, kSelectColor);
        fs.ids[0] = vs.ids[0];

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.pointQuad;
        bindings.vertex_buffers[1] = layer.points;
        bindings.vertex_buffers[2] = layer.pointFlags;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(g.sketchPointPipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_point_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_point_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, static_cast<int>(layer.pointIds.size()));
    };
    sketchLayer(g.sketch, /*preview=*/false);
    sketchLayer(g.preview, /*preview=*/true);

    sg_end_pass();
    sg_commit();
    stats_.cpuMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
}

}  // namespace facetry::render
