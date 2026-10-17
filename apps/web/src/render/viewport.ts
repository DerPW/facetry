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

/** Faces of the ViewCube, in the order of ViewCubeCell.face (facetry_cube_face). */
export const CUBE_FACES = ["Right", "Left", "Back", "Front", "Top", "Bottom"] as const;

/** One cell of the ViewCube (facetry_viewcube_cell); see ViewCube.tsx. */
export interface ViewCubeCell {
  /** Four corners [x, y] in the cube's square, in CSS pixels. */
  points: [number, number][];
  /** Where a click on the cell looks from. */
  direction: [number, number, number];
  face: number;
  /** 0 center, 1 edge, 2 corner. */
  region: number;
}

/** Directions of the named views, from the model toward the eye. */
export const NAMED_VIEWS = {
  Top: [0, 0, 1],
  Bottom: [0, 0, -1],
  Front: [0, -1, 0],
  Back: [0, 1, 0],
  Left: [-1, 0, 0],
  Right: [1, 0, 0],
} as const satisfies Record<string, readonly [number, number, number]>;
export type NamedView = keyof typeof NAMED_VIEWS;

const CELL_BYTES = 56; // facetry_viewcube_cell: 12 floats and 2 uint32s
const MAX_CELLS = 27;

export interface FrameStats {
  /** Average time between frames over the last second, in milliseconds. */
  frameMilliseconds: number;
  /** CPU time of the last draw call in the renderer, in milliseconds. */
  cpuMilliseconds: number;
  triangles: number;
  edgeSegments: number;
}

/** Mouse and trackpad presets (docs/INPUT.md); the mapping is shared C++ (facetry_render.h). */
export const NavigationPreset = { Facetry: 0, Fusion: 1, Trackpad: 2 } as const;
export type NavigationPreset = (typeof NavigationPreset)[keyof typeof NavigationPreset];

export interface NavigationSettings {
  preset: NavigationPreset;
  /** What one finger does; two fingers do the other. */
  oneFinger: "orbit" | "pan";
}

export const DEFAULT_NAVIGATION: NavigationSettings = {
  preset: NavigationPreset.Facetry,
  oneFinger: "orbit",
};

/** facetry_navigation_action */
const Action = { None: 0, Orbit: 1, Pan: 2, Zoom: 3, Select: 4 } as const;

/** Pick radius in CSS pixels per input type: fingers need more room than a pen or mouse. */
const PICK_RADIUS: Record<string, number> = { mouse: 6, pen: 8, touch: 18 };
/** Pointer movement (CSS pixels) below which a press counts as a click/tap. */
const CLICK_SLOP = 4;
/** A two-finger tap lasts at most this long; two of them within the second limit fit the view. */
const TAP_MILLISECONDS = 300;
const DOUBLE_TAP_MILLISECONDS = 400;
/** Wheel events further apart than this start a new orbit around the point under the pointer. */
const SCROLL_GAP_MILLISECONDS = 200;

/** facetry_modifier mask of an event. */
function modifiers(e: MouseEvent): number {
  return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0);
}

export class Viewport {
  readonly #m: FacetryRenderModule;
  readonly #renderer: number;
  readonly #canvas: HTMLCanvasElement;
  readonly #scratch: number;
  readonly #cells: number;
  readonly #pointers = new Map<number, { x: number; y: number; startX: number; startY: number }>();
  #gesture: "none" | "orbit" | "pan" | "select" | "touch" = "none";
  // The current touch sequence, from the first finger down to the last finger up.
  #touch = { start: 0, fingers: 0, moved: false };
  #lastTwoFingerTap = 0;
  #lastWheel = 0;
  #hover: Pick = { kind: PickKind.None, id: 0 };
  #selection: Pick = { kind: PickKind.None, id: 0 };
  #frameTimes: number[] = [];
  #lastFrame = 0;
  #disposed = false;

  #navigation: NavigationSettings = DEFAULT_NAVIGATION;

  /** Called whenever the selection changes. */
  onSelectionChange: (selection: Pick) => void = () => {};
  /** Called about once per second with frame statistics. */
  onStats: (stats: FrameStats) => void = () => {};

