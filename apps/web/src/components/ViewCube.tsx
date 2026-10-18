import { useEffect, useRef, useState } from "react";

import {
  CUBE_FACES,
  NAMED_VIEWS,
  ViewCubeArrowKind,
  type NamedView,
  type Viewport,
} from "../render/viewport";

/** Side of the cube's square in CSS pixels. */
const SIZE = 132;
/** Pointer movement below which a press on the cube counts as a click. */
const CLICK_SLOP = 4;
const COMPASS_LABELS = ["N", "E", "S", "W"];
/**
 * Face names are laid out in units of 1/40 of the face's center cell, which comes out near a
 * CSS pixel when the face is seen straight on: the font size, and the widest a name gets (the
 * face is 67 units wide).
 */
const LABEL_CELL = 40;
const LABEL_FONT = 12;
const LABEL_WIDTH = 58;

type Direction = readonly [number, number, number];

/** What the pointer is over: a cell (by its direction), an arrow (by index) or the compass. */
type Target =
  { kind: "cell"; direction: Direction } | { kind: "arrow"; index: number } | { kind: "compass" };

const sameTarget = (a: Target | null, b: Target) => JSON.stringify(a) === JSON.stringify(b);

interface Props {
  viewport: Viewport;
}

/**
 * The ViewCube, laid out like Fusion's (#33, #63): clicking a face, edge or corner turns the
 * view there; seen straight on a face, arrows turn it to the next face or roll it; dragging the
 * cube orbits, dragging the compass ring turns around the Z axis only. Home sits at the top
 * left, the named views in the menu at the bottom right. The geometry and the hit tests come
 * from the shared renderer.
 */
