// Messages between the UI thread (CoreClient) and the core worker (core.worker.ts).

import type {
  FacetryCore,
  LogLevel,
  MeshData,
  ModelSnapshot,
  TessellationOptions,
  ToolEvent,
} from "./api";
import type { AutosaveEntry } from "./autosave";

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
  | "createSketchTool"
  | "cancelSketchTool"
  | "releaseSketchTool"
> & {
  /** A pointer event for a sketch tool: whether the document changed, and the tool's preview. */
  sketchToolEvent(
    session: number,
    tool: number,
    event: ToolEvent,
    u: number,
    v: number,
    snapRadius: number,
  ): { changed: boolean; preview: MeshData };
  /**
   * Regenerates in steps (see regenerate.ts) and tessellates every body. Resolves with null when
   * a newer regeneration of the session, or releasing it, took over.
   */
  regenerateModel(session: number, options?: TessellationOptions): ModelSnapshot | null;
  /** Saves the session as an autosave (autosave.ts). False when the browser has no storage. */
  autosave(session: number, entry: Omit<AutosaveEntry, "savedAt">): boolean;
  /** The autosaves of every tab. */
  listAutosaves(): AutosaveEntry[];
  /** Opens an autosave as a new session. */
  openAutosave(id: string): number;
  removeAutosave(id: string): void;
};
export type CoreMethod = keyof CoreMethods;

export interface CallRequest {
  id: number;
  method: CoreMethod;
  args: unknown[];
}

export type WorkerMessage =
  | { kind: "result"; id: number; value: unknown }
  | { kind: "error"; id: number; status: number; message: string }
  | { kind: "log"; level: LogLevel; message: string }
  | { kind: "progress"; done: number; total: number };