  private constructor(m: FacetryRenderModule, renderer: number, canvas: HTMLCanvasElement) {
    this.#m = m;
    this.#renderer = renderer;
    this.#canvas = canvas;
    this.#scratch = m._malloc(64);
    this.#cells = m._malloc(CELL_BYTES * MAX_CELLS);
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

  /** Frames the whole model from the current direction (F). */
  fit(): void {
    this.#m._facetry_camera_fit(this.#renderer);
  }

  /** Mouse, trackpad and touch settings; the app stores them per device. */
  setNavigation(navigation: NavigationSettings): void {
    this.#navigation = navigation;
  }

  /** Frames the selection, or the whole model (Shift+F). */
  fitSelection(): void {
    this.#m._facetry_camera_fit_selection(this.#renderer);
  }

  /** The home view: the whole model from the front right, above. */
  home(animate = true): void {
    this.#m._facetry_camera_home(this.#renderer, animate ? 1 : 0);
  }

  /** Turns the view to look from `direction` (from the model toward the eye). */
  viewFrom(direction: readonly [number, number, number], animate = true): void {
    const [x, y, z] = direction;
    this.#m._facetry_camera_view_from(this.#renderer, x, y, z, animate ? 1 : 0);
  }

  /** The visible ViewCube cells for a cube in a square of `size` CSS pixels, far to near. */
  viewCubeCells(size: number): ViewCubeCell[] {
    const m = this.#m;
    const count = Math.min(
      m._facetry_viewcube_cells(this.#renderer, size, this.#cells, MAX_CELLS),
      MAX_CELLS,
    );
    const floats = m.HEAPF32;
    const words = m.HEAPU32;
    const cells: ViewCubeCell[] = [];
    for (let i = 0; i < count; i++) {
      const base = (this.#cells + i * CELL_BYTES) >> 2;
      const f = (offset: number) => floats[base + offset] ?? 0;
      cells.push({
        points: [0, 1, 2, 3].map((k) => [f(k * 2), f(k * 2 + 1)] as [number, number]),
        direction: [f(8), f(9), f(10)],
        face: words[base + 12] ?? 0,
        region: words[base + 13] ?? 0,
      });
    }
    return cells;
  }

  /** Index in viewCubeCells(size) of the cell under (x, y), or -1. */
  viewCubeHit(size: number, x: number, y: number): number {
    return this.#m._facetry_viewcube_hit(this.#renderer, size, x, y);
  }

  /** Orbits around the point in the middle of the view (dragging the ViewCube). */
  orbitFromCenter(dx: number, dy: number, begin: boolean): void {
    if (begin) {
      const rect = this.#canvas.getBoundingClientRect();
      this.#m._facetry_camera_orbit_begin(this.#renderer, rect.width / 2, rect.height / 2);
    }
    this.#m._facetry_camera_orbit(this.#renderer, dx, dy);
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
    this.#m._free(this.#cells);
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
    // A middle press would start the browser's autoscroll on Windows.
    canvas.addEventListener("mousedown", (e) => {
      if (e.button === 1) e.preventDefault();
    });

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
      if (this.#pointers.size === 1) this.#touch = { start: e.timeStamp, fingers: 0, moved: false };
      this.#touch.fingers = Math.max(this.#touch.fingers, this.#pointers.size);
      this.#gesture = "touch";
      this.#beginTouchOrbit();
      return;
    }
    // A pen drag orbits until drawing tools exist (M1.3); mouse buttons follow the preset.
    const action =
      e.pointerType === "pen"
        ? Action.Orbit
        : this.#m._facetry_navigation_drag(this.#navigation.preset, e.button, modifiers(e));
    this.#gesture =
      action === Action.Orbit
        ? "orbit"
        : action === Action.Pan
          ? "pan"
          : action === Action.Select
            ? "select"
            : "none";
    if (action === Action.Orbit) this.#m._facetry_camera_orbit_begin(this.#renderer, x, y);
  }

  // Orbits start at the point under one finger, or under the midpoint of two.
  #beginTouchOrbit(): void {
    const points = [...this.#pointers.values()];
    const orbitFingers = this.#navigation.oneFinger === "orbit" ? 1 : 2;
    if (points.length !== orbitFingers) return;
    const x = points.reduce((sum, p) => sum + p.x, 0) / points.length;
    const y = points.reduce((sum, p) => sum + p.y, 0) / points.length;
    this.#m._facetry_camera_orbit_begin(this.#renderer, x, y);
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
    if (Math.hypot(x - pointer.startX, y - pointer.startY) > CLICK_SLOP) this.#touch.moved = true;
    const orbit =
      this.#gesture === "orbit" ||
      (this.#gesture === "touch" && this.#navigation.oneFinger === "orbit");
    const pan =
      this.#gesture === "pan" ||
      (this.#gesture === "touch" && this.#navigation.oneFinger === "pan");
    if (orbit) {
      this.#m._facetry_camera_orbit(this.#renderer, dx, dy);
    } else if (pan) {
      this.#m._facetry_camera_pan(this.#renderer, dx, dy);
    }
  }

  // Two fingers: the midpoint pans (or orbits, when one finger pans), the change in distance
  // zooms around the midpoint.
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
      if (Math.hypot(x - moved.startX, y - moved.startY) > CLICK_SLOP) this.#touch.moved = true;
    }
    const after = { cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2, d: Math.hypot(a.x - b.x, a.y - b.y) };
    const dx = after.cx - before.cx;
    const dy = after.cy - before.cy;
    if (this.#navigation.oneFinger === "pan") {
      this.#m._facetry_camera_orbit(this.#renderer, dx, dy);
    } else {
      this.#m._facetry_camera_pan(this.#renderer, dx, dy);
    }
    if (before.d > 1 && after.d > 1) {
      this.#m._facetry_camera_zoom(this.#renderer, before.d / after.d, after.cx, after.cy);
    }
  }

  #pointerUp(e: PointerEvent, cancelled = false): void {
    const pointer = this.#pointers.get(e.pointerId);
    this.#pointers.delete(e.pointerId);
    const touch = e.pointerType === "touch";
    const lastFinger = touch && this.#pointers.size === 0;
    if (this.#pointers.size === 0) this.#gesture = "none";
    if (!pointer || cancelled) return;
    if (lastFinger && this.#touchEnded(e.timeStamp)) return;
    const moved = Math.hypot(pointer.x - pointer.startX, pointer.y - pointer.startY);
    // A tap selects only when one finger touched; lifting two fingers is not a tap.
    const tap = touch ? lastFinger && this.#touch.fingers === 1 : moved <= CLICK_SLOP;
    if (tap && moved <= CLICK_SLOP && e.button === 0) {
      const radius = PICK_RADIUS[e.pointerType] ?? 6;
      this.#setSelection(this.pick(pointer.startX, pointer.startY, radius));
    }
    if (this.#gesture === "touch") this.#beginTouchOrbit();
  }

  // The last finger went up. Two two-finger taps in a row fit the view. True if it did.
  #touchEnded(time: number): boolean {
    const { start, fingers, moved } = this.#touch;
    if (fingers !== 2 || moved || time - start > TAP_MILLISECONDS) return false;
    if (time - this.#lastTwoFingerTap <= DOUBLE_TAP_MILLISECONDS) {
      this.#lastTwoFingerTap = 0;
      this.fit();
    } else {
      this.#lastTwoFingerTap = time;
    }
    return true;
  }

  #wheel(e: WheelEvent): void {
    e.preventDefault();
    const { x, y } = this.#local(e);
    const toPixels = e.deltaMode === WheelEvent.DOM_DELTA_LINE ? 16 : 1;
    const dx = e.deltaX * toPixels;
    const dy = e.deltaY * toPixels;
    const gap = e.timeStamp - this.#lastWheel;
    this.#lastWheel = e.timeStamp;
    // Trackpad pinch arrives as wheel events with ctrlKey and always zooms. Browsers cannot tell
    // a trackpad from a mouse wheel, so the preset decides (docs/INPUT.md).
    const action = e.ctrlKey
      ? Action.Zoom
      : this.#m._facetry_navigation_scroll(this.#navigation.preset, modifiers(e), 0);
    if (action === Action.Pan) {
      // Some browsers report Shift + vertical wheel as horizontal scrolling; both pan.
      this.#m._facetry_camera_pan(this.#renderer, -dx, -dy);
    } else if (action === Action.Orbit) {
      if (gap > SCROLL_GAP_MILLISECONDS) this.#m._facetry_camera_orbit_begin(this.#renderer, x, y);
      this.#m._facetry_camera_orbit(this.#renderer, dx, dy);
    } else if (action === Action.Zoom) {
      const speed = e.ctrlKey ? 0.01 : 0.0015;
      this.#m._facetry_camera_zoom(this.#renderer, Math.exp(dy * speed), x, y);
    }
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
