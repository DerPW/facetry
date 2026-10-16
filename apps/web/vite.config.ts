import react from "@vitejs/plugin-react";
import { readFileSync } from "node:fs";
import { defineConfig, type Plugin } from "vite";

import { serviceWorker } from "./vite/service-worker";

const pkg = JSON.parse(readFileSync(new URL("./package.json", import.meta.url), "utf8")) as {
  version: string;
};

// Dev server only: pages opened with ?bench post their measurements to /__bench, which prints
// them to the dev server's log. Used for docs/benchmarks.md in browsers that cannot be scripted.
const benchLog: Plugin = {
  name: "facetry-bench-log",
  apply: "serve",
  configureServer(server) {
    server.middlewares.use("/__bench", (request, response) => {
      let body = "";
      request.on("data", (chunk: Buffer) => (body += chunk.toString()));
      request.on("end", () => {
        server.config.logger.info(`[bench] ${body}`);
        response.statusCode = 204;
        response.end();
      });
    });
  },
};

export default defineConfig({
  plugins: [react(), serviceWorker(), benchLog],
  // Relative base so the build works under any path, e.g. GitHub Pages at /facetry/.
  base: "./",
  define: {
    __APP_VERSION__: JSON.stringify(pkg.version),
  },
  build: {
    target: "es2022",
  },
});
