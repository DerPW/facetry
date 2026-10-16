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

function transferables(value: unknown): Transferable[] {
  if (value === null || typeof value !== "object") {
    return [];
  }
  return Object.values(value)
    .filter((item): item is ArrayBufferView => ArrayBuffer.isView(item))
    .map((view) => view.buffer as ArrayBuffer);
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
