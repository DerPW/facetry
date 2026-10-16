import js from "@eslint/js";
import { defineConfig } from "eslint/config";
import reactHooks from "eslint-plugin-react-hooks";
import globals from "globals";
import tseslint from "typescript-eslint";

export default defineConfig(
  { ignores: ["dist", "src/core/wasm/*.mjs", "src/render/wasm/*.mjs"] },
  js.configs.recommended,
  tseslint.configs.strict,
  reactHooks.configs.flat.recommended,
  {
    languageOptions: {
      globals: { ...globals.browser },
    },
  },
  {
    files: ["vite.config.ts", "vite/**/*.ts", "eslint.config.js"],
    languageOptions: {
      globals: { ...globals.node },
    },
  },
);
