// The core runs here, off the UI thread. Calls arrive as CallRequest messages; mesh buffers are
// transferred back without copying.

import { CoreError, FacetryCore, Status, type TessellationOptions } from "./api";
import type { CallRequest, WorkerMessage } from "./protocol";
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

function regenerateModel(core: FacetryCore, session: number, options?: TessellationOptions) {
  const regeneration = nextRegeneration++;
  latest.set(session, regeneration);
  return regenerateInSteps(core, session, options, {
    isCurrent: () => latest.get(session) === regeneration,
    onProgress: (done, total) => post({ kind: "progress", done, total }),
  });
}

self.onmessage = async (event: MessageEvent<CallRequest>) => {
  const { id, method, args } = event.data;
  try {
    const core = await ready;
    let value: unknown;
    if (method === "regenerateModel") {
      value = await regenerateModel(core, ...(args as [number, TessellationOptions?]));
    } else {
      const fn = core[method] as (...a: unknown[]) => unknown;
      value = fn.apply(core, args);
      if (method === "releaseSession") latest.delete(args[0] as number);
    }
    post({ kind: "result", id, value }, transferables(value));
  } catch (error) {
    const status = error instanceof CoreError ? error.status : Status.Internal;
    const message = error instanceof Error ? error.message : String(error);
    post({ kind: "error", id, status, message });
  }
};
