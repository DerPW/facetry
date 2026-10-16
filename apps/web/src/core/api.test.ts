// Runs the real WebAssembly core in Node. Build it first:
//   cmake --preset wasm-release && cmake --build --preset wasm-release --target facetry-core-web
import { readFileSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import { BooleanOp, CoreError, ElementKind, FacetryCore, LogLevel, Status } from "./api";

const example = readFileSync(
  new URL("../../../../tests/documents/corner-fillet.json", import.meta.url),
  "utf8",
);

/** 10 × 6 × 4 block with one vertical edge rounded by r = 1.5. */
const cornerFilletVolume = (width: number) => width * 6 * 4 - (1 - Math.PI / 4) * 1.5 * 1.5 * 4;

describe("FacetryCore (WebAssembly)", () => {
  let core: FacetryCore;
  const logs: string[] = [];

  beforeAll(async () => {
    core = await FacetryCore.load({
      onLog: (_level, message) => logs.push(message),
      logLevel: LogLevel.Debug,
    });
  });

  afterAll(() => core.dispose());

  it("reports versions", () => {
    const v = core.versions();
    expect(v.api.major).toBe(0);
    expect(v.platform).toBe("wasm");
    expect(v.kernel).toBe("8.0.1");
  });

  it("makes a box, fillets an edge and tessellates it", () => {
    const box = core.makeBox(20, 10, 5);
    const rounded = core.fillet(box, [1], 1);
    const props = core.properties(rounded);
    expect(props.faces).toBe(7);
    expect(props.volume).toBeLessThan(1000);

    const mesh = core.tessellate(rounded);
    expect(mesh.positions.length % 3).toBe(0);
    expect(mesh.normals.length).toBe(mesh.positions.length);
    expect(mesh.triangleFaceIds.length).toBe(mesh.triangles.length / 3);
    expect(mesh.edgeOffsets.length).toBe(mesh.edgeIds.length + 1);
    expect(mesh.vertexIds.length).toBe(props.vertices);
    const vertexCount = mesh.positions.length / 3;
    expect(Math.max(...mesh.triangles)).toBeLessThan(vertexCount);

    core.release(rounded);
    core.release(box);
  });

  it("cuts a hole with the expected volume", () => {
    const plate = core.boolean(
      BooleanOp.Cut,
      core.makeBox(20, 10, 5),
      core.makeCylinder(10, 5, 2, 5),
    );
    expect(core.properties(plate).volume).toBeCloseTo(1000 - Math.PI * 20, 6);
  });

  it("turns failures into CoreError with status and message", () => {
    expect(() => core.makeBox(-1, 1, 1)).toThrow(CoreError);
    const tiny = core.makeBox(1, 1, 1);
    expect(() => core.fillet(tiny, [1], 5)).toThrow(
      expect.objectContaining({ name: "CoreError", status: Status.Kernel }),
    );
    expect(logs.some((line) => line.includes("Box width"))).toBe(true);
  });

  describe("sessions", () => {
    it("open, regenerate and edit the example document", () => {
      const session = core.openSession(example);
      const snapshot = core.regenerateModel(session);
      expect(snapshot.state.features.map((f) => f.status)).toEqual(["ok", "ok", "ok"]);
      expect(snapshot.state.bodies).toEqual(["f2"]);
      const mesh = snapshot.meshes.f2;
      expect(mesh?.triangles.length).toBeGreaterThan(0);

      const shape = core.bodyShape(session, "f2");
      const props = core.properties(shape);
      expect(props.volume).toBeCloseTo(cornerFilletVolume(10), 6);
      const names = core.elementNames(session, "f2");
      expect(names.faces).toHaveLength(props.faces);
      expect(names.edges).toHaveLength(props.edges);
      core.release(shape);

      // Mesh face IDs index the names.
      const faceId = mesh?.triangleFaceIds[0] ?? 0;
      expect(names.faces[faceId - 1]).toMatch(/^f\d/);

      core.applyEdits(session, { op: "setParameter", name: "width", value: 14 });
      expect(core.sessionState(session).needsRegeneration).toBe(true);
      core.regenerate(session);
      const wider = core.bodyShape(session, "f2");
      expect(core.properties(wider).volume).toBeCloseTo(cornerFilletVolume(14), 6);
      core.release(wider);

      expect(core.undo(session)).toBe(true);
      expect(core.redo(session)).toBe(true);
      expect(core.undo(session)).toBe(true);
      expect(core.undo(session)).toBe(false);

      const reference = core.makeReference(session, "f2", ElementKind.Edge, 1);
      expect(reference.kind).toBe("edge");
      expect(reference.name).toMatch(/^edge\{/);
      expect(JSON.parse(core.saveSession(session)).format).toBe("facetry-document");
      core.releaseSession(session);
    });

    it("saves and opens .facetry files", () => {
      const session = core.openSession(example);
      core.applyEdits(session, { op: "setParameter", name: "width", value: 12 });
      const file = core.saveSessionFile(session);
      expect(String.fromCharCode(file[0] ?? 0, file[1] ?? 0)).toBe("PK");
      const reopened = core.openSessionFile(file);
      expect(core.sessionState(reopened).parameters[0]).toEqual({ name: "width", value: 12 });
      expect(core.saveSessionFile(reopened)).toEqual(file);
      const checkedIn = readFileSync(
        new URL("../../../../tests/documents/corner-fillet.facetry", import.meta.url),
      );
      expect(core.sessionState(core.openSessionFile(checkedIn)).features).toHaveLength(3);
      expect(() => core.openSessionFile(new TextEncoder().encode("junk"))).toThrow(CoreError);
    });

    it("cancels from the progress callback", () => {
      const session = core.openSession(example);
      const calls: number[] = [];
      core.regenerate(session, (done, total) => {
        calls.push(done);
        expect(total).toBe(3);
        return false;
      });
      expect(calls).toEqual([1]);
      const state = core.sessionState(session);
      expect(state.regeneration.cancelled).toBe(true);
      expect(state.features.map((f) => f.status)).toEqual(["ok", "notRun", "notRun"]);
      core.releaseSession(session);
    });

    it("rejects bad edits without changing the document", () => {
      const session = core.openSession(example);
      const before = core.saveSession(session);
      expect(() =>
        core.applyEdits(session, [
          { op: "setParameter", name: "width", value: 20 },
          { op: "deleteFeature", id: "f9" },
        ]),
      ).toThrow(expect.objectContaining({ status: Status.InvalidArgument }));
      expect(core.saveSession(session)).toBe(before);
      expect(() => core.openSession("{}")).toThrow(CoreError);
      core.releaseSession(session);
    });
  });
});
