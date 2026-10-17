// Step-wise regeneration (#13) with the real WebAssembly core in Node; see api.test.ts.
import { readFileSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import { FacetryCore } from "./api";
import { regenerateInSteps } from "./regenerate";

const example = readFileSync(
  new URL("../../../../tests/documents/corner-fillet.json", import.meta.url),
  "utf8",
);

describe("regenerateInSteps", () => {
  let core: FacetryCore;

  beforeAll(async () => {
    core = await FacetryCore.load();
  });

  afterAll(() => core.dispose());

  const volume = (session: number) => {
    const shape = core.bodyShape(session, "f2");
    const result = core.properties(shape).volume;
    core.release(shape);
    return result;
  };

  it("runs a feature per step and reports progress between steps", async () => {
    const session = core.openSession(example);
    const progress: [number, number][] = [];
    let pauses = 0;
    const snapshot = await regenerateInSteps(
      core,
      session,
      {},
      {
        stepMilliseconds: 0,
        onProgress: (done, total) => progress.push([done, total]),
        pause: () => {
          pauses += 1;
          return Promise.resolve();
        },
      },
    );
    expect(progress).toEqual([
      [1, 3],
      [2, 3],
    ]);
    expect(pauses).toBe(2);
    expect(snapshot?.state.features.map((f) => f.status)).toEqual(["ok", "ok", "ok"]);
    expect(snapshot?.state.regeneration.executed).toBe(1); // the earlier steps are cached
    expect(snapshot?.meshes.f2?.triangles.length).toBeGreaterThan(0);
    core.releaseSession(session);
  });

  it("includes edits made between steps", async () => {
    const session = core.openSession(example);
    const reference = core.openSession(example);
    core.applyEdits(reference, { op: "setParameter", name: "width", value: 14 });
    core.regenerate(reference);

    let edited = false;
    const snapshot = await regenerateInSteps(
      core,
      session,
      {},
      {
        stepMilliseconds: 0,
        pause: () => {
          if (!edited) core.applyEdits(session, { op: "setParameter", name: "width", value: 14 });
          edited = true;
          return Promise.resolve();
        },
      },
    );
    expect(snapshot?.state.parameters.find((p) => p.name === "width")?.value).toBe(14);
    expect(volume(session)).toBeCloseTo(volume(reference), 6);
    core.releaseSession(session);
    core.releaseSession(reference);
  });

  it("stops when a newer regeneration takes over, which continues from the cache", async () => {
    const session = core.openSession(example);
    const abandoned = await regenerateInSteps(
      core,
      session,
      {},
      { stepMilliseconds: 0, isCurrent: () => false, pause: () => Promise.resolve() },
    );
    expect(abandoned).toBeNull();
    const snapshot = await regenerateInSteps(core, session);
    expect(snapshot?.state.regeneration.executed).toBe(2);
    expect(snapshot?.state.regeneration.cancelled).toBe(false);
    core.releaseSession(session);
  });

  it("finishes in one step when there is time", async () => {
    const session = core.openSession(example);
    let pauses = 0;
    const snapshot = await regenerateInSteps(
      core,
      session,
      {},
      {
        stepMilliseconds: 60_000,
        pause: () => {
          pauses += 1;
          return Promise.resolve();
        },
      },
    );
    expect(pauses).toBe(0);
    expect(snapshot?.state.regeneration.executed).toBe(3);
    core.releaseSession(session);
  });
});
