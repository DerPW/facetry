import { useCallback, useEffect, useRef, useState } from "react";

// The M0.6 example document: what the app shows until a file is opened.
import exampleDocument from "../../../tests/documents/corner-fillet.json?raw";
import { BrowserPanel } from "./components/BrowserPanel";
import { ModelPanel } from "./components/ModelPanel";
import { RecoveryPanel } from "./components/RecoveryPanel";
import { Timeline, type Repair } from "./components/Timeline";
import { ViewCube } from "./components/ViewCube";
import { ViewportCanvas } from "./components/ViewportCanvas";
import {
  BooleanOp,
  ElementKind,
  MILLIMETERS_PER,
  type Edit,
  type MeshData,
  type ModelSnapshot,
  type Reference,
  type SessionState,
} from "./core/api";
import { recoverable, tabLock, type AutosaveEntry } from "./core/autosave";
import { CoreClient } from "./core/client";
import { postBenchmark } from "./dev/bench";
import {
  DEFAULT_NAVIGATION,
  NAMED_VIEWS,
  NavigationPreset,
  PickFilter,
  PickKind,
  VisualStyle,
  type FrameStats,
  type NavigationSettings,
  type Pick,
  type Viewport,
} from "./render/viewport";

declare const __APP_VERSION__: string;

const EMPTY_MESH: MeshData = {
  positions: new Float32Array(),
  normals: new Float32Array(),
  triangles: new Uint32Array(),
  triangleFaceIds: new Uint32Array(),
  edgePoints: new Float32Array(),
  edgeOffsets: new Uint32Array([0]),
  edgeIds: new Uint32Array(),
  vertexPositions: new Float32Array(),
  vertexIds: new Uint32Array(),
};
// Singular and plural names per kind, in the order the summary lists them.
const kindNames: [PickKind, string, string][] = [
  [PickKind.Vertex, "Vertex", "vertices"],
  [PickKind.Edge, "Edge", "edges"],
  [PickKind.Face, "Face", "faces"],
  [PickKind.Body, "Body", "bodies"],
];

function describe(selection: Pick[]): string {
  const [only] = selection;
  if (!only) return "Nothing selected";
  if (selection.length === 1) {
    const name = kindNames.find(([kind]) => kind === only.kind)?.[1] ?? "Element";
    return only.kind === PickKind.Body ? "Body selected" : `${name} ${only.id} selected`;
  }
  const parts = kindNames.flatMap(([kind, singular, plural]) => {
    const count = selection.filter((pick) => pick.kind === kind).length;
    if (count === 0) return [];
    return [`${count} ${count === 1 ? singular.toLowerCase() : plural}`];
  });
  return `${parts.join(", ")} selected`;
}

// Keys 1–5 choose what clicks select, like Plasticity's selection modes.
const filterKeys: Record<string, PickFilter> = {
  Digit1: PickFilter.Vertices,
  Digit2: PickFilter.Edges,
  Digit3: PickFilter.Faces,
  Digit4: PickFilter.Bodies,
  Digit5: PickFilter.Everything,
};

function formatStats(stats: FrameStats): string {
  const fps = 1000 / stats.frameMilliseconds;
  return (
    `${stats.frameMilliseconds.toFixed(1)} ms/frame (${fps.toFixed(0)} fps) · ` +
    `draw ${stats.cpuMilliseconds.toFixed(2)} ms · ${stats.triangles.toLocaleString("en")} triangles`
  );
}

const params = new URLSearchParams(location.search);
// ?stress shows the M0.4 demo part, finely tessellated to about 215k triangles, instead of the
// document, so the numbers in docs/benchmarks.md stay comparable.
const stress = params.has("stress");
const tessellation = { linearDeflection: 0.01, angularDeflection: 0.2 };

// This tab's autosave (#17). The tab holds its Web Lock while it is open, so that other tabs
// only offer to recover autosaves whose tab is gone. Both need a secure context.
const tabId = typeof crypto.randomUUID === "function" ? crypto.randomUUID() : `${Date.now()}`;
if ("locks" in navigator) {
  void navigator.locks.request(tabLock(tabId), () => new Promise<never>(() => {}));
}
// Navigation settings belong to the device, not the document.
const navigationKey = "facetry.navigation";

