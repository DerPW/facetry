// Runs the real WebAssembly core in Node. Build it first:
//   cmake --preset wasm-release && cmake --build --preset wasm-release --target facetry-core-web
import { readFileSync } from "node:fs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import {
  BooleanOp,
  CoreError,
  ElementKind,
  FacetryCore,
  LogLevel,
  Status,
  sketchCurves,
} from "./api";

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
    it("regenerate sketches on origin planes and faces, with arcs and circles", () => {
      const session = core.openSession(
        readFileSync(
          new URL("../../../../tests/documents/sketch-planes.json", import.meta.url),
          "utf8",
        ),
      );
      const snapshot = core.regenerateModel(session);
      expect(snapshot.state.features.map((f) => f.status)).toEqual(Array(6).fill("ok"));
      expect(snapshot.state.features[2]?.definition.plane).toBe("face");
      // Every sketch has one region (profile), with a mesh to show and pick it.
      expect(snapshot.state.features[0]?.regions).toHaveLength(1);
      expect(Object.keys(snapshot.regions).sort()).toEqual(["f1", "f3", "f5"]);
      expect(new Set(snapshot.regions.f1?.triangleFaceIds)).toEqual(new Set([1]));
      // The slot sketch for display: lines l7, l8, the construction line l9, arcs a10, a11.
      const slot = snapshot.state.features[0];
      expect(slot?.frame?.normal).toEqual([0, -1, 0]);
      expect(sketchCurves(slot?.definition ?? { type: "sketch" })).toEqual([
        { id: "l7", construction: false },
        { id: "l8", construction: false },
        { id: "l9", construction: true },
        { id: "a10", construction: false },
        { id: "a11", construction: false },
      ]);
      expect(Array.from(snapshot.sketches.f1?.edgeIds ?? [])).toEqual([1, 2, 3, 4, 5]);
      expect(snapshot.sketches.f1?.vertexIds.length).toBe(6);
      expect(snapshot.sketches.f1?.triangles.length).toBe(0);
      const volume = (body: string) => {
        const shape = core.bodyShape(session, body);
        const result = core.properties(shape).volume;
        core.release(shape);
        return result;
      };
      expect(volume("f2")).toBeCloseTo((20 * 10 + Math.PI * 25) * 8, 6);
      expect(volume("f4")).toBeCloseTo(Math.PI * 9 * 4, 6);
      core.releaseSession(session);
    });

    it("open, regenerate and edit the example document", () => {
      const session = core.openSession(example);
      const snapshot = core.regenerateModel(session);
      expect(snapshot.state.features.map((f) => f.status)).toEqual(["ok", "ok", "ok"]);
      expect(snapshot.state.bodies).toEqual([
        { id: "f2", name: "Body 1", visible: true, opacity: 1 },
      ]);
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

    it("changes units and keeps the geometry", () => {
      const session = core.openSession(example);
      expect(core.regenerateModel(session).state.units).toBe("mm");
      core.applyEdits(session, { op: "setUnits", units: "in" });
      const state = core.regenerateModel(session).state;
      expect(state.units).toBe("in");
      expect(state.undoName).toBe("Change units");
      expect(state.parameters.find((p) => p.name === "width")?.value).toBeCloseTo(10 / 25.4, 12);
      const shape = core.bodyShape(session, "f2");
      expect(core.properties(shape).volume).toBeCloseTo(cornerFilletVolume(10), 6);
      core.release(shape);
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

    it("names undo steps and groups transactions", () => {
      const session = core.openSession(example);
      core.applyEdits(session, { op: "renameFeature", id: "f2", name: "Block" });
      expect(core.sessionState(session).undoName).toBe("Rename feature");
      core.beginTransaction(session, "Extrude tool");
      core.applyEdits(session, { op: "setParameter", name: "height", value: 7 });
      core.applyEdits(session, { op: "setParameter", name: "width", value: 9 });
      expect(core.sessionState(session).inTransaction).toBe(true);
      core.endTransaction(session, true);
      expect(core.sessionState(session).undoName).toBe("Extrude tool");
      expect(core.undo(session)).toBe(true);
      expect(core.sessionState(session).parameters.map((p) => p.value)).toEqual([10, 6, 4, 1.5]);
      expect(core.sessionState(session).redoName).toBe("Extrude tool");
      core.releaseSession(session);
    });

    it("cancels from the progress callback", () => {
      const session = core.openSession(example);
      const calls: number[] = [];
      // Stops once the sketch is done; it reports progress (done = 0) while it finds its
      // profiles.
      core.regenerate(session, (done, total) => {
        calls.push(done);
        expect(total).toBe(3);
        return done < 1;
      });
      expect(calls.at(-1)).toBe(1);
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
