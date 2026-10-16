// The core runs here, off the UI thread. Calls arrive as CallRequest messages; mesh buffers are
// transferred back without copying.

import { CoreError, FacetryCore, Status } from "./api";
import type { CallRequest, CoreMethods, WorkerMessage } from "./protocol";

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

self.onmessage = async (event: MessageEvent<CallRequest>) => {
  const { id, method, args } = event.data;
  try {
    const core = await ready;
    const fn = core[method] as (...a: unknown[]) => ReturnType<CoreMethods[typeof method]>;
    const value = fn.apply(core, args);
    post({ kind: "result", id, value }, transferables(value));
  } catch (error) {
    const status = error instanceof CoreError ? error.status : Status.Internal;
    const message = error instanceof Error ? error.message : String(error);
    post({ kind: "error", id, status, message });
  }
};
