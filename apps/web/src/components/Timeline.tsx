import { useRef, useState } from "react";

import type { Edit, FeatureJson, Reference, SessionState } from "../core/api";

type Feature = SessionState["features"][number];

const CHIP = 124; // chip width plus gap, in CSS pixels
const typeIcon: Record<string, string> = { sketch: "✎", extrude: "⬆", fillet: "◜" };
const statusLabel: Record<string, string> = {
  ok: "OK",
  warning: "Warning",
  error: "Error",
  suppressed: "Suppressed",
  rolledBack: "After the rollback marker",
  notRun: "Not computed yet",
};

/** A broken reference waiting for the user to pick a replacement in the viewport. */
export interface Repair {
  feature: string;
  index: number;
}

interface Props {
  state: SessionState;
  onEdit: (edits: Edit | Edit[]) => void;
  /** The feature whose details are open, e.g. picked in the browser panel. */
  selected: string | null;
  onSelect: (feature: string | null) => void;
  repair: Repair | null;
  onRepair: (repair: Repair | null) => void;
  /** The extrusion whose profiles are being chosen in the viewport. */
  profiles: string | null;
  onChooseProfiles: (feature: string | null) => void;
  /** Takes the profiles selected in the viewport. */
  onFinishProfiles: () => void;
  /** Opens a sketch for editing (sketch mode). */
  onEditSketch: (sketch: string) => void;
}

/** The timeline bar (Fusion-style): one chip per feature, the rollback marker, feature details. */
export function Timeline({
  state,
  onEdit,
  selected,
  onSelect,
  repair,
  onRepair,
  profiles,
  onChooseProfiles,
  onFinishProfiles,
  onEditSketch,
}: Props) {
  const setSelected = onSelect;
  const [dragIndex, setDragIndex] = useState<number | null>(null);
  const row = useRef<HTMLOListElement>(null);
  const features = state.features;
  const marker = dragIndex ?? state.rollback ?? features.length;
  const feature = features.find((f) => f.id === selected);

  const indexAt = (clientX: number) => {
    const left = row.current?.getBoundingClientRect().left ?? 0;
    return Math.max(0, Math.min(features.length, Math.round((clientX - left) / CHIP)));
  };

  return (
    <div className="timeline-area">
      {feature && (
        <FeatureDetails
          key={feature.id}
          feature={feature}
          index={features.indexOf(feature)}
          nameOf={(id) => features.find((f) => f.id === id)?.name ?? id}
          onEdit={onEdit}
          repair={repair}
          onRepair={onRepair}
          choosingProfiles={profiles === feature.id}
          onChooseProfiles={onChooseProfiles}
          onFinishProfiles={onFinishProfiles}
          onEditSketch={onEditSketch}
          onClose={() => {
            if (profiles === feature.id) onChooseProfiles(null);
            setSelected(null);
          }}
        />
      )}
      <nav className="panel timeline-bar" aria-label="Timeline">
        <ol ref={row} className="chips">
          {features.map((f, i) => (
            <li key={f.id}>
              <button
                type="button"
                className={`chip ${f.status}${f.id === selected ? " selected" : ""}${i >= marker ? " after-marker" : ""}`}
                title={`${f.name}: ${statusLabel[f.status] ?? f.status}`}
                onClick={() => setSelected(f.id === selected ? null : f.id)}
              >
                <span className="icon">{typeIcon[f.type] ?? "?"}</span> {f.name}
                {(f.status === "error" || f.status === "warning") && (
                  <span className="badge">{f.status === "error" ? "!" : "⚠"}</span>
                )}
              </button>
            </li>
          ))}
          <li
            className="marker"
            style={{ left: marker * CHIP - 4 }}
            role="slider"
            aria-label="Rollback marker"
            aria-valuemin={0}
            aria-valuemax={features.length}
            aria-valuenow={marker}
            tabIndex={0}
            onPointerDown={(event) => {
              event.currentTarget.setPointerCapture(event.pointerId);
              setDragIndex(indexAt(event.clientX));
            }}
            onPointerMove={(event) => {
              if (dragIndex !== null) setDragIndex(indexAt(event.clientX));
            }}
            onPointerUp={() => {
              if (dragIndex === null) return;
              const index = dragIndex === features.length ? null : dragIndex;
              setDragIndex(null);
              if (index !== state.rollback) onEdit({ op: "setRollback", index });
            }}
            onKeyDown={(event) => {
              const step = event.key === "ArrowLeft" ? -1 : event.key === "ArrowRight" ? 1 : 0;
              if (step === 0) return;
              const index = Math.max(0, Math.min(features.length, marker + step));
              onEdit({ op: "setRollback", index: index === features.length ? null : index });
            }}
          />
        </ol>
      </nav>
    </div>
  );
}

interface DetailsProps {
  feature: Feature;
  index: number;
  nameOf: (id: string) => string;
  onEdit: (edits: Edit | Edit[]) => void;
  repair: Repair | null;
  onRepair: (repair: Repair | null) => void;
  choosingProfiles: boolean;
  onChooseProfiles: (feature: string | null) => void;
  onFinishProfiles: () => void;
  onEditSketch: (sketch: string) => void;
  onClose: () => void;
}

