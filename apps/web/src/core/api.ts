// Idiomatic TypeScript API over the core's C API (core/include/facetry_core.h). Runs wherever
// the WebAssembly module runs: in the core Web Worker in the app, and in Node for tests.
// The UI thread never uses this directly; it talks to the worker through CoreClient.

import createFacetryCore, { type FacetryCoreModule } from "./wasm/facetry-core.mjs";

/** The C API major version this wrapper was written for. */
export const SUPPORTED_API_MAJOR = 0;

export const Status = {
  Ok: 0,
  InvalidArgument: 1,
  InvalidHandle: 2,
  Kernel: 3,
  Unsupported: 4,
  Io: 5,
  OutOfMemory: 6,
  Internal: 7,
} as const;

export const LogLevel = { Trace: 0, Debug: 1, Info: 2, Warning: 3, Error: 4 } as const;
export type LogLevel = (typeof LogLevel)[keyof typeof LogLevel];

export const BooleanOp = { Fuse: 0, Cut: 1, Common: 2 } as const;
export type BooleanOp = (typeof BooleanOp)[keyof typeof BooleanOp];

const MeshBuffer = {
  Positions: 0,
  Normals: 1,
  Triangles: 2,
  TriangleFaceIds: 3,
  EdgePoints: 4,
  EdgeOffsets: 5,
  EdgeIds: 6,
  VertexPositions: 7,
  VertexIds: 8,
} as const;

/** A failed core call: `status` is one of {@link Status}. */
export class CoreError extends Error {
  readonly status: number;

  constructor(status: number, message: string) {
    super(message);
    this.name = "CoreError";
    this.status = status;
  }
}

export interface Versions {
  api: { major: number; minor: number };
  core: string;
  kernel: string;
  platform: string;
}

export interface ShapeProperties {
  volume: number;
  area: number;
  bboxMin: [number, number, number];
  bboxMax: [number, number, number];
  solids: number;
  faces: number;
  edges: number;
  vertices: number;
}

export interface TessellationOptions {
  linearDeflection?: number;
  angularDeflection?: number;
  relative?: boolean;
}

/** Display mesh. Buffers are copies owned by JavaScript and can be transferred to another thread. */
export interface MeshData {
  positions: Float32Array;
  normals: Float32Array;
  triangles: Uint32Array;
  triangleFaceIds: Uint32Array;
  edgePoints: Float32Array;
  edgeOffsets: Uint32Array;
  edgeIds: Uint32Array;
  vertexPositions: Float32Array;
  vertexIds: Uint32Array;
}

export interface LoadOptions {
  onLog?: (level: LogLevel, message: string) => void;
  logLevel?: LogLevel;
}

export class FacetryCore {
  readonly #m: FacetryCoreModule;
  readonly #context: number;
  // Scratch memory for output parameters (handles, pointers, counts, properties).
  readonly #scratch: number;
  #logCallback = 0;

  private constructor(module: FacetryCoreModule, context: number, scratch: number) {
    this.#m = module;
    this.#context = context;
    this.#scratch = scratch;
  }

  static async load(options: LoadOptions = {}): Promise<FacetryCore> {
    const m = await createFacetryCore();
    const scratch = m._malloc(128);
    m._facetry_api_version(scratch, scratch + 4);
    const major = m.HEAPU32[scratch >> 2] ?? -1;
    if (major !== SUPPORTED_API_MAJOR) {
      throw new CoreError(
        Status.Unsupported,
        `Core API ${major}.x does not match the web app (expects ${SUPPORTED_API_MAJOR}.x)`,
      );
    }
    if (m._facetry_context_create(scratch) !== Status.Ok) {
      throw new CoreError(Status.OutOfMemory, "Could not create a core context");
    }
    const core = new FacetryCore(m, m.HEAPU32[scratch >> 2] ?? 0, scratch);
    if (options.onLog) {
      core.#setLogCallback(options.onLog);
    }
    m._facetry_set_log_level(options.logLevel ?? LogLevel.Info);
    return core;
  }

