// Regeneration in the core worker, in steps of about one frame (#13). Between steps the worker
// handles its other messages, so edits get through while a long regeneration runs, and a newer
// regeneration takes over from an older one.

import type { FacetryCore, ModelSnapshot, TessellationOptions } from "./api";

export interface StepOptions {
  /** Time per step in milliseconds. A step ends after the feature that ran past it. */
  stepMilliseconds?: number;
  /** Asked between steps; returning false abandons the regeneration. */
  isCurrent?: () => boolean;
  /** Called between steps: `done` of `total` features are finished. */
  onProgress?: (done: number, total: number) => void;
  /** Lets other work run between steps. */
  pause?: () => Promise<void>;
}

/** Resolves in a new task, after the messages that arrived meanwhile were handled. */
export function nextTask(): Promise<void> {
  return new Promise((resolve) => {
    const { port1, port2 } = new MessageChannel();
    port1.onmessage = () => {
      port1.close();
      resolve();
    };
    port2.postMessage(null);
  });
}

/**
 * Regenerates step by step, then tessellates every body. Each step picks up the session's
 * current document, so edits made between steps are included; finished features stay cached.
 * Resolves with null when the regeneration was abandoned.
 */
export async function regenerateInSteps(
  core: FacetryCore,
  session: number,
  tessellation: TessellationOptions = {},
  { stepMilliseconds = 20, isCurrent = () => true, onProgress, pause = nextTask }: StepOptions = {},
): Promise<ModelSnapshot | null> {
  // Each step goes past where the previous one stopped, so a long run of cached features
  // cannot keep a step from finishing anything.
  let reached = 0;
  for (;;) {
    const start = performance.now();
    let done = 0;
    let total = 0;
    let stopped = false;
    core.regenerate(session, (finished, all) => {
      total = all;
      // The same count again: a long kernel operation asking whether to stop. It could not
      // resume later, so it runs to the end.
      if (finished === done) return true;
      done = finished;
      // Features taken from the cache still report after a stop; the last one ends the run.
      stopped =
        finished < all &&
        (stopped || (finished > reached && performance.now() - start >= stepMilliseconds));
      return !stopped;
    });
    if (!stopped) return core.modelSnapshot(session, tessellation);
    reached = done;
    onProgress?.(done, total);
    await pause();
    if (!isCurrent()) return null;
  }
}
