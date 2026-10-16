import "./style.css";

import { CoreClient } from "./core/client";

declare const __APP_VERSION__: string;

const app = document.querySelector<HTMLElement>("#app");
if (!app) {
  throw new Error("Missing #app element");
}

const line = (text: string, className?: string) => {
  const element = document.createElement("p");
  element.textContent = text;
  if (className) element.className = className;
  return element;
};

const title = document.createElement("h1");
title.textContent = "Facetry";
const status = line("Starting the core…", "status");
const detail = line("", "detail");
app.replaceChildren(title, line(`Version ${__APP_VERSION__} · Phase 0`), status, detail);

// Smoke test of the core running in its Web Worker: box → fillet → tessellate.
const core = new CoreClient();
try {
  const versions = await core.call("versions");
  const box = await core.call("makeBox", 20, 10, 5);
  const rounded = await core.call("fillet", box, [1], 2);
  const properties = await core.call("properties", rounded);
  const mesh = await core.call("tessellate", rounded);
  status.textContent = `Core ${versions.core} · OpenCASCADE ${versions.kernel} · ${versions.platform}`;
  detail.textContent =
    `Filleted box: ${properties.faces} faces, ${mesh.triangles.length / 3} triangles, ` +
    `${mesh.edgeIds.length} edges, ${properties.volume.toFixed(3)} mm³`;
  app.dataset.coreReady = "true";
} catch (error) {
  status.textContent = `The core failed to start: ${error instanceof Error ? error.message : String(error)}`;
  app.dataset.coreReady = "false";
}
