// Runs the real WebAssembly core in Node. Build it first:
//   cmake --preset wasm-release && cmake --build --preset wasm-release --target facetry-core-web
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import { BooleanOp, CoreError, FacetryCore, LogLevel, Status } from "./api";

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
});
