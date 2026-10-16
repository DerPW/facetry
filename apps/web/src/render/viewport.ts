// The 3D viewport: the shared C++ renderer (render/include/facetry_render.h) on WebGL2, driven by
// pointer and wheel input. Runs on the UI thread; meshes come from the core worker.

import type { MeshData } from "../core/api";
import createFacetryRender, { type FacetryRenderModule } from "./wasm/facetry-render.mjs";

export const PickKind = { None: 0, Face: 1, Edge: 2, Vertex: 3 } as const;
export type PickKind = (typeof PickKind)[keyof typeof PickKind];

export interface Pick {
  kind: PickKind;
  id: number;
}

export interface FrameStats {
  /** Average time between frames over the last second, in milliseconds. */
  frameMilliseconds: number;
  /** CPU time of the last draw call in the renderer, in milliseconds. */
  cpuMilliseconds: number;
  triangles: number;
  edgeSegments: number;
}

/** Pick radius in CSS pixels per input type: fingers need more room than a pen or mouse. */
const PICK_RADIUS: Record<string, number> = { mouse: 6, pen: 8, touch: 18 };
/** Pointer movement (CSS pixels) below which a press counts as a click/tap. */
const CLICK_SLOP = 4;

export class Viewport {
  readonly #m: FacetryRenderModule;
  readonly #renderer: number;
  readonly #canvas: HTMLCanvasElement;
  readonly #scratch: number;
  readonly #pointers = new Map<number, { x: number; y: number; startX: number; startY: number }>();
  #gesture: "none" | "orbit" | "pan" | "touch" = "none";
  #hover: Pick = { kind: PickKind.None, id: 0 };
  #selection: Pick = { kind: PickKind.None, id: 0 };
  #frameTimes: number[] = [];
  #lastFrame = 0;
  #disposed = false;

  /** Called whenever the selection changes. */
  onSelectionChange: (selection: Pick) => void = () => {};
  /** Called about once per second with frame statistics. */
  onStats: (stats: FrameStats) => void = () => {};

  private constructor(m: FacetryRenderModule, renderer: number, canvas: HTMLCanvasElement) {
    this.#m = m;
    this.#renderer = renderer;
    this.#canvas = canvas;
    this.#scratch = m._malloc(64);
  }

  static async create(canvas: HTMLCanvasElement): Promise<Viewport> {
    if (!canvas.id) {
      throw new Error("The viewport canvas needs an id");
    }
    const m = await createFacetryRender({ canvas });
    const selector = `#${canvas.id}`;
    const length = m.lengthBytesUTF8(selector) + 1;
    const text = m._malloc(length);
    const out = m._malloc(4); // malloc returns aligned memory, so HEAPU32 can read it
    try {
      m.stringToUTF8(selector, text, length);
      if (m._facetry_renderer_create_webgl(text, 1, out) !== 0) {
        throw new Error(`Renderer: ${m.UTF8ToString(m._facetry_renderer_last_error(0))}`);
      }
      const viewport = new Viewport(m, m.HEAPU32[out >> 2] ?? 0, canvas);
      viewport.#attach();
      return viewport;
    } finally {
      m._free(text);
      m._free(out);
    }
  }

