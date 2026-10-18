import {
  useCallback,
  useEffect,
  useState,
  type Dispatch,
  type ReactNode,
  type SetStateAction,
} from "react";

import {
  DEFAULT_LAYOUT,
  STORAGE_KEY,
  clampToWindow,
  dockAt,
  dropInDock,
  float,
  parseLayout,
  placeOf,
  toggleCollapsed,
  type Dock,
  type DockItem,
  type PanelLayout,
} from "./panelLayout";

/** Pointer movement below which a press on a title is not a drag. */
const DRAG_SLOP = 4;

export interface PanelSpec {
  id: string;
  title: string;
  content: ReactNode;
}

/** The panel arrangement of this device, kept in local storage. */
export function usePanelLayout() {
  const [layout, setLayout] = useState<PanelLayout>(() => {
    try {
      return parseLayout(localStorage.getItem(STORAGE_KEY));
    } catch {
      return DEFAULT_LAYOUT;
    }
  });
  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(layout));
    } catch {
      // Without storage the arrangement lasts until the page closes.
    }
  }, [layout]);
  const reset = useCallback(() => setLayout(DEFAULT_LAYOUT), []);
  return { layout, setLayout, reset };
}

interface Drag {
  id: string;
  x: number;
  y: number;
  /** The dock the panel would go to if dropped now. */
  target: Dock | null;
}

interface Props {
  layout: PanelLayout;
  onLayout: Dispatch<SetStateAction<PanelLayout>>;
  panels: PanelSpec[];
  /** What the right dock holds at order 0 (the ViewCube). */
  rightFixed: ReactNode;
}

/**
 * The panels, docked to the left or the right side or floating (#64). Dragging a title moves a
 * panel; dropped near a side it docks there, between the panels where it is dropped. The
 * chevron collapses a panel to its title.
 */
export function PanelArea({ layout, onLayout, panels, rightFixed }: Props) {
  const [drag, setDrag] = useState<Drag | null>(null);
  // Floating panels stay in the window when it gets smaller.
  const [, setWindowSize] = useState(0);
  useEffect(() => {
    const resized = () => setWindowSize(window.innerWidth * 10000 + window.innerHeight);
    window.addEventListener("resize", resized);
    return () => window.removeEventListener("resize", resized);
  }, []);

  const inDock = (dock: Dock) =>
    panels
      .filter((panel) => panel.id !== drag?.id && placeOf(layout, panel.id).dock === dock)
      .sort((a, b) => placeOf(layout, a.id).order - placeOf(layout, b.id).order);

  const itemsIn = (dock: "left" | "right"): DockItem[] =>
    Array.from(document.querySelector(`[data-dock="${dock}"]`)?.children ?? []).flatMap(
      (child): DockItem[] => {
        const element = child as HTMLElement;
        const rect = element.getBoundingClientRect();
        if (element.dataset.panel)
          return [{ id: element.dataset.panel, top: rect.top, bottom: rect.bottom }];
        if (element.dataset.dockFixed !== undefined)
          return [{ id: null, top: rect.top, bottom: rect.bottom }];
        return [];
      },
    );

  // The drag follows the pointer on the window: the panel moves out of its dock into the
  // floating layer, so the element first pressed goes away.
  const begin = (e: React.PointerEvent<HTMLElement>, id: string) => {
    if (e.button !== 0 || (e.target as HTMLElement).closest("button")) return;
    const rect = e.currentTarget.parentElement?.getBoundingClientRect();
    const [startX, startY] = [e.clientX, e.clientY];
    const [dx, dy] = [startX - (rect?.left ?? startX), startY - (rect?.top ?? startY)];
    let current: Drag | null = null;
    const move = (event: PointerEvent) => {
      if (!current && Math.hypot(event.clientX - startX, event.clientY - startY) < DRAG_SLOP) {
        return;
      }
      current = {
        id,
        x: event.clientX - dx,
        y: event.clientY - dy,
        target: dockAt(event.clientX, window.innerWidth),
      };
      setDrag(current);
    };
    const end = (event: PointerEvent) => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", end);
      setDrag(null);
      if (!current || event.type === "pointercancel") return;
      const { target } = current;
      if (target === "left" || target === "right") {
        const items = itemsIn(target);
        onLayout((layout) => dropInDock(layout, target, items, id, event.clientY));
      } else {
        const [x, y] = clampToWindow(current.x, current.y, window.innerWidth, window.innerHeight);
        onLayout((layout) => float(layout, id, x, y));
      }
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", end);
  };

  const frame = (panel: PanelSpec, style?: React.CSSProperties) => {
    const place = placeOf(layout, panel.id);
    return (
      <section
        key={panel.id}
        className={`panel panel-frame${drag?.id === panel.id ? " dragging" : ""}`}
        data-panel={panel.id}
        aria-label={panel.title}
        style={style ?? { order: place.order }}
      >
        <div className="panel-title" onPointerDown={(e) => begin(e, panel.id)}>
          <svg className="panel-grip" viewBox="0 0 8 14" width="8" height="14" aria-hidden="true">
            {[2, 7, 12].flatMap((y) => [
              <circle key={`a${y}`} cx="2" cy={y} r="1.2" />,
              <circle key={`b${y}`} cx="6" cy={y} r="1.2" />,
            ])}
          </svg>
          <span className="panel-title-text">{panel.title}</span>
          <button
            type="button"
            className="panel-collapse"
            aria-expanded={!place.collapsed}
            aria-label={place.collapsed ? `Show ${panel.title}` : `Collapse ${panel.title}`}
            title={place.collapsed ? "Show" : "Collapse"}
            onClick={() => onLayout((layout) => toggleCollapsed(layout, panel.id))}
          >
            <svg viewBox="0 0 24 24" width="12" height="12" aria-hidden="true">
              <path d={place.collapsed ? "m9 6 6 6-6 6" : "m6 9 6 6 6-6"} />
            </svg>
          </button>
        </div>
        {!place.collapsed && <div className="panel-body">{panel.content}</div>}
      </section>
    );
  };

  const floating = inDock("float");
  const dragged = drag ? panels.find((panel) => panel.id === drag.id) : undefined;
  return (
    <>
      <div className="dock dock-left" data-dock="left">
        {inDock("left").map((panel) => frame(panel))}
      </div>
      <div className="dock dock-right" data-dock="right">
        {inDock("right").map((panel) => frame(panel))}
        <div className="dock-fixed" data-dock-fixed="" style={{ order: 0 }}>
          {rightFixed}
        </div>
      </div>
      {floating.map((panel) => {
        const place = placeOf(layout, panel.id);
        const [x, y] = clampToWindow(place.x, place.y, window.innerWidth, window.innerHeight);
        return frame(panel, { position: "fixed", left: x, top: y });
      })}
      {dragged && drag && frame(dragged, { position: "fixed", left: drag.x, top: drag.y })}
      {drag?.target && drag.target !== "float" && (
        <div className={`dock-target dock-target-${drag.target}`} aria-hidden="true" />
      )}
    </>
  );
}
