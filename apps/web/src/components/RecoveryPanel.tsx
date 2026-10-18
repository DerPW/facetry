import type { AutosaveEntry } from "../core/autosave";

interface Props {
  entries: AutosaveEntry[];
  onOpen: (entry: AutosaveEntry) => void;
  onDiscard: (entry: AutosaveEntry) => void;
}

/** Autosaves of tabs that closed before their work was saved (#17). */
export function RecoveryPanel({ entries, onOpen, onDiscard }: Props) {
  return (
    <div className="recovery">
      <p className="muted">Facetry closed before this was saved.</p>
      <ul>
        {entries.map((entry) => (
          <li key={entry.id}>
            <span className="name">
              {entry.name}
              <span className="muted"> · {new Date(entry.savedAt).toLocaleString()}</span>
            </span>
            <button type="button" onClick={() => onOpen(entry)}>
              Open
            </button>
            <button type="button" onClick={() => onDiscard(entry)}>
              Discard
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}
