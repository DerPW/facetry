import "./style.css";

declare const __APP_VERSION__: string;

const app = document.querySelector<HTMLElement>("#app");
if (!app) {
  throw new Error("Missing #app element");
}

const title = document.createElement("h1");
title.textContent = "Facetry";

const subtitle = document.createElement("p");
subtitle.textContent = `Version ${__APP_VERSION__} · Phase 0`;

app.replaceChildren(title, subtitle);
