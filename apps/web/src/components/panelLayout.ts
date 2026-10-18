/**
 * Where the panels are (#64): docked to the left or the right side of the window, in order, or
 * floating like a small window. Collapsed panels show only their title. The arrangement belongs
 * to the device, so it is kept in local storage. The Apple app follows the same rules
 * (PanelLayout.swift).
 */

export type Dock = "left" | "right" | "float";

export interface PanelPlace {
  dock: Dock;
  /**
   * Position in the dock, smallest first. The right dock holds the ViewCube at 0: panels with a
   * negative order sit above it, the others below.
   */
  order: number;
  collapsed: boolean;
  /** Top-left corner while floating, in CSS pixels. */
  x: number;
  y: number;
}

export type PanelLayout = Record<string, PanelPlace>;

const docked = (dock: Dock, order: number): PanelPlace => ({
  dock,
  order,
  collapsed: false,
  x: 0,
  y: 0,
});

/** Where each panel starts, and goes back to when the layout is reset. */
export const DEFAULT_LAYOUT: PanelLayout = {
  header: docked("left", 1),
  recovery: docked("left", 2),
  parameters: docked("left", 3),
  browser: docked("left", 4),
  toolbar: docked("right", -1),
  sketch: docked("right", 1),
};

/** A panel dropped this close to a side of the window docks there, in CSS pixels. */
export const DOCK_REACH = 56;

export const STORAGE_KEY = "facetry.panels";

/** Where a panel is; panels the layout does not know yet go to the end of the left dock. */
export function placeOf(layout: PanelLayout, id: string): PanelPlace {
  return layout[id] ?? DEFAULT_LAYOUT[id] ?? docked("left", 100);
}

/** The dock a panel dropped with the pointer at `x` goes to, or null to float. */
export function dockAt(x: number, windowWidth: number): Dock | null {
  if (x <= DOCK_REACH) return "left";
  if (x >= windowWidth - DOCK_REACH) return "right";
  return null;
}

/** An item in a dock as it stands on the screen: a panel, or the dock's fixed item (null). */
export interface DockItem {
  id: string | null;
  top: number;
  bottom: number;
}

/**
 * Docks panel `id` where the pointer dropped it, at height `y`, between the items already in
 * the dock (top to bottom), and numbers the dock's panels again.
 */
export function dropInDock(
  layout: PanelLayout,
  dock: "left" | "right",
  items: readonly DockItem[],
  id: string,
  y: number,
): PanelLayout {
  const order = items.filter((item) => item.id !== id);
  const before = order.findIndex((item) => y < (item.top + item.bottom) / 2);
  order.splice(before < 0 ? order.length : before, 0, { id, top: y, bottom: y });
  // Around the fixed item, which stays at 0; in a dock without one, from 1.
  const fixed = order.findIndex((item) => item.id === null);
  const next: PanelLayout = { ...layout };
  order.forEach((item, index) => {
    if (item.id === null) return;
    next[item.id] = {
      ...placeOf(layout, item.id),
      dock,
      order: fixed < 0 ? index + 1 : index - fixed,
    };
  });
  return next;
}

/** Floats panel `id` with its top-left corner at (x, y). */
export function float(layout: PanelLayout, id: string, x: number, y: number): PanelLayout {
  return { ...layout, [id]: { ...placeOf(layout, id), dock: "float", x, y } };
}

export function toggleCollapsed(layout: PanelLayout, id: string): PanelLayout {
  const place = placeOf(layout, id);
  return { ...layout, [id]: { ...place, collapsed: !place.collapsed } };
}

/** Keeps at least the start of a floating panel's title inside the window. */
export function clampToWindow(
  x: number,
  y: number,
  windowWidth: number,
  windowHeight: number,
): [number, number] {
  return [
    Math.min(Math.max(x, 0), Math.max(windowWidth - 80, 0)),
    Math.min(Math.max(y, 0), Math.max(windowHeight - 32, 0)),
  ];
}

const isPlace = (value: unknown): value is PanelPlace => {
  if (typeof value !== "object" || value === null) return false;
  const place = value as Record<string, unknown>;
  return (
    (place.dock === "left" || place.dock === "right" || place.dock === "float") &&
    typeof place.collapsed === "boolean" &&
    [place.order, place.x, place.y].every((n) => typeof n === "number" && Number.isFinite(n))
  );
};

/** The stored layout over the defaults; anything unreadable is left out. */
export function parseLayout(text: string | null): PanelLayout {
  const layout: PanelLayout = { ...DEFAULT_LAYOUT };
  if (!text) return layout;
  try {
    const stored: unknown = JSON.parse(text);
    if (typeof stored !== "object" || stored === null) return layout;
    for (const [id, place] of Object.entries(stored)) {
      if (isPlace(place)) layout[id] = place;
    }
  } catch {
    // A broken entry starts over from the defaults.
  }
  return layout;
}
