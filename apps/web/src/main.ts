import "./style.css";

import { BooleanOp } from "./core/api";
import { CoreClient } from "./core/client";
import { PickKind, Viewport, type Pick } from "./render/viewport";

declare const __APP_VERSION__: string;

const app = document.querySelector<HTMLElement>("#app");
const canvas = document.querySelector<HTMLCanvasElement>("#viewport");
if (!app || !canvas) {
  throw new Error("Missing #app or #viewport element");
}

const element = <K extends keyof HTMLElementTagNameMap>(tag: K, className: string, text = "") => {
  const node = document.createElement(tag);
  node.className = className;
  node.textContent = text;
  return node;
};

// Phase 0 tech preview UI: title, core status, frame statistics, selection and a few controls.
const header = element("header", "panel header");
header.append(
  element("h1", "", "Facetry"),
  element("p", "muted", `Version ${__APP_VERSION__} · Phase 0`),
);
const status = element("p", "muted", "Starting the core…");
header.append(status);
const stats = element("p", "panel stats", "");
const selection = element("p", "panel selection", "Click a face, edge or vertex");
const controls = element("div", "panel controls");
const fitButton = element("button", "", "Fit");
const orthoButton = element("button", "", "Orthographic");
const stressLink = element(
  "a",
  "",
  new URLSearchParams(location.search).has("stress") ? "Normal" : "Stress test",
);
stressLink.setAttribute(
  "href",
  new URLSearchParams(location.search).has("stress") ? "?" : "?stress",
);
controls.append(fitButton, orthoButton, stressLink);
app.replaceChildren(header, controls, stats, selection);

const kindName: Record<number, string> = {
  [PickKind.Face]: "Face",
  [PickKind.Edge]: "Edge",
  [PickKind.Vertex]: "Vertex",
};
const describe = (pick: Pick) =>
  pick.kind === PickKind.None ? "Nothing selected" : `${kindName[pick.kind]} ${pick.id} selected`;

try {
  const [core, viewport] = await Promise.all([
    Promise.resolve(new CoreClient()),
    Viewport.create(canvas),
  ]);
  const versions = await core.call("versions");
  status.textContent = `Core ${versions.core} · OpenCASCADE ${versions.kernel} · ${versions.platform}`;

  // Demo part: a plate with rounded edges, a boss on top and a hole through both.
  const stress = new URLSearchParams(location.search).has("stress");
  const plate = await core.call("makeBox", 40, 30, 4);
  const rounded = await core.call("fillet", plate, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], 1.5);
  const boss = await core.call("makeCylinder", 20, 15, 6, 12);
  const joined = await core.call("boolean", BooleanOp.Fuse, rounded, boss);
  const hole = await core.call("makeCylinder", 20, 15, 3, 12);
  const part = await core.call("boolean", BooleanOp.Cut, joined, hole);
  const mesh = await core.call("tessellate", part, {
    linearDeflection: stress ? 0.0004 : 0.01,
    angularDeflection: stress ? 0.02 : 0.2,
  });
  viewport.setMesh(mesh);
  viewport.fit();

  let orthographic = false;
  fitButton.addEventListener("click", () => viewport.fit());
  orthoButton.addEventListener("click", () => {
    orthographic = !orthographic;
    viewport.setOrthographic(orthographic);
    orthoButton.textContent = orthographic ? "Perspective" : "Orthographic";
  });
  viewport.onSelectionChange = (pick) => {
    selection.textContent = describe(pick);
  };
  viewport.onStats = (frame) => {
    const fps = 1000 / frame.frameMilliseconds;
    stats.textContent =
      `${frame.frameMilliseconds.toFixed(1)} ms/frame (${fps.toFixed(0)} fps) · ` +
      `draw ${frame.cpuMilliseconds.toFixed(2)} ms · ${frame.triangles.toLocaleString("en")} triangles`;
    app.dataset.fps = fps.toFixed(1);
  };
  if (import.meta.env.DEV) {
    // Handle for manual testing and benchmarks in the browser console.
    Object.assign(window, { facetry: { viewport, core } });
    if (new URLSearchParams(location.search).has("bench")) {
      setTimeout(() => void postBenchmark(viewport, mesh.triangles.length / 3), 1000);
    }
  }
  app.dataset.coreReady = "true";
  app.dataset.triangles = String(mesh.triangles.length / 3);
} catch (error) {
  status.textContent = `Start-up failed: ${error instanceof Error ? error.message : String(error)}`;
  app.dataset.coreReady = "false";
}

// Dev only (see vite.config.ts): measures draw and pick times and posts them to the dev server.
async function postBenchmark(viewport: Viewport, triangles: number) {
  viewport.benchmark(10);
  const frameMs = [viewport.benchmark(60), viewport.benchmark(60), viewport.benchmark(60)];
  const width = canvas?.clientWidth ?? 0;
  const height = canvas?.clientHeight ?? 0;
  const started = performance.now();
  for (let i = 0; i < 200; i++) {
    viewport.pick((((i * 37) % 97) / 97) * width, (((i * 53) % 89) / 89) * height, 18);
  }
  const pickMs = (performance.now() - started) / 200;
  // Frames actually presented in two seconds while rendering continuously.
  const frames = await new Promise<number>((resolve) => {
    let count = 0;
    const until = performance.now() + 2000;
    const tick = () => {
      count++;
      if (performance.now() < until) requestAnimationFrame(tick);
      else resolve(count);
    };
    requestAnimationFrame(tick);
  });
  await fetch("/__bench", {
    method: "POST",
    body: JSON.stringify({
      userAgent: navigator.userAgent,
      triangles,
      canvas: [canvas?.width, canvas?.height],
      frameMs: frameMs.map((ms) => Number(ms.toFixed(2))),
      pickMs: Number(pickMs.toFixed(3)),
      presentedFps: frames / 2,
    }),
  });
}
