// Types for the core WebAssembly module (facetry-core.mjs), which CMake builds from
// core/wasm and copies next to this file. The functions mirror core/include/facetry_core.h:
// pointers and handles are numbers, statuses are numbers (0 = ok).

export interface FacetryCoreModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  HEAPF64: Float64Array;
  UTF8ToString(pointer: number): string;
  addFunction(fn: (...args: number[]) => void, signature: string): number;
  removeFunction(pointer: number): void;
  _malloc(size: number): number;
  _free(pointer: number): void;

  _facetry_api_version(majorOut: number, minorOut: number): void;
  _facetry_core_version(): number;
  _facetry_kernel_version(): number;
  _facetry_platform(): number;

  _facetry_context_create(contextOut: number): number;
  _facetry_context_destroy(context: number): void;
  _facetry_last_error_message(context: number): number;
  _facetry_last_error_status(context: number): number;

  _facetry_set_log_callback(callback: number, userData: number): void;
  _facetry_set_log_level(level: number): void;

  _facetry_shape_make_box(context: number, dx: number, dy: number, dz: number, out: number): number;
  _facetry_shape_make_cylinder(
    context: number,
    x: number,
    y: number,
    radius: number,
    height: number,
    out: number,
  ): number;
  _facetry_shape_boolean(
    context: number,
    op: number,
    shape: number,
    tool: number,
    out: number,
  ): number;
  _facetry_shape_fillet(
    context: number,
    shape: number,
    edgeIds: number,
    edgeCount: number,
    radius: number,
    out: number,
  ): number;
  _facetry_shape_properties_get(context: number, shape: number, out: number): number;
  _facetry_shape_release(context: number, shape: number): number;
  _facetry_shape_tessellate(
    context: number,
    shape: number,
    linearDeflection: number,
    angularDeflection: number,
    relative: number,
    out: number,
  ): number;
  _facetry_mesh_buffer(
    context: number,
    mesh: number,
    kind: number,
    dataOut: number,
    countOut: number,
  ): number;
  _facetry_mesh_release(context: number, mesh: number): number;
}

declare function createFacetryCore(options?: Record<string, unknown>): Promise<FacetryCoreModule>;
export default createFacetryCore;
