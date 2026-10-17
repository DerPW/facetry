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
in float selected;  // 1 if the vertex's face is selected

out vec3 v_normal;
flat out float v_face_id;
flat out float v_selected;

void main() {
    gl_Position = view_proj * vec4(position, 1.0);
    v_normal = normal;
    v_face_id = face_id;
    v_selected = selected;
}
@end

@fs mesh_fs
layout(binding=1) uniform mesh_fs_params {
    vec4 key_light;     // xyz: direction towards the key light, world space
    vec4 base_color;
    vec4 hover_color;   // a: blend amount
    vec4 select_color;  // a: blend amount
    // x: hovered face, y: 1 for flat (unlit) color, z: 1 when the whole body is hovered,
    // w: alpha of hovered and selected faces if above 0 (sketch regions), else base_color.a
    vec4 ids;
};

in vec3 v_normal;
flat in float v_face_id;
flat in float v_selected;
out vec4 frag_color;

void main() {
    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    // Z-up world: the sky lights faces pointing up, the ground faces pointing down.
    vec3 ambient = mix(vec3(0.52, 0.52, 0.55), vec3(1.0, 1.0, 1.0), 0.5 + 0.5 * n.z);
    float key = max(dot(n, key_light.xyz), 0.0);
    vec3 color = ids.y > 0.5 ? base_color.rgb : base_color.rgb * (0.62 * ambient + 0.45 * key);
    float alpha = base_color.a;  // the body's opacity
    if (v_selected > 0.5) {
        color = mix(color, select_color.rgb, select_color.a);
        alpha = ids.w > 0.0 ? ids.w : alpha;
    } else if (ids.z > 0.5 || (ids.x > 0.5 && abs(v_face_id - ids.x) < 0.5)) {
        color = mix(color, hover_color.rgb, hover_color.a);
        alpha = ids.w > 0.0 ? mix(base_color.a, ids.w, 0.6) : alpha;
    }
    frag_color = vec4(color, alpha);
}
@end

@program mesh mesh_vs mesh_fs

// ---- Edges: segments expanded to screen-space quads of constant pixel width ------------------

@vs edge_vs
layout(binding=0) uniform edge_vs_params {
    mat4 view_proj;
    vec4 viewport;  // xy: framebuffer size in pixels, z: half width, w: half width highlighted
    vec4 ids;       // x: hovered edge
};

in vec2 corner;  // x: 0 = segment start, 1 = end; y: -1/+1 side
in vec3 p0;
in vec3 p1;
in float edge_id;
in float edge_selected;

flat out float v_edge_id;
flat out float v_selected;
out float v_along;  // pixels along the segment on the screen, for dashes

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
    bool highlighted = (ids.x > 0.5 && abs(edge_id - ids.x) < 0.5) || edge_selected > 0.5;
    float half_width = highlighted ? viewport.w : viewport.z;
    bool at_end = corner.x > 0.5;
    vec4 c = at_end ? c1 : c0;
    vec2 offset = (nrm * corner.y + dir * (at_end ? 1.0 : -1.0)) * half_width;
    c.xy += offset / half_size * c.w;
    gl_Position = c;
    v_edge_id = edge_id;
    v_selected = edge_selected;
    v_along = at_end ? len : 0.0;
}
@end

@fs edge_fs
layout(binding=1) uniform edge_fs_params {
    vec4 color;
    vec4 hover_color;
    vec4 select_color;
    vec4 ids;  // x: hovered edge, z: dash period in pixels (0 solid),
               // w: 1 to draw only the hovered and selected edges
};

flat in float v_edge_id;
flat in float v_selected;
in float v_along;
out vec4 frag_color;

