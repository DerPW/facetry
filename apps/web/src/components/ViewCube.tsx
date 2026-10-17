import { useEffect, useRef } from "react";

import { CUBE_FACES, NAMED_VIEWS, type NamedView, type Viewport } from "../render/viewport";

/** Side of the cube's square in CSS pixels. */
const SIZE = 120;
/** Pointer movement below which a press on the cube counts as a click. */
const CLICK_SLOP = 4;

type Direction = readonly [number, number, number];

const sameDirection = (a: Direction | null, b: Direction) =>
  a !== null && a[0] === b[0] && a[1] === b[1] && a[2] === b[2];

interface Props {
  viewport: Viewport;
}

/**
 * The ViewCube (#33): shows the camera's orientation. Clicking a face, edge or corner turns the
 * view there; dragging it orbits. The cells and the hit test come from the shared renderer.
 */
export function ViewCube({ viewport }: Props) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const hover = useRef<Direction | null>(null);
  const press = useRef<{ x: number; y: number; startX: number; startY: number } | null>(null);
  const orbiting = useRef(false);

  // Redraws whenever the camera or the hovered cell changes.
  useEffect(() => {
    const element = canvas.current;
    const context = element?.getContext("2d");
    if (!element || !context) return;
    let frame = 0;
    let drawn = "";
    const draw = () => {
      frame = requestAnimationFrame(draw);
      const cells = viewport.viewCubeCells(SIZE);
      const key = JSON.stringify([cells.map((cell) => cell.points), hover.current]);
      if (key === drawn) return;
      drawn = key;

      const ratio = window.devicePixelRatio || 1;
      if (element.width !== SIZE * ratio) {
        element.width = SIZE * ratio;
        element.height = SIZE * ratio;
      }
      const style = getComputedStyle(element);
      const color = (name: string) => style.getPropertyValue(name).trim();
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
      context.clearRect(0, 0, SIZE, SIZE);
      context.lineJoin = "round";
      for (const cell of cells) {
        context.beginPath();
        cell.points.forEach(([x, y], i) => (i === 0 ? context.moveTo(x, y) : context.lineTo(x, y)));
        context.closePath();
        context.fillStyle = sameDirection(hover.current, cell.direction)
          ? color("--accent")
          : color("--cube-face");
        context.fill();
        context.strokeStyle = color("--cube-line");
        context.lineWidth = 1;
        context.stroke();
      }
      // Face names on the center cells, sized to the cell; faces seen edge-on stay unlabeled.
      context.fillStyle = color("--muted");
      context.textAlign = "center";
      context.textBaseline = "middle";
      for (const cell of cells) {
        if (cell.region !== 0) continue;
        const [a, b, c, d] = cell.points;
        if (!a || !b || !c || !d) continue;
        const area = Math.abs((c[0] - a[0]) * (d[1] - b[1]) - (d[0] - b[0]) * (c[1] - a[1])) / 2;
        const fontSize = Math.min(13, Math.sqrt(area) * 0.3);
        if (fontSize < 6) continue;
        context.font = `600 ${fontSize.toFixed(1)}px system-ui, sans-serif`;
        const name = CUBE_FACES[cell.face] ?? "";
        context.fillText(
          name.toUpperCase(),
          (a[0] + b[0] + c[0] + d[0]) / 4,
          (a[1] + b[1] + c[1] + d[1]) / 4,
        );
      }
    };
    frame = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(frame);
  }, [viewport]);

  const local = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  };

  const cellAt = (x: number, y: number) => {
    const index = viewport.viewCubeHit(SIZE, x, y);
    return index < 0 ? null : (viewport.viewCubeCells(SIZE)[index] ?? null);
  };

  return (
    <div className="viewcube">
      <canvas
        ref={canvas}
        style={{ width: SIZE, height: SIZE }}
        aria-label="ViewCube: click a face, edge or corner to turn the view there"
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId);
          const { x, y } = local(e);
          press.current = { x, y, startX: x, startY: y };
          orbiting.current = false;
        }}
        onPointerMove={(e) => {
          const { x, y } = local(e);
          const pressed = press.current;
          if (!pressed) {
            hover.current = cellAt(x, y)?.direction ?? null;
            return;
          }
          const moved = Math.hypot(x - pressed.startX, y - pressed.startY) > CLICK_SLOP;
          if (moved || orbiting.current) {
            viewport.orbitFromCenter(x - pressed.x, y - pressed.y, !orbiting.current);
            orbiting.current = true;
          }
          pressed.x = x;
          pressed.y = y;
        }}
        onPointerUp={(e) => {
          const pressed = press.current;
          press.current = null;
          if (!pressed || orbiting.current) return;
          const cell = cellAt(pressed.startX, pressed.startY);
          if (cell) viewport.viewFrom(cell.direction);
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
      <div className="viewcube-actions">
        <button type="button" title="Home view (Home)" onClick={() => viewport.home()}>
          Home
        </button>
        <select
          aria-label="Named view"
          value=""
          onChange={(event) => {
            const view = event.currentTarget.value as NamedView;
            if (view in NAMED_VIEWS) viewport.viewFrom(NAMED_VIEWS[view]);
          }}
        >
          <option value="" disabled>
            View…
          </option>
          {(Object.keys(NAMED_VIEWS) as NamedView[]).map((view) => (
            <option key={view} value={view}>
              {view}
            </option>
          ))}
        </select>
        <button
          type="button"
          title="Fit the selection (Shift+F)"
          onClick={() => viewport.fitSelection()}
        >
          Fit sel.
        </button>
      </div>
    </div>
  );
}
