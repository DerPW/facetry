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
// Dashes of hidden edges, in points.
constexpr float kDashPeriod = 7.0F;
// Below this the faces count as transparent.
constexpr float kOpaque = 0.999F;
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

    // Shared unit quads for instanced edges and vertex dots.
    sg_buffer edgeQuad{};
    sg_buffer pointQuad{};
    sg_buffer quadIndices{};

    sg_buffer positions{};
    sg_buffer normals{};
    sg_buffer faceIds{};
    sg_buffer triangles{};
    sg_buffer segments{};
    sg_buffer vertices{};
    std::uint32_t indexCount = 0;
    std::uint32_t segmentCount = 0;
    std::uint32_t vertexCount = 0;

    void releaseMesh() {
        destroy(positions);
        destroy(normals);
        destroy(faceIds);
        destroy(triangles);
        destroy(segments);
        destroy(vertices);
        indexCount = segmentCount = vertexCount = 0;
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
        desc.index_type = SG_INDEXTYPE_UINT16;
        desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        desc.depth.write_enabled = true;
        desc.cull_mode = SG_CULLMODE_NONE;
        desc.label = "vertices";
        g.pointPipeline = sg_make_pipeline(&desc);
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
        sg_query_pipeline_state(g.hiddenEdgePipeline) != SG_RESOURCESTATE_VALID) {
        throw std::runtime_error("Creating the viewport shaders failed");
    }
}

Renderer::~Renderer() {
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

    const std::size_t vertexCount = mesh.positions.size() / 3;
    if (vertexCount > 0 && !mesh.triangles.empty()) {
        // Mesh vertices belong to exactly one face, so the face ID can be a vertex attribute.
        std::vector<float> faceIds(vertexCount, 0.0F);
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            faceIds[mesh.triangles[i]] = static_cast<float>(mesh.triangleFaceIds[i / 3]);
        }
        g.positions = makeBuffer(mesh.positions, false, "positions");
        g.normals = makeBuffer(mesh.normals, false, "normals");
        g.faceIds = makeBuffer(faceIds, false, "face ids");
        g.triangles = makeBuffer(mesh.triangles, true, "triangles");
        g.indexCount = static_cast<std::uint32_t>(mesh.triangles.size());
    }

    std::vector<float> segments;
    for (std::size_t e = 0; e + 1 < mesh.edgeOffsets.size() && e < mesh.edgeIds.size(); ++e) {
        for (std::uint32_t k = mesh.edgeOffsets[e]; k + 1 < mesh.edgeOffsets[e + 1]; ++k) {
            const float* a = &mesh.edgePoints[std::size_t{k} * 3];
            segments.insert(segments.end(), {a[0], a[1], a[2], a[3], a[4], a[5],
                                             static_cast<float>(mesh.edgeIds[e])});
        }
    }
    if (!segments.empty()) {
        g.segments = makeBuffer(segments, false, "edge segments");
        g.segmentCount = static_cast<std::uint32_t>(segments.size() / 7);
    }

    std::vector<float> vertices;
    for (std::size_t v = 0; v < mesh.vertexIds.size(); ++v) {
        const float* p = &mesh.vertexPositions[v * 3];
        vertices.insert(vertices.end(), {p[0], p[1], p[2], static_cast<float>(mesh.vertexIds[v])});
    }
    if (!vertices.empty()) {
        g.vertices = makeBuffer(vertices, false, "vertices");
        g.vertexCount = static_cast<std::uint32_t>(mesh.vertexIds.size());
    }

    stats_.triangles = g.indexCount / 3;
    stats_.edgeSegments = g.segmentCount;
    stats_.vertices = g.vertexCount;
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

void Renderer::setStyle(VisualStyle style, float opacity) {
    style_ = style;
    opacity_ = std::clamp(opacity, 0.0F, 1.0F);
}

bool Renderer::picksThroughFaces() const {
    return style_ == VisualStyle::Wireframe || opacity_ < kOpaque;
}

void Renderer::fitSelection() {
    Bounds bounds = pick_.elementBounds(highlight_.selectKind, highlight_.selectId);
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

    const bool drawFaces = style_ != VisualStyle::Wireframe && opacity_ > 0.001F;
    const bool transparent = opacity_ < kOpaque;

    const auto faces = [&](sg_pipeline pipeline) {
        mesh_vs_params_t vs{};
        copyMatrix(vs.view_proj, viewProjection);
        mesh_fs_params_t fs{};
        const Vec3 light =
            normalize(camera_.up() * 0.8F - camera_.forward() * 0.6F - camera_.right() * 0.3F);
        fs.key_light[0] = light.x;
        fs.key_light[1] = light.y;
        fs.key_light[2] = light.z;
        copy4(fs.base_color, kBaseColor);
        fs.base_color[3] = opacity_;
        copy4(fs.hover_color, kFaceHoverTint);
        copy4(fs.select_color, kFaceSelectTint);
        fs.ids[0] = idFor(h.hoverKind, PickKind::Face, h.hoverId);
        fs.ids[1] = idFor(h.selectKind, PickKind::Face, h.selectId);

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.positions;
        bindings.vertex_buffers[1] = g.normals;
        bindings.vertex_buffers[2] = g.faceIds;
        bindings.index_buffer = g.triangles;
        sg_apply_pipeline(pipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_mesh_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_mesh_fs_params, SG_RANGE(fs));
        sg_draw(0, static_cast<int>(g.indexCount), 1);
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
        vs.ids[1] = idFor(h.selectKind, PickKind::Edge, h.selectId);
        edge_fs_params_t fs{};
        copy4(fs.color, hidden ? kHiddenEdgeColor : kEdgeColor);
        copy4(fs.hover_color, kHoverColor);
        copy4(fs.select_color, kSelectColor);
        fs.ids[0] = vs.ids[0];
        fs.ids[1] = vs.ids[1];
        fs.ids[2] = hidden ? kDashPeriod * pixelRatio_ : 0.0F;
        fs.ids[3] = onlyHighlighted ? 1.0F : 0.0F;

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.edgeQuad;
        bindings.vertex_buffers[1] = g.segments;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(hidden ? g.hiddenEdgePipeline : g.edgePipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_edge_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_edge_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, static_cast<int>(g.segmentCount));
    };

    if (g.indexCount > 0 && drawFaces && !transparent) {
        faces(g.meshPipeline);
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
    if (g.indexCount > 0 && drawFaces && transparent) {
        faces(g.meshBackPipeline);
        faces(g.meshFrontPipeline);
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
        vs.ids[1] = idFor(h.selectKind, PickKind::Vertex, h.selectId);
        point_fs_params_t fs{};
        copy4(fs.color, kVertexColor);
        copy4(fs.hover_color, kHoverColor);
        copy4(fs.select_color, kSelectColor);
        fs.ids[0] = vs.ids[0];
        fs.ids[1] = vs.ids[1];
        fs.ids[2] = style_ == VisualStyle::Shaded ? 1.0F : 0.0F;

        sg_bindings bindings{};
        bindings.vertex_buffers[0] = g.pointQuad;
        bindings.vertex_buffers[1] = g.vertices;
        bindings.index_buffer = g.quadIndices;
        sg_apply_pipeline(g.pointPipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_point_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_point_fs_params, SG_RANGE(fs));
        sg_draw(0, 6, static_cast<int>(g.vertexCount));
    }

    sg_end_pass();
    sg_commit();
    stats_.cpuMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
}

}  // namespace facetry::render
