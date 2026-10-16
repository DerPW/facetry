import { useCallback, useEffect, useRef, useState } from "react";

// The M0.6 example document, opened on start until documents can be opened and saved (#11).
import exampleDocument from "../../../tests/documents/corner-fillet.json?raw";
import { ModelPanel } from "./components/ModelPanel";
import { ViewportCanvas } from "./components/ViewportCanvas";
import type { Edit, ModelSnapshot, SessionState } from "./core/api";
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
// ?stress tessellates finely (hundreds of thousands of triangles) for performance checks.
const stress = params.has("stress");
const tessellation = {
  linearDeflection: stress ? 0.0004 : 0.01,
  angularDeflection: stress ? 0.02 : 0.2,
};

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
        const handle = await core.call("openSession", exampleDocument);
        const snapshot = await core.call("regenerateModel", handle, tessellation);
        if (cancelled) return;
        session.current = { core, handle };
        const mesh = show(snapshot);
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
            <p className="muted">Version {__APP_VERSION__} · tech preview</p>
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