  /** Uploads a mesh from the core. The camera keeps its position; call fit() to frame it. */
  setMesh(mesh: MeshData): void {
    const m = this.#m;
    const buffers = [
      mesh.positions,
      mesh.normals,
      mesh.triangles,
      mesh.triangleFaceIds,
      mesh.edgePoints,
      mesh.edgeOffsets,
      mesh.edgeIds,
      mesh.vertexPositions,
      mesh.vertexIds,
    ];
    const pointers = buffers.map((buffer) => {
      const pointer = m._malloc(Math.max(4, buffer.byteLength));
      m.HEAPU8.set(new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength), pointer);
      return pointer;
    });
    // facetry_render_mesh: 14 four-byte fields on wasm32 (pointers and uint32 counts).
    const [pos, nrm, tri, faces, edgePts, edgeOffs, edgeIds, vtxPos, vtxIds] = pointers as [
      number,
      number,
      number,
      number,
      number,
      number,
      number,
      number,
      number,
    ];
    const fields = [
      pos,
      mesh.positions.length,
      nrm,
      tri,
      mesh.triangles.length,
      faces,
      edgePts,
      mesh.edgePoints.length,
      edgeOffs,
      mesh.edgeOffsets.length,
      edgeIds,
      vtxPos,
      mesh.vertexPositions.length,
      vtxIds,
    ];
    const struct = m._malloc(fields.length * 4);
    m.HEAPU32.set(fields, struct >> 2);
    try {
      this.#check(m._facetry_renderer_set_mesh(this.#renderer, struct));
    } finally {
      m._free(struct);
      pointers.forEach((pointer) => m._free(pointer));
    }
    this.#hover = { kind: PickKind.None, id: 0 };
    this.#selection = { kind: PickKind.None, id: 0 };
    m._facetry_renderer_set_hover(this.#renderer, 0, 0);
    m._facetry_renderer_set_selection(this.#renderer, 0, 0);
  }

  /** The current selection (one element; multi-selection comes with M1.2). */
  get selection(): Pick {
    return this.#selection;
  }

  /** Sets the selection and statistics callbacks. */
  setListeners(listeners: {
    onSelectionChange?: (selection: Pick) => void;
    onStats?: (stats: FrameStats) => void;
  }): void {
    if (listeners.onSelectionChange) this.onSelectionChange = listeners.onSelectionChange;
    if (listeners.onStats) this.onStats = listeners.onStats;
  }

  /** Clears the selection (Esc). */
  clearSelection(): void {
    this.#setSelection({ kind: PickKind.None, id: 0 });
  }

  fit(): void {
    this.#m._facetry_camera_fit(this.#renderer);
  }

  setOrthographic(orthographic: boolean): void {
    this.#m._facetry_camera_set_orthographic(this.#renderer, orthographic ? 1 : 0);
  }

  pick(x: number, y: number, radius: number): Pick {
    this.#check(this.#m._facetry_renderer_pick(this.#renderer, x, y, radius, this.#scratch));
    const words = this.#m.HEAPU32;
    return {
      kind: (words[this.#scratch >> 2] ?? 0) as PickKind,
      id: words[(this.#scratch >> 2) + 1] ?? 0,
    };
  }

  /**
   * Draws `frames` frames back to back and waits for the GPU to finish them. Returns the average
   * time per frame in milliseconds. For benchmarks (docs/benchmarks.md); independent of the
   * display's refresh rate and of requestAnimationFrame throttling.
   */
  benchmark(frames: number): number {
    const gl = this.#canvas.getContext("webgl2");
    const pixel = new Uint8Array(4);
    const started = performance.now();
    for (let i = 0; i < frames; i++) {
      this.#check(this.#m._facetry_renderer_draw(this.#renderer, 0));
    }
    gl?.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel); // waits for the GPU
    return (performance.now() - started) / frames;
  }

  dispose(): void {
    this.#disposed = true;
    this.#m._facetry_renderer_destroy(this.#renderer);
    this.#m._free(this.#scratch);
  }

  #attach(): void {
    const canvas = this.#canvas;
    const resize = () => {
      const ratio = window.devicePixelRatio || 1;
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;
      canvas.width = Math.max(1, Math.round(width * ratio));
      canvas.height = Math.max(1, Math.round(height * ratio));
      this.#m._facetry_renderer_set_viewport(this.#renderer, width, height, ratio);
    };
    new ResizeObserver(resize).observe(canvas);
    resize();

    canvas.style.touchAction = "none";
    canvas.addEventListener("pointerdown", (e) => this.#pointerDown(e));
    canvas.addEventListener("pointermove", (e) => this.#pointerMove(e));
    canvas.addEventListener("pointerup", (e) => this.#pointerUp(e));
    canvas.addEventListener("pointercancel", (e) => this.#pointerUp(e, true));
    canvas.addEventListener("pointerleave", () => this.#setHover({ kind: PickKind.None, id: 0 }));
    canvas.addEventListener("wheel", (e) => this.#wheel(e), { passive: false });
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());

    const frame = (time: number) => {
      if (this.#disposed) return;
      this.#recordFrame(time);
      this.#check(this.#m._facetry_renderer_draw(this.#renderer, 0));
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
  }

  #local(e: PointerEvent | WheelEvent): { x: number; y: number } {
    const rect = this.#canvas.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  }

  #pointerDown(e: PointerEvent): void {
    this.#canvas.setPointerCapture(e.pointerId);
    const { x, y } = this.#local(e);
    this.#pointers.set(e.pointerId, { x, y, startX: x, startY: y });
    if (e.pointerType === "touch") {
      this.#gesture = "touch";
      if (this.#pointers.size === 1) this.#m._facetry_camera_orbit_begin(this.#renderer, x, y);
      return;
    }
    // Mouse and pen: left drag orbits; right/middle drag or Shift + left drag pans.
    const pan = e.button === 1 || e.button === 2 || (e.button === 0 && e.shiftKey);
    this.#gesture = pan ? "pan" : "orbit";
    if (!pan) this.#m._facetry_camera_orbit_begin(this.#renderer, x, y);
  }

  #pointerMove(e: PointerEvent): void {
    const { x, y } = this.#local(e);
    const pointer = this.#pointers.get(e.pointerId);
    if (!pointer) {
      // Hover: mouse, or a pen hovering above the screen.
      const radius = PICK_RADIUS[e.pointerType] ?? 6;
      this.#setHover(this.pick(x, y, radius));
      return;
    }
    const dx = x - pointer.x;
    const dy = y - pointer.y;
    if (this.#gesture === "touch" && this.#pointers.size >= 2) {
      this.#touchPanZoom(e.pointerId, x, y);
      return;
    }
    pointer.x = x;
    pointer.y = y;
    if (this.#gesture === "orbit" || this.#gesture === "touch") {
      this.#m._facetry_camera_orbit(this.#renderer, dx, dy);
    } else if (this.#gesture === "pan") {
      this.#m._facetry_camera_pan(this.#renderer, dx, dy);
    }
  }

  // Two fingers: the midpoint pans, the change in distance zooms around the midpoint.
  #touchPanZoom(id: number, x: number, y: number): void {
    const [a, b] = [...this.#pointers.values()];
    if (!a || !b) return;
    const before = {
      cx: (a.x + b.x) / 2,
      cy: (a.y + b.y) / 2,
      d: Math.hypot(a.x - b.x, a.y - b.y),
    };
    const moved = this.#pointers.get(id);
    if (moved) {
      moved.x = x;
      moved.y = y;
    }
    const after = { cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2, d: Math.hypot(a.x - b.x, a.y - b.y) };
    this.#m._facetry_camera_pan(this.#renderer, after.cx - before.cx, after.cy - before.cy);
    if (before.d > 1 && after.d > 1) {
      this.#m._facetry_camera_zoom(this.#renderer, before.d / after.d, after.cx, after.cy);
    }
  }

  #pointerUp(e: PointerEvent, cancelled = false): void {
    const pointer = this.#pointers.get(e.pointerId);
    this.#pointers.delete(e.pointerId);
    if (this.#pointers.size === 0) this.#gesture = "none";
    if (!pointer || cancelled) return;
    const moved = Math.hypot(pointer.x - pointer.startX, pointer.y - pointer.startY);
    if (moved <= CLICK_SLOP && e.button === 0) {
      const radius = PICK_RADIUS[e.pointerType] ?? 6;
      this.#setSelection(this.pick(pointer.startX, pointer.startY, radius));
    }
    if (this.#gesture === "touch" && this.#pointers.size === 1) {
      const [remaining] = [...this.#pointers.values()];
      if (remaining) this.#m._facetry_camera_orbit_begin(this.#renderer, remaining.x, remaining.y);
    }
  }

  #wheel(e: WheelEvent): void {
    e.preventDefault();
    const { x, y } = this.#local(e);
    const toPixels = e.deltaMode === WheelEvent.DOM_DELTA_LINE ? 16 : 1;
    if (e.shiftKey) {
      // Shift + wheel or two-finger trackpad scroll pans (docs/INPUT.md). Some browsers report
      // Shift + vertical wheel as horizontal scrolling.
      this.#m._facetry_camera_pan(this.#renderer, -e.deltaX * toPixels, -e.deltaY * toPixels);
      return;
    }
    // Wheel zooms toward the pointer; trackpad pinch arrives as wheel events with ctrlKey.
    const speed = e.ctrlKey ? 0.01 : 0.0015;
    this.#m._facetry_camera_zoom(this.#renderer, Math.exp(e.deltaY * toPixels * speed), x, y);
  }

  #setHover(pick: Pick): void {
    if (pick.kind === this.#hover.kind && pick.id === this.#hover.id) return;
    this.#hover = pick;
    this.#m._facetry_renderer_set_hover(this.#renderer, pick.kind, pick.id);
    this.#canvas.style.cursor = pick.kind === PickKind.None ? "default" : "pointer";
  }

  #setSelection(pick: Pick): void {
    this.#selection = pick;
    this.#m._facetry_renderer_set_selection(this.#renderer, pick.kind, pick.id);
    this.onSelectionChange(pick);
  }

  #recordFrame(time: number): void {
    if (this.#lastFrame > 0) this.#frameTimes.push(time - this.#lastFrame);
    this.#lastFrame = time;
    const total = this.#frameTimes.reduce((sum, t) => sum + t, 0);
    if (total < 1000) return;
    const m = this.#m;
    m._facetry_renderer_stats(this.#renderer, this.#scratch);
    const floats = m.HEAPF32;
    const words = m.HEAPU32;
    const base = this.#scratch >> 2;
    this.onStats({
      frameMilliseconds: total / this.#frameTimes.length,
      cpuMilliseconds: floats[base] ?? 0,
      triangles: words[base + 1] ?? 0,
      edgeSegments: words[base + 2] ?? 0,
    });
    this.#frameTimes = [];
  }

  #check(status: number): void {
    if (status !== 0) {
      throw new Error(
        `Renderer: ${this.#m.UTF8ToString(this.#m._facetry_renderer_last_error(this.#renderer))}`,
      );
    }
  }
}