export function ViewCube({ viewport }: Props) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const hover = useRef<Target | null>(null);
  const press = useRef<{
    x: number;
    y: number;
    startX: number;
    startY: number;
    target: Target | null;
  } | null>(null);
  const orbiting = useRef(false);
  const [menuOpen, setMenuOpen] = useState(false);
  const menu = useRef<HTMLDivElement>(null);

  // Redraws whenever the camera or the hovered target changes.
  useEffect(() => {
    const element = canvas.current;
    const context = element?.getContext("2d");
    if (!element || !context) return;
    let frame = 0;
    let drawn = "";
    const draw = () => {
      frame = requestAnimationFrame(draw);
      const cells = viewport.viewCubeCells(SIZE);
      const arrows = viewport.viewCubeArrows(SIZE);
      const compass = viewport.viewCubeCompass(SIZE);
      const key = JSON.stringify([
        cells.map((cell) => cell.points),
        arrows.map((arrow) => arrow.center),
        compass.labels,
        hover.current,
      ]);
      if (key === drawn) return;
      drawn = key;

      const ratio = window.devicePixelRatio || 1;
      if (element.width !== SIZE * ratio) {
        element.width = SIZE * ratio;
        element.height = SIZE * ratio;
      }
      const style = getComputedStyle(element);
      const color = (name: string) => style.getPropertyValue(name).trim();
      const hovered = (target: Target) => sameTarget(hover.current, target);
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
      context.clearRect(0, 0, SIZE, SIZE);
      context.lineJoin = "round";
      context.textAlign = "center";
      context.textBaseline = "middle";

      // The compass under the cube.
      if (compass.ring.length > 0) {
        const active = hovered({ kind: "compass" });
        context.beginPath();
        compass.ring.forEach(([x, y], i) =>
          i === 0 ? context.moveTo(x, y) : context.lineTo(x, y),
        );
        context.strokeStyle = active ? color("--accent") : color("--cube-line");
        context.lineWidth = active ? 2.5 : 1.5;
        context.stroke();
        context.font = "600 9px system-ui, sans-serif";
        compass.labels.forEach(([x, y], i) => {
          context.beginPath();
          context.arc(x, y, 6.5, 0, 2 * Math.PI);
          context.fillStyle = color("--cube-face");
          context.fill();
          context.strokeStyle = active ? color("--accent") : color("--cube-line");
          context.lineWidth = 1;
          context.stroke();
          context.fillStyle = i === 0 ? color("--accent") : color("--muted");
          context.fillText(COMPASS_LABELS[i] ?? "", x, y + 0.5);
        });
      }

      for (const cell of cells) {
        context.beginPath();
        cell.points.forEach(([x, y], i) => (i === 0 ? context.moveTo(x, y) : context.lineTo(x, y)));
        context.closePath();
        context.fillStyle = hovered({ kind: "cell", direction: cell.direction })
          ? color("--accent")
          : color("--cube-face");
        context.fill();
        context.strokeStyle = color("--cube-line");
        context.lineWidth = 1;
        context.stroke();
      }
      // Face names lie on the faces and turn with the cube. The cube is drawn without
      // perspective, so the center cell's corners give the face's axes on the screen: from the
      // first corner, the second lies along the face's horizontal and the fourth along its up.
      context.fillStyle = color("--muted");
      context.font = `600 ${LABEL_FONT}px system-ui, sans-serif`;
      for (const cell of cells) {
        if (cell.region !== 0) continue;
        const [a, b, c, d] = cell.points;
        if (!a || !b || !c || !d) continue;
        const [ux, uy] = [(b[0] - a[0]) / LABEL_CELL, (b[1] - a[1]) / LABEL_CELL];
        const [vx, vy] = [(d[0] - a[0]) / LABEL_CELL, (d[1] - a[1]) / LABEL_CELL];
        // Faces seen nearly edge-on stay unlabeled.
        if (Math.abs(ux * vy - uy * vx) < 0.12) continue;
        const name = (CUBE_FACES[cell.face] ?? "").toUpperCase();
        const scale = Math.min(1, LABEL_WIDTH / context.measureText(name).width);
        const cx = (a[0] + b[0] + c[0] + d[0]) / 4;
        const cy = (a[1] + b[1] + c[1] + d[1]) / 4;
        // Text runs along the face's horizontal; its downward is the face's down.
        context.setTransform(
          ratio * ux * scale,
          ratio * uy * scale,
          -ratio * vx * scale,
          -ratio * vy * scale,
          ratio * cx,
          ratio * cy,
        );
        context.fillText(name, 0, 0.5);
      }
      context.setTransform(ratio, 0, 0, ratio, 0, 0);

      // Arrows around the cube: triangles that turn, curved arrows that roll.
      arrows.forEach((arrow, index) => {
        const fill = hovered({ kind: "arrow", index }) ? color("--accent") : color("--muted");
        context.fillStyle = fill;
        context.strokeStyle = fill;
        if (arrow.kind === ViewCubeArrowKind.Turn) {
          context.beginPath();
          arrow.points.forEach(([x, y], i) =>
            i === 0 ? context.moveTo(x, y) : context.lineTo(x, y),
          );
          context.closePath();
          context.fill();
          return;
        }
        drawRoll(context, arrow.center, arrow.kind === ViewCubeArrowKind.RollClockwise);
      });
    };
    frame = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(frame);
  }, [viewport]);

  // The menu closes on a press anywhere else.
  useEffect(() => {
    if (!menuOpen) return;
    const close = (event: PointerEvent) => {
      if (!menu.current?.contains(event.target as Node)) setMenuOpen(false);
    };
    document.addEventListener("pointerdown", close);
    return () => document.removeEventListener("pointerdown", close);
  }, [menuOpen]);

  const local = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  };

  // Arrows lie outside the cube; the compass ring under it.
  const targetAt = (x: number, y: number): Target | null => {
    const arrow = viewport.viewCubeArrowHit(SIZE, x, y);
    if (arrow >= 0) return { kind: "arrow", index: arrow };
    const index = viewport.viewCubeHit(SIZE, x, y);
    const cell = index < 0 ? undefined : viewport.viewCubeCells(SIZE)[index];
    if (cell) return { kind: "cell", direction: cell.direction };
    return viewport.viewCubeCompassHit(SIZE, x, y) ? { kind: "compass" } : null;
  };

  const click = (target: Target | null) => {
    if (target?.kind === "cell") viewport.viewFrom(target.direction);
    if (target?.kind === "arrow") {
      const arrow = viewport.viewCubeArrows(SIZE)[target.index];
      if (arrow) viewport.viewFromLeaning(arrow.direction, arrow.lean);
    }
  };

  return (
    <div className="viewcube">
      <canvas
        ref={canvas}
        style={{ width: SIZE, height: SIZE }}
        aria-label="ViewCube: click a face, edge, corner or arrow to turn the view there"
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId);
          const { x, y } = local(e);
          press.current = { x, y, startX: x, startY: y, target: targetAt(x, y) };
          orbiting.current = false;
        }}
        onPointerMove={(e) => {
          const { x, y } = local(e);
          const pressed = press.current;
          if (!pressed) {
            hover.current = targetAt(x, y);
            return;
          }
          const moved = Math.hypot(x - pressed.startX, y - pressed.startY) > CLICK_SLOP;
          if (moved || orbiting.current) {
            // The compass turns around the Z axis only.
            const dy = pressed.target?.kind === "compass" ? 0 : y - pressed.y;
            viewport.orbitFromCenter(x - pressed.x, dy, !orbiting.current);
            orbiting.current = true;
          }
          pressed.x = x;
          pressed.y = y;
        }}
        onPointerUp={(e) => {
          const pressed = press.current;
          press.current = null;
          if (!pressed || orbiting.current) return;
          click(pressed.target);
          // Touch has no hover; keep no highlight behind.
          if (e.pointerType === "touch") hover.current = null;
        }}
        onPointerCancel={() => {
          press.current = null;
        }}
        onPointerLeave={() => {
          if (!press.current) hover.current = null;
        }}
      />
      <button
        type="button"
        className="viewcube-home"
        aria-label="Home view"
        title="Home view (Home)"
        onClick={() => viewport.home()}
      >
        <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
          <path d="M3 11.5 12 4l9 7.5M5.5 9.5V20h5v-6h3v6h5V9.5" />
        </svg>
      </button>
      <div className="viewcube-menu" ref={menu}>
        <button
          type="button"
          aria-label="View menu"
          aria-haspopup="menu"
          aria-expanded={menuOpen}
          title="Views"
          onClick={() => setMenuOpen((open) => !open)}
        >
          <svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true">
            <path d="m6 9 6 6 6-6" />
          </svg>
        </button>
        {menuOpen && (
          <div className="panel viewcube-menu-list" role="menu">
            {(Object.keys(NAMED_VIEWS) as NamedView[]).map((view) => (
              <button
                key={view}
                type="button"
                role="menuitem"
                onClick={() => {
                  viewport.viewFrom(NAMED_VIEWS[view]);
                  setMenuOpen(false);
                }}
              >
                {view}
              </button>
            ))}
            <hr />
            <button
              type="button"
              role="menuitem"
              title="Shift+F"
              onClick={() => {
                viewport.fitSelection();
                setMenuOpen(false);
              }}
            >
              Fit selection
            </button>
          </div>
        )}
      </div>
    </div>
  );
}

/** A curved arrow around `center` that rolls the view, as on Fusion's ViewCube. */
function drawRoll(
  context: CanvasRenderingContext2D,
  [cx, cy]: [number, number],
  clockwise: boolean,
) {
  const radius = SIZE * 0.042;
  // Three quarters of a circle; the arrowhead at the end points along the turn.
  const start = clockwise ? -0.9 * Math.PI : -0.1 * Math.PI;
  const end = clockwise ? start + 1.5 * Math.PI : start - 1.5 * Math.PI;
  context.beginPath();
  context.arc(cx, cy, radius, start, end, !clockwise);
  context.lineWidth = 2;
  context.stroke();
  const x = cx + radius * Math.cos(end);
  const y = cy + radius * Math.sin(end);
  const sign = clockwise ? 1 : -1;
  const [tx, ty] = [-Math.sin(end) * sign, Math.cos(end) * sign]; // along the turn
  const head = 4.5;
  context.beginPath();
  context.moveTo(x + tx * head, y + ty * head);
  context.lineTo(x - ty * head * 0.8, y + tx * head * 0.8);
  context.lineTo(x + ty * head * 0.8, y - tx * head * 0.8);
  context.closePath();
  context.fill();
}
