// Types for the core WebAssembly module (facetry-core.mjs), which CMake builds from
// core/wasm and copies next to this file. The functions mirror core/include/facetry_core.h:
// pointers and handles are numbers, statuses are numbers (0 = ok).

export interface FacetryCoreModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  HEAPF64: Float64Array;
  UTF8ToString(pointer: number): string;
  addFunction(fn: (...args: number[]) => unknown, signature: string): number;
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

  _facetry_session_create(context: number, documentJson: number, out: number): number;
  _facetry_session_save(context: number, session: number, jsonOut: number): number;
  _facetry_session_open_file(context: number, data: number, size: number, out: number): number;
  _facetry_session_save_file(
    context: number,
    session: number,
    thumbnail: number,
    thumbnailSize: number,
    dataOut: number,
    sizeOut: number,
  ): number;
  _facetry_session_apply(context: number, session: number, editJson: number): number;
  _facetry_session_begin_transaction(context: number, session: number, name: number): number;
  _facetry_session_end_transaction(context: number, session: number, commit: number): number;
  _facetry_session_undo(context: number, session: number, changedOut: number): number;
  _facetry_session_redo(context: number, session: number, changedOut: number): number;
  _facetry_session_regenerate(
    context: number,
    session: number,
    progress: number,
    userData: number,
  ): number;
  _facetry_session_state(context: number, session: number, jsonOut: number): number;
  _facetry_session_body_shape(context: number, session: number, body: number, out: number): number;
  _facetry_session_sketch_regions(
    context: number,
    session: number,
    sketch: number,
    out: number,
  ): number;
  _facetry_session_element_names(
    context: number,
    session: number,
    body: number,
    jsonOut: number,
  ): number;
  _facetry_session_make_reference(
    context: number,
    session: number,
    body: number,
    kind: number,
    elementId: number,
    jsonOut: number,
  ): number;
  _facetry_session_release(context: number, session: number): number;
}

declare function createFacetryCore(options?: Record<string, unknown>): Promise<FacetryCoreModule>;
export default createFacetryCore;
