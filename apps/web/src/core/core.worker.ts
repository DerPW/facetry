// The core runs here, off the UI thread. Calls arrive as CallRequest messages; mesh buffers are
// transferred back without copying.

import { CoreError, FacetryCore, Status, type TessellationOptions, type ToolEvent } from "./api";
import { type AutosaveEntry, openAutosaveStore } from "./autosave";
import type { CallRequest, CoreMethod, WorkerMessage } from "./protocol";
import { regenerateInSteps } from "./regenerate";

const post = (message: WorkerMessage, transfer: Transferable[] = []) => {
  self.postMessage(message, { transfer });
};

const ready = FacetryCore.load({
  onLog: (level, message) => post({ kind: "log", level, message }),
});

// The buffers of every typed array in a result, e.g. the meshes of a model snapshot.
function transferables(value: unknown, found: Transferable[] = []): Transferable[] {
  if (ArrayBuffer.isView(value)) {
    found.push(value.buffer as ArrayBuffer);
  } else if (value !== null && typeof value === "object") {
    for (const item of Object.values(value)) transferables(item, found);
  }
  return found;
}

// The latest regeneration of each session; older ones stop at their next step.
const latest = new Map<number, number>();
let nextRegeneration = 1;

const autosaves = openAutosaveStore();
// One write at a time: a file in the origin private file system has one writer.
let writing: Promise<unknown> = Promise.resolve();
function queued<T>(work: () => Promise<T>): Promise<T> {
  const result = writing.then(work);
  writing = result.catch(() => undefined);
  return result;
}

type Local = { [M in CoreMethod]?: (core: FacetryCore, ...args: never[]) => unknown };

// Methods the worker adds to the core's.
const local = {
  regenerateModel(core: FacetryCore, session: number, options?: TessellationOptions) {
    const regeneration = nextRegeneration++;
    latest.set(session, regeneration);
    return regenerateInSteps(core, session, options, {
      isCurrent: () => latest.get(session) === regeneration,
      onProgress: (done, total) => post({ kind: "progress", done, total }),
    });
  },
  sketchToolEvent(
    core: FacetryCore,
    session: number,
    tool: number,
    event: ToolEvent,
    u: number,
    v: number,
    snapRadius: number,
    grid: number,
  ) {
    core.setSketchToolGrid(tool, grid);
    const changed = core.sketchToolPointer(session, tool, event, u, v, snapRadius);
    return {
      changed,
      preview: core.sketchToolPreview(session, tool),
      feedback: core.sketchToolFeedback(tool),
    };
  },
  releaseSession(core: FacetryCore, session: number) {
    latest.delete(session);
    core.releaseSession(session);
  },
  async autosave(core: FacetryCore, session: number, entry: Omit<AutosaveEntry, "savedAt">) {
    const store = await autosaves;
    if (!store) return false;
    const bytes = core.saveSessionFile(session);
    await queued(() => store.write({ ...entry, savedAt: Date.now() }, bytes));
    return true;
  },
  async listAutosaves() {
    return (await (await autosaves)?.list()) ?? [];
  },
  async openAutosave(core: FacetryCore, id: string) {
    const bytes = await (await autosaves)?.read(id);
    if (!bytes) throw new CoreError(Status.InvalidArgument, "This autosave no longer exists");
    return core.openSessionFile(bytes);
  },
  async removeAutosave(_core: FacetryCore, id: string) {
    const store = await autosaves;
    if (store) await queued(() => store.remove(id));
  },
} satisfies Local;

self.onmessage = async (event: MessageEvent<CallRequest>) => {
  const { id, method, args } = event.data;
  try {
    const core = await ready;
    const fn =
      method in local
        ? (local[method as keyof typeof local] as (...a: unknown[]) => unknown).bind(null, core)
        : (core[method as keyof FacetryCore] as (...a: unknown[]) => unknown).bind(core);
    const value = await fn(...args);
    post({ kind: "result", id, value }, transferables(value));
  } catch (error) {
    const status = error instanceof CoreError ? error.status : Status.Internal;
    const message = error instanceof Error ? error.message : String(error);
    post({ kind: "error", id, status, message });
  }
};
