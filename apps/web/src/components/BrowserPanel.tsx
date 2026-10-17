import { useState } from "react";

import type { Edit, SessionState } from "../core/api";

interface Props {
  state: SessionState;
  onEdit: (edits: Edit | Edit[]) => void;
  /** Opens a feature's details in the timeline. */
  onSelectFeature: (feature: string) => void;
}

/** The browser: the document as a tree of origin, bodies and sketches (Fusion's browser). */

/** Opacities offered for a body; other values from a file show as they are. */
const OPACITIES = [1, 0.75, 0.5, 0.25];
export function BrowserPanel({ state, onEdit, onSelectFeature }: Props) {
  const [renaming, setRenaming] = useState<string | null>(null);
  const sketches = state.features.filter((f) => f.type === "sketch");

  const eye = (visible: boolean, label: string, toggle: () => void) => (
    <button
      type="button"
      className={`eye${visible ? "" : " hidden"}`}
      aria-label={`${visible ? "Hide" : "Show"} ${label}`}
      title={visible ? "Hide" : "Show"}
      onClick={toggle}
    >
      {visible ? "●" : "○"}
    </button>
  );

  return (
    <section className="panel browser" aria-label="Browser">
      <details>
        <summary>Origin</summary>
        <ul>
          {state.origin.map((item) => (
            <li key={item.id}>
              {eye(item.visible, item.name, () =>
                onEdit({ op: "setOriginVisible", id: item.id, visible: !item.visible }),
              )}
              <span className="muted">{item.name}</span>
            </li>
          ))}
        </ul>
      </details>
      <details open>
        <summary>Bodies</summary>
        <ul>
          {state.bodies.map((body) => (
            <li key={body.id}>
              {eye(body.visible, body.name, () =>
                onEdit({ op: "setBodyVisible", id: body.id, visible: !body.visible }),
              )}
              {renaming === body.id ? (
                <input
                  aria-label="Body name"
                  defaultValue={body.name}
                  autoFocus
                  onFocus={(event) => event.currentTarget.select()}
                  onKeyDown={(event) => {
                    if (event.key === "Enter") event.currentTarget.blur();
                    if (event.key === "Escape") setRenaming(null);
                  }}
                  onBlur={(event) => {
                    const name = event.currentTarget.value.trim();
                    setRenaming(null);
                    if (name && name !== body.name) onEdit({ op: "renameBody", id: body.id, name });
                  }}
                />
              ) : (
                <button
                  type="button"
                  className="item"
                  title="Double-click to rename"
                  onClick={() => onSelectFeature(body.id)}
                  onDoubleClick={() => setRenaming(body.id)}
                >
                  {body.name}
                </button>
              )}
              <button
                type="button"
                className="small"
                title="Show only this body"
                onClick={() =>
                  onEdit(
                    state.bodies.map((other) => ({
                      op: "setBodyVisible" as const,
                      id: other.id,
                      visible: other.id === body.id,
                    })),
                  )
                }
              >
                Isolate
              </button>
              <select
                className="small"
                aria-label={`Opacity of ${body.name}`}
                title="Opacity"
                value={body.opacity}
                onChange={(event) =>
                  onEdit({
                    op: "setBodyOpacity",
                    id: body.id,
                    opacity: Number(event.currentTarget.value),
                  })
                }
              >
                {OPACITIES.includes(body.opacity) ? null : (
                  <option value={body.opacity}>{Math.round(body.opacity * 100)} %</option>
                )}
                {OPACITIES.map((opacity) => (
                  <option key={opacity} value={opacity}>
                    {opacity * 100} %
                  </option>
                ))}
              </select>
            </li>
          ))}
          {state.bodies.length === 0 && <li className="muted">No bodies</li>}
        </ul>
      </details>
      <details open>
        <summary>Sketches</summary>
        <ul>
          {sketches.map((sketch) => (
            <li key={sketch.id}>
              <button type="button" className="item" onClick={() => onSelectFeature(sketch.id)}>
                {sketch.name}
              </button>
            </li>
          ))}
          {sketches.length === 0 && <li className="muted">No sketches</li>}
        </ul>
      </details>
    </section>
  );
}
