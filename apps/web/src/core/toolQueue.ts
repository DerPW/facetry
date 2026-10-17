// Sends a sketch tool's pointer events to the core worker one at a time, in order. Moves that
// pile up while a call runs collapse into the latest, so the preview keeps up with the pointer.

import { ToolEvent, type MeshData } from "./api";

export interface ToolPointer {
  event: ToolEvent;
  u: number;
  v: number;
  snapRadius: number;
}

export interface ToolResult {
  changed: boolean;
  preview: MeshData;
}

export class ToolQueue {
  readonly #send: (pointer: ToolPointer) => Promise<ToolResult>;
  readonly #onResult: (result: ToolResult) => void;
  readonly #onError: (error: unknown) => void;
  readonly #pending: ToolPointer[] = [];
  #running = false;
  #closed = false;

  constructor(
    send: (pointer: ToolPointer) => Promise<ToolResult>,
    onResult: (result: ToolResult) => void,
    onError: (error: unknown) => void,
  ) {
    this.#send = send;
    this.#onResult = onResult;
    this.#onError = onError;
  }

  /** Drops what is still waiting; results of a call already running are dropped too. */
  close(): void {
    this.#closed = true;
    this.#pending.length = 0;
  }

  push(pointer: ToolPointer): void {
    if (this.#closed) return;
    const last = this.#pending.at(-1);
    if (pointer.event === ToolEvent.Move && last?.event === ToolEvent.Move) {
      this.#pending[this.#pending.length - 1] = pointer;
    } else {
      this.#pending.push(pointer);
    }
    void this.#run();
  }

  async #run(): Promise<void> {
    if (this.#running) return;
    this.#running = true;
    try {
      for (let next = this.#pending.shift(); next; next = this.#pending.shift()) {
        try {
          const result = await this.#send(next);
          if (!this.#closed) this.#onResult(result);
        } catch (error) {
          if (!this.#closed) this.#onError(error);
        }
      }
    } finally {
      this.#running = false;
    }
  }
}