function loadNavigation(): NavigationSettings {
  try {
    const saved: unknown = JSON.parse(localStorage.getItem(navigationKey) ?? "null");
    if (typeof saved === "object" && saved !== null) {
      const { preset, oneFinger } = saved as Partial<NavigationSettings>;
      return {
        preset: Object.values(NavigationPreset).includes(preset as NavigationPreset)
          ? (preset as NavigationPreset)
          : DEFAULT_NAVIGATION.preset,
        oneFinger: oneFinger === "pan" ? "pan" : "orbit",
      };
    }
  } catch {
    // No storage here (private window) or a damaged value: the defaults.
  }
  return DEFAULT_NAVIGATION;
}

const hasTouch = navigator.maxTouchPoints > 0;

const gridKey = "facetry.grid";
const styleKey = "facetry.style";

function loadStyle(): VisualStyle {
  try {
    const saved = Number(localStorage.getItem(styleKey));
    if (Object.values(VisualStyle).includes(saved as VisualStyle)) return saved as VisualStyle;
  } catch {
    // The default below.
  }
  return VisualStyle.ShadedEdges;
}

function loadGrid(): boolean {
  try {
    return localStorage.getItem(gridKey) !== "hidden";
  } catch {
    return true;
  }
}

// An autosave follows the first unsaved change by at most this long.
const autosaveDelay = 1000;

async function heldLocks(): Promise<Set<string>> {
  if (!("locks" in navigator)) return new Set();
  const { held = [] } = await navigator.locks.query();
  return new Set(held.flatMap((lock) => (lock.name ? [lock.name] : [])));
}

/** The M0.4 demo part: a plate with rounded edges, a boss on top and a hole through both. */
async function stressMesh(core: CoreClient): Promise<MeshData> {
  const plate = await core.call("makeBox", 40, 30, 4);
  const rounded = await core.call("fillet", plate, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], 1.5);
  const boss = await core.call("makeCylinder", 20, 15, 6, 12);
  const joined = await core.call("boolean", BooleanOp.Fuse, rounded, boss);
  const hole = await core.call("makeCylinder", 20, 15, 3, 12);
  const part = await core.call("boolean", BooleanOp.Cut, joined, hole);
  return core.call("tessellate", part, { linearDeflection: 0.0004, angularDeflection: 0.02 });
}

/** The open model in the core worker. */
interface Session {
  core: CoreClient;
  handle: number;
}

