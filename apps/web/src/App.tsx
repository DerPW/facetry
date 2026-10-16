import { useCallback, useEffect, useRef, useState } from "react";

// The M0.6 example document, opened on start until documents can be opened and saved (#11).
import exampleDocument from "../../../tests/documents/corner-fillet.json?raw";
import { ModelPanel } from "./components/ModelPanel";
import { ViewportCanvas } from "./components/ViewportCanvas";
import {
  BooleanOp,
  type Edit,
  type MeshData,
  type ModelSnapshot,
  type SessionState,
} from "./core/api";
import { CoreClient } from "./core/client";
import { postBenchmark } from "./dev/bench";
import { PickKind, type FrameStats, type Pick, type Viewport } from "./render/viewport";

declare const __APP_VERSION__: string;

const NOTHING: Pick = { kind: PickKind.None, id: 0 };
const kindName: Record<number, string> = {
  [PickKind.Face]: "Face",
  [PickKind.Edge]: "Edge",
  [PickKind.Vertex]: "Vertex",
};

function describe(pick: Pick): string {
  return pick.kind === PickKind.None
    ? "Nothing selected"
    : `${kindName[pick.kind]} ${pick.id} selected`;
}

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
  const [selection, setSelection] = useState<Pick>(NOTHING);
  const [orthographic, setOrthographic] = useState(false);
  const [model, setModel] = useState<SessionState | null>(null);
  const [editError, setEditError] = useState<string | null>(null);
  const [fileName, setFileName] = useState("Untitled.facetry");
  const fileInput = useRef<HTMLInputElement>(null);
  const session = useRef<Session | null>(null);

  const onError = useCallback((message: string) => {
    setStatus(`Start-up failed: ${message}`);
    setReady(false);
  }, []);

  const show = useCallback(
    (snapshot: ModelSnapshot) => {
      // One body for now; the browser panel (#15) shows several.
      const body = snapshot.state.bodies[0];
      const mesh = body === undefined ? undefined : snapshot.meshes[body];
      if (viewport && mesh) {
        // Element IDs change with the geometry, so an old selection would point elsewhere.
        viewport.clearSelection();
        viewport.setMesh(mesh);
      }
      setModel(snapshot.state);
      return mesh;
    },
    [viewport],
  );

  // Open the example document in the core worker and show it.
  useEffect(() => {
    if (!viewport) return;
    viewport.setListeners({ onSelectionChange: setSelection, onStats: setStats });
    let cancelled = false;
    const core = new CoreClient();
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
          if (cancelled) return;
          session.current = { core, handle };
          mesh = show(snapshot);
        }
        viewport.fit();
        setStatus(`Core ${versions.core} · OpenCASCADE ${versions.kernel} · ${versions.platform}`);
        setReady(true);
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

  // Runs a change in the core worker, then regenerates and shows the result. Calls queue up in
  // the worker and run in order.
  const change = useCallback(
    async (run: (current: Session) => Promise<unknown>) => {
      const current = session.current;
      if (!current) return;
      try {
        await run(current);
        setEditError(null);
        show(await current.core.call("regenerateModel", current.handle, tessellation));
      } catch (error) {
        setEditError(error instanceof Error ? error.message : String(error));
      }
    },
    [show],
  );

  const edit = useCallback(
    (edits: Edit | Edit[]) =>
      void change(({ core, handle }) => core.call("applyEdits", handle, edits)),
    [change],
  );

  // Opens a .facetry file in place of the current document. Saving and opening with the File
  // System Access API, and autosave, come with M1.9 and #17.
  const openFile = useCallback(
    async (file: File) => {
      const current = session.current;
      if (!current || !viewport) return;
      try {
        const bytes = new Uint8Array(await file.arrayBuffer());
        const handle = await current.core.call("openSessionFile", bytes);
        await current.core.call("releaseSession", current.handle);
        current.handle = handle;
        show(await current.core.call("regenerateModel", handle, tessellation));
        viewport.fit();
        setFileName(file.name.endsWith(".facetry") ? file.name : `${file.name}.facetry`);
        setEditError(null);
      } catch (error) {
        setEditError(error instanceof Error ? error.message : String(error));
      }
    },
    [show, viewport],
  );

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
    } catch (error) {
      setEditError(error instanceof Error ? error.message : String(error));
    }
  }, [fileName]);

  useEffect(() => {
    viewport?.setOrthographic(orthographic);
  }, [viewport, orthographic]);

  // Keyboard shortcuts (docs/INPUT.md).
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (
        event.metaKey ||
        event.ctrlKey ||
        event.altKey ||
        event.target instanceof HTMLInputElement
      ) {
        return;
      }
      if (event.key === "f") viewport?.fit();
      else if (event.key === "o") setOrthographic((value) => !value);
      else if (event.key === "Escape") viewport?.clearSelection();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [viewport]);

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
            {editError && <p className="error">{editError}</p>}
          </header>
          {model && (
            <ModelPanel
              state={model}
              onSetParameter={(name, value) => edit({ op: "setParameter", name, value })}
            />
          )}
        </div>
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
            onClick={() => void change(({ core, handle }) => core.call("undo", handle))}
          >
            Undo
          </button>
          <button
            type="button"
            disabled={!model?.canRedo}
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
          <a href={stress ? "?" : "?stress"}>{stress ? "Normal" : "Stress test"}</a>
        </div>
        <p className="panel stats">{stats ? formatStats(stats) : ""}</p>
        <p className="panel selection">{describe(selection)}</p>
      </div>
    </>
  );
}
