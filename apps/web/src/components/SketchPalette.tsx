import type { ReactNode } from "react";

import { SketchTool } from "../core/api";

/** Outline icons on a 24 × 24 grid, drawn with the current color. */
const ICONS: Record<string, ReactNode> = {
  select: <path d="M6 3v16l4-4 3 6 2-1-3-6h6z" />,
  line: (
    <>
      <path d="M5 19 19 5" />
      <circle cx="5" cy="19" r="1.6" />
      <circle cx="19" cy="5" r="1.6" />
    </>
  ),
  rectangle: <rect x="4" y="6" width="16" height="12" />,
  rectangle3: <path d="M3 13 12 4l9 7-9 9z" />,
  rectangleCenter: (
    <>
      <rect x="4" y="6" width="16" height="12" />
      <circle cx="12" cy="12" r="1.4" />
    </>
  ),
  circleCenter: (
    <>
      <circle cx="12" cy="12" r="8" />
      <circle cx="12" cy="12" r="1.4" />
    </>
  ),
  circle2: (
    <>
      <circle cx="12" cy="12" r="8" />
      <path d="M4 12h16" />
    </>
  ),
  circle3: (
    <>
      <circle cx="12" cy="12" r="8" />
      <circle cx="12" cy="4" r="1.6" />
      <circle cx="5" cy="16" r="1.6" />
      <circle cx="19" cy="16" r="1.6" />
    </>
  ),
  arc3: (
    <>
      <path d="M4 18a9 9 0 0 1 16 0" />
      <circle cx="4" cy="18" r="1.6" />
      <circle cx="12" cy="9.5" r="1.6" />
      <circle cx="20" cy="18" r="1.6" />
    </>
  ),
  arcCenter: (
    <>
      <path d="M4 18a8 8 0 0 1 16 0" />
      <path d="M12 18h8" />
      <circle cx="12" cy="18" r="1.4" />
    </>
  ),
  arcTangent: <path d="M3 18h7a7 7 0 0 0 7-7V5" />,
  point: (
    <>
      <circle cx="12" cy="12" r="2.2" />
      <path d="M12 4v4M12 16v4M4 12h4M16 12h4" />
    </>
  ),
  trim: <path d="M3 12h6M15 12h6M9 7l6 10M15 7 9 17" />,
  extend: <path d="M3 12h13M13 9l3 3-3 3M20 4v16" />,
  fillet: <path d="M5 20v-8a7 7 0 0 1 7-7h8" />,
  move: (
    <path d="M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3" />
  ),
  copy: (
    <>
      <rect x="4" y="8" width="11" height="11" />
      <path d="M9 8V4h11v11h-5" />
    </>
  ),
  project: <path d="M12 3v11M8 10l4 4 4-4M4 19h16" />,
  construction: <path d="M4 20 20 4" strokeDasharray="3 3" />,
  grid: <path d="M4 4h16v16H4zM4 10h16M4 15h16M10 4v16M15 4v16" />,
  delete: <path d="M5 7h14M10 7V4h4v3M7 7l1 13h8l1-13" />,
  finish: <path d="m5 12 5 5 9-10" />,
  cancel: <path d="M6 6l12 12M18 6 6 18" />,
};

function Icon({ name }: { name: string }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width="20"
      height="20"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      {ICONS[name]}
    </svg>
  );
}

interface ToolButton {
  tool: SketchTool | null;
  label: string;
  icon: string;
  key?: string;
}

