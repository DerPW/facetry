import { describe, expect, it } from "vitest";

import {
  DEFAULT_LAYOUT,
  clampToWindow,
  dockAt,
  dropInDock,
  float,
  parseLayout,
  placeOf,
  toggleCollapsed,
} from "./panelLayout";

describe("panel layout", () => {
  it("docks near the sides and floats in between", () => {
    expect(dockAt(20, 1000)).toBe("left");
    expect(dockAt(990, 1000)).toBe("right");
    expect(dockAt(500, 1000)).toBeNull();
  });

  it("docks a panel between the items where it is dropped", () => {
    const left = [
      { id: "header", top: 0, bottom: 100 },
      { id: "parameters", top: 110, bottom: 300 },
    ];
    const layout = dropInDock(DEFAULT_LAYOUT, "left", left, "toolbar", 120);
    expect(placeOf(layout, "header").order).toBe(1);
    expect(placeOf(layout, "toolbar")).toMatchObject({ dock: "left", order: 2 });
    expect(placeOf(layout, "parameters").order).toBe(3);
  });

  it("keeps the right dock's ViewCube at 0, with panels above and below it", () => {
    const right = [
      { id: "toolbar", top: 0, bottom: 40 },
      { id: null, top: 50, bottom: 190 },
    ];
    const above = dropInDock(DEFAULT_LAYOUT, "right", right, "browser", 10);
    expect(placeOf(above, "browser")).toMatchObject({ dock: "right", order: -2 });
    expect(placeOf(above, "toolbar").order).toBe(-1);
    const below = dropInDock(DEFAULT_LAYOUT, "right", right, "browser", 400);
    expect(placeOf(below, "browser").order).toBe(1);
    expect(placeOf(below, "toolbar").order).toBe(-1);
  });

  it("floats, collapses and keeps floating panels in the window", () => {
    const floating = float(DEFAULT_LAYOUT, "browser", 300, 200);
    expect(placeOf(floating, "browser")).toMatchObject({ dock: "float", x: 300, y: 200 });
    expect(placeOf(toggleCollapsed(floating, "browser"), "browser").collapsed).toBe(true);
    expect(clampToWindow(2000, -10, 1000, 800)).toEqual([920, 0]);
  });

  it("reads a stored layout over the defaults and skips what it cannot read", () => {
    const stored = JSON.stringify({
      browser: { dock: "float", order: 0, collapsed: true, x: 10, y: 20 },
      parameters: { dock: "sideways" },
    });
    const layout = parseLayout(stored);
    expect(placeOf(layout, "browser")).toMatchObject({ dock: "float", collapsed: true });
    expect(placeOf(layout, "parameters")).toEqual(DEFAULT_LAYOUT.parameters);
    expect(parseLayout("not json")).toEqual(DEFAULT_LAYOUT);
  });
});