/** Tech preview: the viewport with the example model, its parameters, status and statistics. */
export function App() {
  const [viewport, setViewport] = useState<Viewport | null>(null);
  const [status, setStatus] = useState("Starting the core…");
  const [ready, setReady] = useState<boolean | undefined>(undefined);
  const [stats, setStats] = useState<FrameStats | null>(null);
  const [selection, setSelection] = useState<Pick[]>([]);
  const [pickFilter, setPickFilter] = useState<PickFilter>(PickFilter.Everything);
  const [multiSelect, setMultiSelect] = useState(false);
  const [boxMode, setBoxMode] = useState(false);
  const [orthographic, setOrthographic] = useState(false);
  const [model, setModel] = useState<SessionState | null>(null);
  const [editError, setEditError] = useState<string | null>(null);
  const [fileName, setFileName] = useState("Untitled.facetry");
  const [repair, setRepair] = useState<Repair | null>(null);
  const [selectedFeature, setSelectedFeature] = useState<string | null>(null);
  // Shown while a regeneration takes longer than a step; the last model stays on screen.
  const [progress, setProgress] = useState<{ done: number; total: number } | null>(null);
  const [recovery, setRecovery] = useState<AutosaveEntry[]>([]);
  const [navigation, setNavigation] = useState(loadNavigation);
  const [grid, setGrid] = useState(loadGrid);
  const [style, setStyle] = useState(loadStyle);
  const fileInput = useRef<HTMLInputElement>(null);
  const session = useRef<Session | null>(null);

  const onError = useCallback((message: string) => {
    setStatus(`Start-up failed: ${message}`);
    setReady(false);
  }, []);

  const show = useCallback(
    (snapshot: ModelSnapshot) => {
      // The first visible body. Showing several bodies at once comes with the first features
      // that make more than one (M1.5).
      const body = snapshot.state.bodies.find((b) => b.visible);
      const mesh = body ? snapshot.meshes[body.id] : undefined;
      if (viewport) {
        // Element IDs change with the geometry, so an old selection would point elsewhere.
        viewport.clearSelection();
        viewport.setMesh(mesh ?? EMPTY_MESH);
      }
      setModel(snapshot.state);
      setProgress(null);
      return mesh;
    },
    [viewport],
  );

  // Open the example document in the core worker and show it.
  useEffect(() => {
    if (!viewport) return;
    viewport.setListeners({ onStats: setStats });
    let cancelled = false;
    const core = new CoreClient();
    core.onProgress = (done, total) => setProgress({ done, total });
    void (async () => {
      try {
        const versions = await core.call("versions");
        let mesh: MeshData | undefined;
        let handle: number | undefined;
        if (stress) {
          mesh = await stressMesh(core);
          if (cancelled) return;
          viewport.setMesh(mesh);
        } else {
          handle = await core.call("openSession", exampleDocument);
          const snapshot = await core.call("regenerateModel", handle, tessellation);
          if (cancelled || !snapshot) return;
          session.current = { core, handle };
          mesh = show(snapshot);
        }
        viewport.home(false);
        setStatus(`Core ${versions.core} · OpenCASCADE ${versions.kernel} · ${versions.platform}`);
        setReady(true);
        if (handle !== undefined) {
          const entries = recoverable(await core.call("listAutosaves"), await heldLocks());
          if (!cancelled) setRecovery(entries);
        }
        if (import.meta.env.DEV) {
          // Handle for manual testing and benchmarks in the browser console.
          Object.assign(window, { facetry: { viewport, core, session: handle } });
          const canvas = document.querySelector<HTMLCanvasElement>("#viewport");
          if (params.has("bench") && canvas && mesh) {
            setTimeout(() => void postBenchmark(viewport, canvas, mesh.triangles.length / 3), 1000);
          }
        }
      } catch (error) {
        if (!cancelled) onError(error instanceof Error ? error.message : String(error));
      }
    })();
    return () => {
      cancelled = true;
      session.current = null;
      core.terminate();
    };
  }, [viewport, onError, show]);

  // Autosave (#17): about a second after a change, of the document as it is by then.
  const fileNameRef = useRef(fileName);
  const autosaveTimer = useRef<number | null>(null);

  const autosaveNow = useCallback(() => {
    if (autosaveTimer.current !== null) window.clearTimeout(autosaveTimer.current);
    autosaveTimer.current = null;
    const current = session.current;
    if (!current) return;
    current.core
      .call("autosave", current.handle, { id: tabId, name: fileNameRef.current })
      .catch((error: unknown) => console.warn("Autosave failed:", error));
  }, []);

  const scheduleAutosave = useCallback(() => {
    autosaveTimer.current ??= window.setTimeout(autosaveNow, autosaveDelay);
  }, [autosaveNow]);

  // The document was saved, or replaced by a saved one: there is nothing to recover.
  const markSaved = useCallback(() => {
    if (autosaveTimer.current !== null) window.clearTimeout(autosaveTimer.current);
    autosaveTimer.current = null;
    void session.current?.core.call("removeAutosave", tabId);
  }, []);

  // A hidden tab can be closed or discarded without notice, so its autosave happens now.
  useEffect(() => {
    const onVisibilityChange = () => {
      if (document.visibilityState === "hidden" && autosaveTimer.current !== null) autosaveNow();
    };
    document.addEventListener("visibilitychange", onVisibilityChange);
    return () => document.removeEventListener("visibilitychange", onVisibilityChange);
  }, [autosaveNow]);

  // Runs a change in the core worker, then regenerates and shows the result. Calls queue up in
  // the worker and run in order; a newer change's regeneration takes over from a running one,
  // which then resolves with null.
  const change = useCallback(
    async (run: (current: Session) => Promise<unknown>) => {
      const current = session.current;
      if (!current) return;
      try {
        await run(current);
        scheduleAutosave();
        setEditError(null);
        const snapshot = await current.core.call("regenerateModel", current.handle, tessellation);
        if (snapshot) show(snapshot);
      } catch (error) {
        setProgress(null);
        setEditError(error instanceof Error ? error.message : String(error));
      }
    },
    [show, scheduleAutosave],
  );

  const edit = useCallback(
    (edits: Edit | Edit[]) =>
      void change(({ core, handle }) => core.call("applyEdits", handle, edits)),
    [change],
  );

  // Repairing a broken reference: the next edge the user selects replaces it (ADR-0006). Refs, so
  // that the viewport's selection callback sees the current values.
  const repairRef = useRef<Repair | null>(null);
  const modelRef = useRef<SessionState | null>(null);
  useEffect(() => {
    repairRef.current = repair;
    modelRef.current = model;
  }, [repair, model]);

  const onPick = useCallback(
    (list: Pick[], pick: Pick) => {
      setSelection(list);
      const pending = repairRef.current;
      const body = modelRef.current?.bodies.find((b) => b.visible)?.id;
      const feature = modelRef.current?.features.find((f) => f.id === pending?.feature);
      // The edge just added to the selection (not one just removed from it).
      const added = list.some((p) => p.kind === pick.kind && p.id === pick.id);
      if (!pending || !body || !feature || pick.kind !== PickKind.Edge || !added) return;
      repairRef.current = null;
      setRepair(null);
      void change(async ({ core, handle }) => {
        const reference: Reference = await core.call(
          "makeReference",
          handle,
          body,
          ElementKind.Edge,
          pick.id,
        );
        const edges = [...((feature.definition.edges as Reference[] | undefined) ?? [])];
        edges[pending.index] = reference;
        await core.call("applyEdits", handle, {
          op: "replaceFeature",
          feature: { ...feature.definition, edges },
        });
      });
    },
    [change],
  );

  useEffect(() => {
    viewport?.setListeners({ onSelectionChange: onPick });
  }, [viewport, onPick]);

  // Replaces the open document, with a file or a recovered autosave. Saving and opening with the
  // File System Access API come with M1.9.
  const replaceDocument = useCallback(
    async (open: (core: CoreClient) => Promise<number>, name: string) => {
      const current = session.current;
      if (!current || !viewport) return false;
      try {
        const handle = await open(current.core);
        await current.core.call("releaseSession", current.handle);
        current.handle = handle;
        fileNameRef.current = name;
        setFileName(name);
        setEditError(null);
        const snapshot = await current.core.call("regenerateModel", handle, tessellation);
        if (snapshot) {
          show(snapshot);
          viewport.home(false);
        }
        return true;
      } catch (error) {
        setEditError(error instanceof Error ? error.message : String(error));
        return false;
      }
    },
    [show, viewport],
  );

  const openFile = useCallback(
    async (file: File) => {
      const name = file.name.endsWith(".facetry") ? file.name : `${file.name}.facetry`;
      const opened = await replaceDocument(
        async (core) => core.call("openSessionFile", new Uint8Array(await file.arrayBuffer())),
        name,
      );
      if (opened) markSaved();
    },
    [replaceDocument, markSaved],
  );

  const recover = useCallback(
    async (entry: AutosaveEntry) => {
      const opened = await replaceDocument(
        (core) => core.call("openAutosave", entry.id),
        entry.name,
      );
      if (!opened) return;
      // It stays an autosave, now this tab's, until the user saves it.
      autosaveNow();
      void session.current?.core.call("removeAutosave", entry.id);
      setRecovery((entries) => entries.filter((e) => e.id !== entry.id));
    },
    [replaceDocument, autosaveNow],
  );

  const discard = useCallback((entry: AutosaveEntry) => {
    void session.current?.core.call("removeAutosave", entry.id);
    setRecovery((entries) => entries.filter((e) => e.id !== entry.id));
  }, []);

  // Saves by downloading the file.
  const saveFile = useCallback(async () => {
    const current = session.current;
    if (!current) return;
    try {
      const bytes = await current.core.call("saveSessionFile", current.handle);
      const url = URL.createObjectURL(
        new Blob([bytes as Uint8Array<ArrayBuffer>], { type: "application/octet-stream" }),
      );
      const link = Object.assign(document.createElement("a"), { href: url, download: fileName });
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      markSaved();
    } catch (error) {
      setEditError(error instanceof Error ? error.message : String(error));
    }
  }, [fileName, markSaved]);

  useEffect(() => {
    viewport?.setOrthographic(orthographic);
  }, [viewport, orthographic]);

  useEffect(() => {
    viewport?.setPickFilter(pickFilter);
    viewport?.setMultiSelect(multiSelect);
    viewport?.setBoxMode(boxMode);
  }, [viewport, pickFilter, multiSelect, boxMode]);

  // The shown body's opacity comes from the document; the style is a device setting.
  const opacity = model?.bodies.find((body) => body.visible)?.opacity ?? 1;
  useEffect(() => {
    viewport?.setStyle(style, opacity);
    try {
      localStorage.setItem(styleKey, String(style));
    } catch {
      // Not stored; the setting lasts until the page is closed.
    }
  }, [viewport, style, opacity]);

  // The grid's spacing follows the document's units.
  const units = model?.units ?? "mm";
  useEffect(() => {
    viewport?.setGrid(grid, MILLIMETERS_PER[units]);
    try {
      localStorage.setItem(gridKey, grid ? "shown" : "hidden");
    } catch {
      // Not stored; the setting lasts until the page is closed.
    }
  }, [viewport, grid, units]);

  useEffect(() => {
    viewport?.setNavigation(navigation);
    try {
      localStorage.setItem(navigationKey, JSON.stringify(navigation));
    } catch {
      // Not stored; the setting lasts until the page is closed.
    }
  }, [viewport, navigation]);

  // Keyboard shortcuts (docs/INPUT.md).
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.target instanceof HTMLInputElement) return;
      // Undo: Cmd/Ctrl+Z. Redo: Shift+Cmd/Ctrl+Z, or Ctrl+Y.
      const command = event.metaKey || event.ctrlKey;
      const key = event.key.toLowerCase();
      if (command && !event.altKey && (key === "z" || (key === "y" && event.ctrlKey))) {
        event.preventDefault();
        const redo = key === "y" || event.shiftKey;
        void change(({ core, handle }) => core.call(redo ? "redo" : "undo", handle));
        return;
      }
      // Named views on the numeric keypad, as in Blender: 1 front, 3 right, 7 top; with Ctrl
      // the opposite side.
      const numpad: Record<string, [keyof typeof NAMED_VIEWS, keyof typeof NAMED_VIEWS]> = {
        Numpad1: ["Front", "Back"],
        Numpad3: ["Right", "Left"],
        Numpad7: ["Top", "Bottom"],
      };
      const view = numpad[event.code];
      if (view && !event.metaKey && !event.altKey) {
        viewport?.viewFrom(NAMED_VIEWS[event.ctrlKey ? view[1] : view[0]]);
        return;
      }
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      const filter = filterKeys[event.code];
      if (filter !== undefined) setPickFilter(filter);
      else if (event.key === "Home") viewport?.home();
      else if (event.key === "F") viewport?.fitSelection();
      else if (event.key === "f") viewport?.fit();
      else if (event.key === "o") setOrthographic((value) => !value);
      else if (event.key === "g") setGrid((shown) => !shown);
      else if (event.key === "Escape") viewport?.clearSelection();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [viewport, change]);

  return (
    <>
      <ViewportCanvas onReady={setViewport} onError={onError} />
      <div
        id="app"
        data-core-ready={ready === undefined ? undefined : String(ready)}
        data-fps={stats ? (1000 / stats.frameMilliseconds).toFixed(1) : undefined}
      >
        <div className="sidebar">
          <header className="panel header">
            <h1>Facetry</h1>
            <p className="muted">
              Version {__APP_VERSION__} · tech preview{stress ? " · stress test" : ""}
            </p>
            {model && <p className="muted">{fileName}</p>}
            <p className="muted">{status}</p>
            {progress && (
              <p className="muted" role="status">
                Regenerating… {progress.done} of {progress.total} features
              </p>
            )}
            {editError && <p className="error">{editError}</p>}
          </header>
          {recovery.length > 0 && (
            <RecoveryPanel
              entries={recovery}
              onOpen={(entry) => void recover(entry)}
              onDiscard={discard}
            />
          )}
          {model && (
            <ModelPanel
              state={model}
              onSetParameter={(name, value) => edit({ op: "setParameter", name, value })}
              onSetUnits={(units) => edit({ op: "setUnits", units })}
            />
          )}
          {model && (
            <BrowserPanel
              state={model}
              onEdit={edit}
              onSelectFeature={setSelectedFeature}
              onSelectBody={() => viewport?.selectBody()}
            />
          )}
        </div>
        <div className="top-right">
          <div className="panel controls">
            <input
              ref={fileInput}
              type="file"
              accept=".facetry,.json"
              hidden
              onChange={(event) => {
                const file = event.currentTarget.files?.[0];
                event.currentTarget.value = "";
                if (file) void openFile(file);
              }}
            />
            <button type="button" disabled={!model} onClick={() => fileInput.current?.click()}>
              Open…
            </button>
            <button type="button" disabled={!model} onClick={() => void saveFile()}>
              Save
            </button>
            <button
              type="button"
              disabled={!model?.canUndo}
              title={model?.undoName ? `Undo ${model.undoName} (⌘Z / Ctrl+Z)` : "Undo"}
              onClick={() => void change(({ core, handle }) => core.call("undo", handle))}
            >
              Undo
            </button>
            <button
              type="button"
              disabled={!model?.canRedo}
              title={model?.redoName ? `Redo ${model.redoName} (⇧⌘Z / Ctrl+Y)` : "Redo"}
              onClick={() => void change(({ core, handle }) => core.call("redo", handle))}
            >
              Redo
            </button>
            <button type="button" title="Fit (F)" onClick={() => viewport?.fit()}>
              Fit
            </button>
            <button
              type="button"
              title="Toggle projection (O)"
              onClick={() => setOrthographic((v) => !v)}
            >
              {orthographic ? "Perspective" : "Orthographic"}
            </button>
            <select
              aria-label="Visual style"
              value={style}
              onChange={(event) => setStyle(Number(event.currentTarget.value) as VisualStyle)}
            >
              <option value={VisualStyle.Shaded}>Shaded</option>
              <option value={VisualStyle.ShadedEdges}>Shaded with edges</option>
              <option value={VisualStyle.ShadedHiddenEdges}>Hidden edges</option>
              <option value={VisualStyle.Wireframe}>Wireframe</option>
            </select>
            <select
              aria-label="Select"
              title="What clicks select (keys 1–5)"
              value={pickFilter}
              onChange={(event) => setPickFilter(Number(event.currentTarget.value) as PickFilter)}
            >
              <option value={PickFilter.Everything}>Select: all</option>
              <option value={PickFilter.Vertices}>Vertices</option>
              <option value={PickFilter.Edges}>Edges</option>
              <option value={PickFilter.Faces}>Faces</option>
              <option value={PickFilter.Bodies}>Bodies</option>
            </select>
            <button
              type="button"
              aria-pressed={multiSelect}
              title="Every click or tap adds to the selection (or Shift/Cmd/Ctrl + click)"
              onClick={() => setMultiSelect((on) => !on)}
            >
              Multi
            </button>
            <button
              type="button"
              aria-pressed={boxMode}
              title="Drags draw a selection box: to the right a window, to the left a crossing box"
              onClick={() => setBoxMode((on) => !on)}
            >
              Box
            </button>
            <button
              type="button"
              aria-pressed={grid}
              title="Show or hide the ground grid (G)"
              onClick={() => setGrid((shown) => !shown)}
            >
              Grid
            </button>
            <select
              aria-label="Mouse and trackpad navigation"
              title="Mouse and trackpad navigation (docs/INPUT.md)"
              value={navigation.preset}
              onChange={(event) =>
                setNavigation({
                  ...navigation,
                  preset: Number(event.currentTarget.value) as NavigationPreset,
                })
              }
            >
              <option value={NavigationPreset.Facetry}>Facetry navigation</option>
              <option value={NavigationPreset.Fusion}>Fusion navigation</option>
              <option value={NavigationPreset.Trackpad}>Trackpad navigation</option>
            </select>
            {hasTouch && (
              <select
                aria-label="One-finger drag"
                value={navigation.oneFinger}
                onChange={(event) =>
                  setNavigation({
                    ...navigation,
                    oneFinger: event.currentTarget.value === "pan" ? "pan" : "orbit",
                  })
                }
              >
                <option value="orbit">One finger orbits</option>
                <option value="pan">One finger pans</option>
              </select>
            )}
            <a href={stress ? "?" : "?stress"}>{stress ? "Normal" : "Stress test"}</a>
          </div>
          {viewport && <ViewCube viewport={viewport} />}
        </div>
        {model && (
          <Timeline
            state={model}
            onEdit={edit}
            selected={selectedFeature}
            onSelect={setSelectedFeature}
            repair={repair}
            onRepair={setRepair}
          />
        )}
        {repair && (
          <p className="panel hint">Select the edge that should replace the missing one.</p>
        )}
        <p className="panel stats">{stats ? formatStats(stats) : ""}</p>
        <p className="panel selection">{describe(selection)}</p>
      </div>
    </>
  );
}
