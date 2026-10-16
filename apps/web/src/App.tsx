import { useCallback, useEffect, useState } from "react";

import { ViewportCanvas } from "./components/ViewportCanvas";
import { BooleanOp } from "./core/api";
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
const stress = params.has("stress");

/** Phase 0 tech preview: the viewport with a demo part, status, statistics and a few controls. */
export function App() {
  const [viewport, setViewport] = useState<Viewport | null>(null);
  const [status, setStatus] = useState("Starting the core…");
  const [ready, setReady] = useState<boolean | undefined>(undefined);
  const [stats, setStats] = useState<FrameStats | null>(null);
  const [selection, setSelection] = useState<Pick>(NOTHING);
  const [orthographic, setOrthographic] = useState(false);

  const onError = useCallback((message: string) => {
    setStatus(`Start-up failed: ${message}`);
    setReady(false);
  }, []);

  // Build the demo part in the core worker and show it: a plate with rounded edges, a boss on
  // top and a hole through both.
  useEffect(() => {
    if (!viewport) return;
    viewport.setListeners({ onSelectionChange: setSelection, onStats: setStats });
    let cancelled = false;
    const core = new CoreClient();
    void (async () => {
      try {
        const versions = await core.call("versions");
        const plate = await core.call("makeBox", 40, 30, 4);
        const rounded = await core.call(
          "fillet",
          plate,
          [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
          1.5,
        );
        const boss = await core.call("makeCylinder", 20, 15, 6, 12);
        const joined = await core.call("boolean", BooleanOp.Fuse, rounded, boss);
        const hole = await core.call("makeCylinder", 20, 15, 3, 12);
        const part = await core.call("boolean", BooleanOp.Cut, joined, hole);
        const mesh = await core.call("tessellate", part, {
          linearDeflection: stress ? 0.0004 : 0.01,
          angularDeflection: stress ? 0.02 : 0.2,
        });
        if (cancelled) return;
        viewport.setMesh(mesh);
        viewport.fit();
        setStatus(`Core ${versions.core} · OpenCASCADE ${versions.kernel} · ${versions.platform}`);
        setReady(true);
        if (import.meta.env.DEV) {
          // Handle for manual testing and benchmarks in the browser console.
          Object.assign(window, { facetry: { viewport, core } });
          const canvas = document.querySelector<HTMLCanvasElement>("#viewport");
          if (params.has("bench") && canvas) {
            setTimeout(() => void postBenchmark(viewport, canvas, mesh.triangles.length / 3), 1000);
          }
        }
      } catch (error) {
        if (!cancelled) onError(error instanceof Error ? error.message : String(error));
      }
    })();
    return () => {
      cancelled = true;
      core.terminate();
    };
  }, [viewport, onError]);

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
        <header className="panel header">
          <h1>Facetry</h1>
          <p className="muted">Version {__APP_VERSION__} · Phase 0</p>
          <p className="muted">{status}</p>
        </header>
        <div className="panel controls">
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