  versions(): Versions {
    const m = this.#m;
    m._facetry_api_version(this.#scratch, this.#scratch + 4);
    return {
      api: { major: this.#u32(this.#scratch), minor: this.#u32(this.#scratch + 4) },
      core: m.UTF8ToString(m._facetry_core_version()),
      kernel: m.UTF8ToString(m._facetry_kernel_version()),
      platform: m.UTF8ToString(m._facetry_platform()),
    };
  }

  makeBox(dx: number, dy: number, dz: number): number {
    this.#check(this.#m._facetry_shape_make_box(this.#context, dx, dy, dz, this.#scratch));
    return this.#u32(this.#scratch);
  }

  makeCylinder(x: number, y: number, radius: number, height: number): number {
    this.#check(
      this.#m._facetry_shape_make_cylinder(this.#context, x, y, radius, height, this.#scratch),
    );
    return this.#u32(this.#scratch);
  }

  boolean(op: BooleanOp, shape: number, tool: number): number {
    this.#check(this.#m._facetry_shape_boolean(this.#context, op, shape, tool, this.#scratch));
    return this.#u32(this.#scratch);
  }

  fillet(shape: number, edgeIds: readonly number[], radius: number): number {
    const m = this.#m;
    const ids = m._malloc(Math.max(4, edgeIds.length * 4));
    try {
      m.HEAPU32.set(edgeIds, ids >> 2);
      this.#check(
        m._facetry_shape_fillet(this.#context, shape, ids, edgeIds.length, radius, this.#scratch),
      );
      return this.#u32(this.#scratch);
    } finally {
      m._free(ids);
    }
  }

  properties(shape: number): ShapeProperties {
    this.#check(this.#m._facetry_shape_properties_get(this.#context, shape, this.#scratch));
    // facetry_shape_properties: 12 doubles.
    const d = this.#m.HEAPF64.subarray(this.#scratch >> 3, (this.#scratch >> 3) + 12);
    const at = (i: number) => d[i] ?? Number.NaN;
    return {
      volume: at(0),
      area: at(1),
      bboxMin: [at(2), at(3), at(4)],
      bboxMax: [at(5), at(6), at(7)],
      solids: at(8),
      faces: at(9),
      edges: at(10),
      vertices: at(11),
    };
  }

  tessellate(shape: number, options: TessellationOptions = {}): MeshData {
    const m = this.#m;
    this.#check(
      m._facetry_shape_tessellate(
        this.#context,
        shape,
        options.linearDeflection ?? 0.05,
        options.angularDeflection ?? 0.35,
        options.relative ? 1 : 0,
        this.#scratch,
      ),
    );
    const mesh = this.#u32(this.#scratch);
    try {
      return {
        positions: this.#floats(mesh, MeshBuffer.Positions),
        normals: this.#floats(mesh, MeshBuffer.Normals),
        triangles: this.#uints(mesh, MeshBuffer.Triangles),
        triangleFaceIds: this.#uints(mesh, MeshBuffer.TriangleFaceIds),
        edgePoints: this.#floats(mesh, MeshBuffer.EdgePoints),
        edgeOffsets: this.#uints(mesh, MeshBuffer.EdgeOffsets),
        edgeIds: this.#uints(mesh, MeshBuffer.EdgeIds),
        vertexPositions: this.#floats(mesh, MeshBuffer.VertexPositions),
        vertexIds: this.#uints(mesh, MeshBuffer.VertexIds),
      };
    } finally {
      m._facetry_mesh_release(this.#context, mesh);
    }
  }

  release(shape: number): void {
    this.#check(this.#m._facetry_shape_release(this.#context, shape));
  }

  /** Releases the context and every shape it owns. The instance must not be used afterwards. */
  dispose(): void {
    const m = this.#m;
    m._facetry_context_destroy(this.#context);
    if (this.#logCallback !== 0) {
      m._facetry_set_log_callback(0, 0);
      m.removeFunction(this.#logCallback);
      this.#logCallback = 0;
    }
    m._free(this.#scratch);
  }

  #setLogCallback(onLog: (level: LogLevel, message: string) => void): void {
    const m = this.#m;
    this.#logCallback = m.addFunction((level: number, message: number) => {
      onLog(level as LogLevel, m.UTF8ToString(message));
    }, "viii");
    m._facetry_set_log_callback(this.#logCallback, 0);
  }

  #check(status: number): void {
    if (status !== Status.Ok) {
      throw new CoreError(
        status,
        this.#m.UTF8ToString(this.#m._facetry_last_error_message(this.#context)),
      );
    }
  }

  // Heap views are read fresh on every access: they are replaced when wasm memory grows.
  #u32(pointer: number): number {
    return this.#m.HEAPU32[pointer >> 2] ?? 0;
  }

  #buffer(mesh: number, kind: number): { pointer: number; count: number } {
    this.#check(
      this.#m._facetry_mesh_buffer(this.#context, mesh, kind, this.#scratch, this.#scratch + 4),
    );
    return { pointer: this.#u32(this.#scratch), count: this.#u32(this.#scratch + 4) };
  }

  #floats(mesh: number, kind: number): Float32Array {
    const { pointer, count } = this.#buffer(mesh, kind);
    return this.#m.HEAPF32.slice(pointer >> 2, (pointer >> 2) + count);
  }

  #uints(mesh: number, kind: number): Uint32Array {
    const { pointer, count } = this.#buffer(mesh, kind);
    return this.#m.HEAPU32.slice(pointer >> 2, (pointer >> 2) + count);
  }
}
