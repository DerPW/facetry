import { describe, expect, it } from "vitest";

import { recoverable, tabLock, type AutosaveEntry } from "./autosave";

describe("recoverable", () => {
  const entry = (id: string, savedAt: number): AutosaveEntry => ({
    id,
    name: `${id}.facetry`,
    savedAt,
  });

  it("offers the autosaves of closed tabs, newest first", () => {
    const entries = [entry("a", 1), entry("open", 5), entry("b", 3)];
    const held = new Set([tabLock("open"), "some-other-lock"]);
    expect(recoverable(entries, held).map((e) => e.id)).toEqual(["b", "a"]);
  });

  it("offers nothing when every tab is still open", () => {
    expect(recoverable([entry("a", 1)], new Set([tabLock("a")]))).toEqual([]);
    expect(recoverable([], new Set())).toEqual([]);
  });
});