/** The tools, by group, with their keys (docs/INPUT.md). */
const DRAW: ToolButton[] = [
  { tool: null, label: "Select", icon: "select" },
  { tool: SketchTool.Line, label: "Line", icon: "line", key: "l" },
  {
    tool: SketchTool.RectangleTwoPoint,
    label: "Rectangle: 2 corners",
    icon: "rectangle",
    key: "r",
  },
  { tool: SketchTool.RectangleThreePoint, label: "Rectangle: 3 points", icon: "rectangle3" },
  { tool: SketchTool.RectangleCenter, label: "Rectangle: center", icon: "rectangleCenter" },
  { tool: SketchTool.CircleCenter, label: "Circle: center", icon: "circleCenter", key: "c" },
  { tool: SketchTool.CircleTwoPoint, label: "Circle: 2 points", icon: "circle2" },
  { tool: SketchTool.CircleThreePoint, label: "Circle: 3 points", icon: "circle3" },
  { tool: SketchTool.ArcThreePoint, label: "Arc: 3 points", icon: "arc3", key: "a" },
  { tool: SketchTool.ArcCenter, label: "Arc: center", icon: "arcCenter" },
  { tool: SketchTool.ArcTangent, label: "Arc: tangent", icon: "arcTangent" },
  { tool: SketchTool.Point, label: "Point", icon: "point", key: "p" },
];
const EDIT: ToolButton[] = [
  { tool: SketchTool.Trim, label: "Trim", icon: "trim", key: "t" },
  { tool: SketchTool.Extend, label: "Extend", icon: "extend", key: "e" },
  { tool: SketchTool.Fillet, label: "Fillet", icon: "fillet" },
  { tool: SketchTool.Move, label: "Move", icon: "move", key: "m" },
  { tool: SketchTool.Copy, label: "Copy", icon: "copy" },
];

/** The tools with a key, for the keyboard shortcuts. */
export const SKETCH_TOOL_KEYS = [...DRAW, ...EDIT].flatMap(({ tool, key }) =>
  tool !== null && key ? [{ tool, key }] : [],
);

export interface SketchPaletteProps {
  name: string;
  tool: SketchTool | null;
  onTool: (tool: SketchTool | null) => void;
  construction: boolean;
  onConstruction: () => void;
  gridSnap: boolean;
  onGridSnap: () => void;
  canDelete: boolean;
  onDelete: () => void;
  onProject: () => void;
  onFinish: (keep: boolean) => void;
}

/** The sketch tools and actions as a vertical palette of icons (#62), in the "sketch" panel. */
export function SketchPalette(props: SketchPaletteProps) {
  const toolButton = ({ tool, label, icon, key }: ToolButton) => (
    <button
      key={label}
      type="button"
      aria-label={label}
      aria-pressed={props.tool === tool}
      title={key ? `${label} (${key.toUpperCase()})` : label}
      onClick={() => props.onTool(tool)}
    >
      <Icon name={icon} />
    </button>
  );
  const action = (
    label: string,
    icon: string,
    onClick: () => void,
    options: { pressed?: boolean; disabled?: boolean } = {},
  ) => (
    <button
      type="button"
      aria-label={label}
      title={label}
      aria-pressed={options.pressed}
      disabled={options.disabled}
      onClick={onClick}
    >
      <Icon name={icon} />
    </button>
  );
  return (
    <div className="sketch-palette" role="toolbar" aria-label={`Sketch tools: ${props.name}`}>
      <div className="sketch-palette-group">{DRAW.map(toolButton)}</div>
      <div className="sketch-palette-group">{EDIT.map(toolButton)}</div>
      <div className="sketch-palette-group">
        {action(
          "Project the selected faces and edges; without a selection, the origin and axes",
          "project",
          props.onProject,
        )}
        {action(
          "Construction curves (X): new ones, or the selected ones",
          "construction",
          props.onConstruction,
          {
            pressed: props.construction,
          },
        )}
        {action("Snap free points to the grid", "grid", props.onGridSnap, {
          pressed: props.gridSnap,
        })}
        {action("Delete the selected points and curves (Delete)", "delete", props.onDelete, {
          disabled: !props.canDelete,
        })}
      </div>
      <div className="sketch-palette-group">
        {action("Finish sketch", "finish", () => props.onFinish(true))}
        {action("Cancel the sketch", "cancel", () => props.onFinish(false))}
      </div>
    </div>
  );
}
