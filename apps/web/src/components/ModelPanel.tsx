import type { SessionState } from "../core/api";

interface Props {
  state: SessionState;
  onSetParameter: (name: string, value: number) => void;
}

/** The parameters of the open model. */
export function ModelPanel({ state, onSetParameter }: Props) {
  const commit = (name: string, current: number, input: HTMLInputElement) => {
    const value = input.valueAsNumber;
    if (Number.isFinite(value) && value !== current) onSetParameter(name, value);
    else input.value = String(current);
  };

  return (
    <section className="panel model" aria-label="Parameters">
      <h2>Parameters</h2>
      {state.parameters.map(({ name, value }) => (
        <label key={name} className="parameter">
          <span>{name}</span>
          <input
            // Remounts with the new value when an edit, undo or redo changes it.
            key={value}
            type="number"
            step="any"
            defaultValue={value}
            onBlur={(event) => commit(name, value, event.currentTarget)}
            onKeyDown={(event) => {
              if (event.key === "Enter") commit(name, value, event.currentTarget);
            }}
          />
          <span className="muted">mm</span>
        </label>
      ))}
    </section>
  );
}
