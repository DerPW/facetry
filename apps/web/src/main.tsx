import "./style.css";

import { createRoot } from "react-dom/client";

import { App } from "./App";

const root = document.querySelector("#root");
if (!root) {
  throw new Error("Missing #root element");
}
// No StrictMode: its double-mounted effects would create the WebGL renderer twice.
createRoot(root).render(<App />);

// Offline support (PWA). Only in production builds, so development never serves stale files.
if (import.meta.env.PROD && "serviceWorker" in navigator) {
  void navigator.serviceWorker.register("./sw.js");
}