void main() {
    bool selected = v_selected > 0.5;
    bool hovered = ids.x > 0.5 && abs(v_edge_id - ids.x) < 0.5;
    if (ids.w > 0.5 && !selected && !hovered) {
        discard;
    }
    if (ids.z > 0.0 && mod(v_along, ids.z) > ids.z * 0.55) {
        discard;
    }
    vec4 c = color;
    if (selected) {
        c = select_color;
    } else if (hovered) {
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
    vec4 ids;       // x: hovered vertex
};

in vec2 corner;  // -1..1
in vec3 center;
in float vertex_id;
in float vertex_selected;

out vec2 v_corner;
flat out float v_vertex_id;
flat out float v_selected;

void main() {
    vec4 c = view_proj * vec4(center, 1.0);
    bool highlighted = (ids.x > 0.5 && abs(vertex_id - ids.x) < 0.5) || vertex_selected > 0.5;
    float radius = highlighted ? viewport.w : viewport.z;
    c.xy += corner * radius / (viewport.xy * 0.5) * c.w;
    gl_Position = c;
    v_corner = corner;
    v_vertex_id = vertex_id;
    v_selected = vertex_selected;
}
@end

@fs point_fs
layout(binding=1) uniform point_fs_params {
    vec4 color;
    vec4 hover_color;
    vec4 select_color;
    vec4 ids;  // x: hovered vertex, z: 1 to draw only the hovered and selected vertices
};

in vec2 v_corner;
flat in float v_vertex_id;
flat in float v_selected;
out vec4 frag_color;

void main() {
    if (dot(v_corner, v_corner) > 1.0) {
        discard;
    }
    bool selected = v_selected > 0.5;
    bool hovered = ids.x > 0.5 && abs(v_vertex_id - ids.x) < 0.5;
    if (ids.z > 0.5 && !selected && !hovered) {
        discard;
    }
    vec4 c = color;
    if (selected) {
        c = select_color;
    } else if (hovered) {
        c = hover_color;
    }
    frag_color = c;
}
@end

@program point point_vs point_fs

// ---- Ground grid: lines on the XY plane, anti-aliased with screen-space derivatives -----------

@vs grid_vs
layout(binding=0) uniform grid_vs_params {
    mat4 view_proj;
    vec4 area;  // xy: center on the XY plane, z: half the side of the drawn square
};

in vec2 corner;  // -1..1
out vec2 v_world;

void main() {
    v_world = area.xy + corner * area.z;
    gl_Position = view_proj * vec4(v_world, 0.0, 1.0);
}
@end

@fs grid_fs
layout(binding=1) uniform grid_fs_params {
    vec4 spacing;  // x: minor, y: major, z: minor opacity, w: half the side of the square
    vec4 area;     // xy: center
    vec4 eye;      // xyz: camera position; w: 1 for orthographic
    vec4 forward;  // xyz: view direction
    vec4 minor_color;
    vec4 major_color;
    vec4 x_axis_color;
    vec4 y_axis_color;
};

in vec2 v_world;
out vec4 frag_color;

// 1 on a line through multiples of `step`, falling off over one pixel.
float grid_line(vec2 coord) {
    vec2 width = max(fwidth(coord), vec2(1e-6));
    vec2 distance = abs(fract(coord - 0.5) - 0.5) / width;
    return 1.0 - min(min(distance.x, distance.y), 1.0);
}

vec4 stronger(vec4 a, vec4 b) {
    return b.a > a.a ? b : a;
}

void main() {
    vec4 c = vec4(minor_color.rgb, minor_color.a * spacing.z * grid_line(v_world / spacing.x));
    c = stronger(c, vec4(major_color.rgb, major_color.a * grid_line(v_world / spacing.y)));
    // The X axis (y = 0) red and the Y axis (x = 0) green, a little wider than the lines.
    vec2 width = max(fwidth(v_world) * 1.5, vec2(1e-6));
    float on_x = 1.0 - min(abs(v_world.y) / width.y, 1.0);
    float on_y = 1.0 - min(abs(v_world.x) / width.x, 1.0);
    c = stronger(c, vec4(x_axis_color.rgb, x_axis_color.a * on_x));
    c = stronger(c, vec4(y_axis_color.rgb, y_axis_color.a * on_y));
    // Fade toward the edge of the square and where the plane is seen edge-on.
    float fade = 1.0 - smoothstep(0.35, 1.0, length(v_world - area.xy) / spacing.w);
    vec3 to_eye = eye.w > 0.5 ? -forward.xyz : normalize(eye.xyz - vec3(v_world, 0.0));
    fade *= smoothstep(0.0, 0.25, abs(to_eye.z));
    c.a *= fade;
    if (c.a < 0.004) {
        discard;
    }
    frag_color = c;
}
@end

@program grid grid_vs grid_fs
