// Types for the renderer WebAssembly module (facetry-render.mjs), which CMake builds from
// render/wasm and copies next to this file. Mirrors render/include/facetry_render.h.

export interface FacetryRenderModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  UTF8ToString(pointer: number): string;
  stringToUTF8(text: string, pointer: number, maxBytes: number): void;
  lengthBytesUTF8(text: string): number;
  _malloc(size: number): number;
  _free(pointer: number): void;

  _facetry_renderer_create_webgl(canvasSelector: number, antialias: number, out: number): number;
  _facetry_renderer_destroy(renderer: number): void;
  _facetry_renderer_last_error(renderer: number): number;
  _facetry_renderer_set_mesh(renderer: number, mesh: number): number;
  _facetry_renderer_set_viewport(
    renderer: number,
    width: number,
    height: number,
    pixelRatio: number,
  ): void;
  _facetry_renderer_set_style(renderer: number, style: number, opacity: number): void;
  _facetry_renderer_set_grid(renderer: number, visible: number, unitMillimeters: number): void;
  _facetry_camera_fit(renderer: number): void;
  _facetry_camera_fit_selection(renderer: number): void;
  _facetry_camera_home(renderer: number, animate: number): void;
  _facetry_camera_view_from(
    renderer: number,
    x: number,
    y: number,
    z: number,
    animate: number,
  ): void;
  _facetry_viewcube_cells(renderer: number, size: number, out: number, capacity: number): number;
  _facetry_viewcube_hit(renderer: number, size: number, x: number, y: number): number;
  _facetry_camera_set_orthographic(renderer: number, orthographic: number): void;
  _facetry_camera_orbit_begin(renderer: number, x: number, y: number): void;
  _facetry_camera_orbit(renderer: number, dx: number, dy: number): void;
  _facetry_camera_pan(renderer: number, dx: number, dy: number): void;
  _facetry_camera_zoom(renderer: number, factor: number, x: number, y: number): void;
  _facetry_navigation_drag(preset: number, button: number, modifiers: number): number;
  _facetry_navigation_scroll(preset: number, modifiers: number, trackpad: number): number;
  _facetry_renderer_pick(
    renderer: number,
    x: number,
    y: number,
    radius: number,
    out: number,
  ): number;
  _facetry_renderer_set_hover(renderer: number, kind: number, id: number): void;
  _facetry_renderer_set_selection(renderer: number, kind: number, id: number): void;
  _facetry_renderer_select(renderer: number, kind: number, id: number, mode: number): void;
  _facetry_renderer_selection(renderer: number, out: number, capacity: number): number;
  _facetry_renderer_box_select(
    renderer: number,
    x0: number,
    y0: number,
    x1: number,
    y1: number,
    mode: number,
  ): number;
  _facetry_renderer_set_pick_filter(renderer: number, filter: number): void;
  _facetry_renderer_draw(renderer: number, target: number): number;
  _facetry_renderer_stats(renderer: number, out: number): void;
}

declare function createFacetryRender(
  options?: Record<string, unknown>,
): Promise<FacetryRenderModule>;
export default createFacetryRender;
