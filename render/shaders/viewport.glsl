// Viewport shaders in sokol-shdc's annotated GLSL. render/shaders/generate.sh compiles them into
// render/src/shaders/viewport.glsl.h for Metal (macOS, iOS, simulator) and WebGL2.
//
// IDs (faces, edges, vertices) are floats: exact up to 2^24, compared with a 0.5 tolerance.
// An ID of 0 means "nothing hovered/selected".

// ---- Faces: hemisphere light plus a key light, hover/selection tint --------------------------

@vs mesh_vs
layout(binding=0) uniform mesh_vs_params {
    mat4 view_proj;
};

in vec3 position;
in vec3 normal;
in float face_id;

out vec3 v_normal;
flat out float v_face_id;

void main() {
    gl_Position = view_proj * vec4(position, 1.0);
    v_normal = normal;
    v_face_id = face_id;
}
@end

@fs mesh_fs
layout(binding=1) uniform mesh_fs_params {
    vec4 key_light;     // xyz: direction towards the key light, world space
    vec4 base_color;
    vec4 hover_color;   // a: blend amount
    vec4 select_color;  // a: blend amount
    vec4 ids;           // x: hovered face, y: selected face
};

in vec3 v_normal;
flat in float v_face_id;
out vec4 frag_color;

void main() {
    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    // Z-up world: the sky lights faces pointing up, the ground faces pointing down.
    vec3 ambient = mix(vec3(0.52, 0.52, 0.55), vec3(1.0, 1.0, 1.0), 0.5 + 0.5 * n.z);
    float key = max(dot(n, key_light.xyz), 0.0);
    vec3 color = base_color.rgb * (0.62 * ambient + 0.45 * key);
    if (ids.y > 0.5 && abs(v_face_id - ids.y) < 0.5) {
        color = mix(color, select_color.rgb, select_color.a);
    } else if (ids.x > 0.5 && abs(v_face_id - ids.x) < 0.5) {
        color = mix(color, hover_color.rgb, hover_color.a);
    }
    frag_color = vec4(color, 1.0);
}
@end

@program mesh mesh_vs mesh_fs

// ---- Edges: segments expanded to screen-space quads of constant pixel width ------------------

@vs edge_vs
layout(binding=0) uniform edge_vs_params {
    mat4 view_proj;
    vec4 viewport;  // xy: framebuffer size in pixels, z: half width, w: half width highlighted
    vec4 ids;       // x: hovered edge, y: selected edge
};

in vec2 corner;  // x: 0 = segment start, 1 = end; y: -1/+1 side
in vec3 p0;
in vec3 p1;
in float edge_id;

flat out float v_edge_id;

void main() {
    vec4 c0 = view_proj * vec4(p0, 1.0);
    vec4 c1 = view_proj * vec4(p1, 1.0);
    vec2 half_size = viewport.xy * 0.5;
    vec2 s0 = c0.xy / max(c0.w, 1e-6) * half_size;
    vec2 s1 = c1.xy / max(c1.w, 1e-6) * half_size;
    vec2 d = s1 - s0;
    float len = length(d);
    vec2 dir = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 nrm = vec2(-dir.y, dir.x);
    bool highlighted = (ids.x > 0.5 && abs(edge_id - ids.x) < 0.5) ||
                       (ids.y > 0.5 && abs(edge_id - ids.y) < 0.5);
    float half_width = highlighted ? viewport.w : viewport.z;
    bool at_end = corner.x > 0.5;
    vec4 c = at_end ? c1 : c0;
    vec2 offset = (nrm * corner.y + dir * (at_end ? 1.0 : -1.0)) * half_width;
    c.xy += offset / half_size * c.w;
    gl_Position = c;
    v_edge_id = edge_id;
}
@end

@fs edge_fs
layout(binding=1) uniform edge_fs_params {
    vec4 color;
    vec4 hover_color;
    vec4 select_color;
    vec4 ids;  // x: hovered edge, y: selected edge
};

flat in float v_edge_id;
out vec4 frag_color;

void main() {
    vec4 c = color;
    if (ids.y > 0.5 && abs(v_edge_id - ids.y) < 0.5) {
        c = select_color;
    } else if (ids.x > 0.5 && abs(v_edge_id - ids.x) < 0.5) {
        c = hover_color;
    }
    frag_color = c;
}
@end

@program edge edge_vs edge_fs

// ---- Vertices: round dots of constant pixel size ----------------------------------------------

@vs point_vs
layout(binding=0) uniform point_vs_params {
    mat4 view_proj;
    vec4 viewport;  // xy: framebuffer size in pixels, z: radius, w: radius highlighted
    vec4 ids;       // x: hovered vertex, y: selected vertex
};

in vec2 corner;  // -1..1
in vec3 center;
in float vertex_id;

out vec2 v_corner;
flat out float v_vertex_id;

void main() {
    vec4 c = view_proj * vec4(center, 1.0);
    bool highlighted = (ids.x > 0.5 && abs(vertex_id - ids.x) < 0.5) ||
                       (ids.y > 0.5 && abs(vertex_id - ids.y) < 0.5);
    float radius = highlighted ? viewport.w : viewport.z;
    c.xy += corner * radius / (viewport.xy * 0.5) * c.w;
    gl_Position = c;
    v_corner = corner;
    v_vertex_id = vertex_id;
}
@end

@fs point_fs
layout(binding=1) uniform point_fs_params {
    vec4 color;
    vec4 hover_color;
    vec4 select_color;
    vec4 ids;  // x: hovered vertex, y: selected vertex
};

in vec2 v_corner;
flat in float v_vertex_id;
out vec4 frag_color;

void main() {
    if (dot(v_corner, v_corner) > 1.0) {
        discard;
    }
    vec4 c = color;
    if (ids.y > 0.5 && abs(v_vertex_id - ids.y) < 0.5) {
        c = select_color;
    } else if (ids.x > 0.5 && abs(v_vertex_id - ids.x) < 0.5) {
        c = hover_color;
    }
    frag_color = c;
}
@end

@program point point_vs point_fs
