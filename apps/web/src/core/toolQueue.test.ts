import { describe, expect, it } from "vitest";

import { ToolEvent, type MeshData } from "./api";
import { ToolQueue, type ToolPointer } from "./toolQueue";

const preview = {} as MeshData;
const feedback = { snap: "", inferences: [] };

describe("ToolQueue", () => {
  it("sends events in order and collapses moves that pile up", async () => {
    const sent: ToolPointer[] = [];
    let release: () => void = () => undefined;
    const results: boolean[] = [];
    const queue = new ToolQueue(
      (pointer) => {
        sent.push(pointer);
        return new Promise((resolve) => {
          release = () => resolve({ changed: pointer.event === ToolEvent.Up, preview, feedback });
        });
      },
      (result) => results.push(result.changed),
      () => undefined,
    );
    const at = (event: ToolEvent, u: number) => ({ event, u, v: 0, snapRadius: 1, grid: 0 });
    queue.push(at(ToolEvent.Move, 1)); // sent at once
    queue.push(at(ToolEvent.Move, 2)); // replaced by the next move
    queue.push(at(ToolEvent.Move, 3));
    queue.push(at(ToolEvent.Down, 4));
    queue.push(at(ToolEvent.Up, 5));
    for (let i = 0; i < 4; i++) {
      release();
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
    expect(sent.map((p) => p.u)).toEqual([1, 3, 4, 5]);
    expect(results).toEqual([false, false, false, true]);
  });
});
