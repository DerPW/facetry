import js from "@eslint/js";
import { defineConfig } from "eslint/config";
import globals from "globals";
import tseslint from "typescript-eslint";

export default defineConfig(
  { ignores: ["dist"] },
  js.configs.recommended,
  tseslint.configs.strict,
  {
    languageOptions: {
      globals: { ...globals.browser },
    },
  },
  {
    files: ["vite.config.ts", "eslint.config.js"],
    languageOptions: {
      globals: { ...globals.node },
    },
  },
);
