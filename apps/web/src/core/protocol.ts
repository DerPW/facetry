// Messages between the UI thread (CoreClient) and the core worker (core.worker.ts).

import type { FacetryCore, LogLevel } from "./api";

/** Core methods callable from the UI thread. */
export type CoreMethods = Pick<
  FacetryCore,
  | "versions"
  | "makeBox"
  | "makeCylinder"
  | "boolean"
  | "fillet"
  | "properties"
  | "tessellate"
  | "release"
  | "openSession"
  | "saveSession"
  | "openSessionFile"
  | "saveSessionFile"
  | "applyEdits"
  | "undo"
  | "beginTransaction"
  | "endTransaction"
  | "redo"
  | "sessionState"
  | "elementNames"
  | "makeReference"
  | "releaseSession"
  | "regenerateModel"
>;
export type CoreMethod = keyof CoreMethods;

export interface CallRequest {
  id: number;
  method: CoreMethod;
  args: unknown[];
}

export type WorkerMessage =
  | { kind: "result"; id: number; value: unknown }
  | { kind: "error"; id: number; status: number; message: string }
  | { kind: "log"; level: LogLevel; message: string };
