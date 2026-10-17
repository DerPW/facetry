// UI-thread side of the core: every call goes to the core worker and returns a promise.

import { CoreError, LogLevel } from "./api";
import type { CallRequest, CoreMethod, CoreMethods, WorkerMessage } from "./protocol";

type Pending = { resolve: (value: unknown) => void; reject: (error: unknown) => void };

const consoleLog = (level: LogLevel, message: string) => {
  const text = `[core] ${message}`;
  if (level >= LogLevel.Error) console.error(text);
  else if (level >= LogLevel.Warning) console.warn(text);
  else if (level >= LogLevel.Info) console.info(text);
  else console.debug(text);
};

export class CoreClient {
  readonly #worker: Worker;
  readonly #pending = new Map<number, Pending>();
  #nextId = 1;
  onLog: (level: LogLevel, message: string) => void = consoleLog;
  /** Progress of a regeneration that takes longer than a step: `done` of `total` features. */
  onProgress: (done: number, total: number) => void = () => {};

  constructor(
    worker = new Worker(new URL("./core.worker.ts", import.meta.url), { type: "module" }),
  ) {
    this.#worker = worker;
    this.#worker.onmessage = (event: MessageEvent<WorkerMessage>) => this.#receive(event.data);
  }

  call<M extends CoreMethod>(
    method: M,
    ...args: Parameters<CoreMethods[M]>
  ): Promise<ReturnType<CoreMethods[M]>> {
    const id = this.#nextId++;
    const request: CallRequest = { id, method, args };
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
      this.#worker.postMessage(request);
    });
  }

  terminate(): void {
    this.#worker.terminate();
    for (const pending of this.#pending.values()) {
      pending.reject(new CoreError(7, "Core worker terminated"));
    }
    this.#pending.clear();
  }

  #receive(message: WorkerMessage): void {
    if (message.kind === "log") {
      this.onLog(message.level, message.message);
      return;
    }
    if (message.kind === "progress") {
      this.onProgress(message.done, message.total);
      return;
    }
    const pending = this.#pending.get(message.id);
    if (!pending) return;
    this.#pending.delete(message.id);
    if (message.kind === "result") {
      pending.resolve(message.value);
    } else {
      pending.reject(new CoreError(message.status, message.message));
    }
  }
}