/** The numbers a feature type exposes for editing (sketch geometry is edited with M1.3). */
const valueFields: Record<string, string[]> = { extrude: ["distance"], fillet: ["radius"] };

function FeatureDetails({
  feature,
  index,
  nameOf,
  onEdit,
  repair,
  onRepair,
  choosingProfiles,
  onChooseProfiles,
  onFinishProfiles,
  onEditSketch,
  onClose,
}: DetailsProps) {
  const definition = feature.definition;
  const replace = (changes: Partial<FeatureJson>) =>
    onEdit({ op: "replaceFeature", feature: { ...definition, ...changes } });
  const edges = (definition.edges as Reference[] | undefined) ?? [];
  const chosenProfiles = (definition.regions as string[] | undefined) ?? [];

  return (
    <section className="panel feature-details" aria-label={`Feature ${feature.name}`}>
      <header>
        <input
          aria-label="Name"
          defaultValue={feature.name}
          onKeyDown={(event) => {
            if (event.key === "Enter") event.currentTarget.blur();
          }}
          onBlur={(event) => {
            const name = event.currentTarget.value.trim();
            if (name && name !== feature.name)
              onEdit({ op: "renameFeature", id: feature.id, name });
          }}
        />
        <button type="button" aria-label="Close" onClick={onClose}>
          ✕
        </button>
      </header>
      <p className={`status ${feature.status}`}>{statusLabel[feature.status] ?? feature.status}</p>
      {feature.messages.map((message) => (
        <p key={message} className="muted">
          {message}
        </p>
      ))}
      {(valueFields[feature.type] ?? []).map((field) => (
        <label key={field} className="parameter">
          <span>{field}</span>
          <input
            key={String(definition[field])}
            defaultValue={String(definition[field] ?? "")}
            title="A number, or the name of a parameter"
            onKeyDown={(event) => {
              if (event.key === "Enter") event.currentTarget.blur();
            }}
            onBlur={(event) => {
              const text = event.currentTarget.value.trim();
              const value = text !== "" && Number.isFinite(Number(text)) ? Number(text) : text;
              if (text && value !== definition[field]) replace({ [field]: value });
            }}
          />
        </label>
      ))}
      {feature.type === "sketch" && (
        <div className="profiles">
          <p className="muted">
            {feature.regions?.length
              ? `${feature.regions.length} ${feature.regions.length === 1 ? "profile" : "profiles"}`
              : "No closed profile"}
          </p>
          <button type="button" onClick={() => onEditSketch(feature.id)}>
            Edit sketch
          </button>
        </div>
      )}
      {feature.type === "extrude" &&
        (choosingProfiles ? (
          <div className="profiles">
            <p className="muted">Click profiles in the view to add or leave them out.</p>
            <button type="button" onClick={onFinishProfiles}>
              Done
            </button>
            <button type="button" onClick={() => onChooseProfiles(null)}>
              Cancel
            </button>
          </div>
        ) : (
          <div className="profiles">
            <p className="muted">
              {chosenProfiles.length === 0
                ? "Profile: the sketch's only one"
                : `${chosenProfiles.length} ${chosenProfiles.length === 1 ? "profile" : "profiles"}`}
            </p>
            <button type="button" onClick={() => onChooseProfiles(feature.id)}>
              Choose profiles
            </button>
          </div>
        ))}
      {/* Fillets repair their edges here; sketches report lost references in their messages. */}
      {(feature.type === "fillet" ? feature.brokenReferences : []).map((i) => {
        const shadow = edges[i]?.shadow;
        const place = shadow
          ? `a ${shadow.type} at (${shadow.centroid.map((v) => +v.toFixed(2)).join(", ")})`
          : "unknown";
        const active = repair?.feature === feature.id && repair.index === i;
        return (
          <div key={i} className="broken">
            <p>
              Edge {i + 1} is missing; it was {place}.
            </p>
            <button
              type="button"
              onClick={() => onRepair(active ? null : { feature: feature.id, index: i })}
            >
              {active ? "Cancel" : "Pick replacement"}
            </button>
          </div>
        );
      })}
      <div className="actions">
        <button
          type="button"
          onClick={() =>
            onEdit({ op: "suppressFeature", id: feature.id, suppressed: !feature.suppressed })
          }
        >
          {feature.suppressed ? "Unsuppress" : "Suppress"}
        </button>
        <button type="button" onClick={() => onEdit({ op: "setRollback", index: index + 1 })}>
          Roll back to here
        </button>
        <button
          type="button"
          className="danger"
          onClick={() => {
            const names = feature.dependents.map(nameOf).join(", ");
            const question = feature.dependents.length
              ? `Delete "${feature.name}"? These features use it and will fail: ${names}.`
              : `Delete "${feature.name}"?`;
            if (window.confirm(question)) {
              onEdit({ op: "deleteFeature", id: feature.id });
              onClose();
            }
          }}
        >
          Delete
        </button>
      </div>
    </section>
  );
}
