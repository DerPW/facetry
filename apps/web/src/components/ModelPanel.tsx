import type { SessionState } from "../core/api";

const statusSymbol: Record<string, string> = {
  ok: "✓",
  warning: "⚠",
  error: "✕",
  suppressed: "⏸",
  rolledBack: "↶",
  notRun: "○",
};

interface Props {
  state: SessionState;
  onSetParameter: (name: string, value: number) => void;
}

/** Parameters and timeline of the open model. */
export function ModelPanel({ state, onSetParameter }: Props) {
  const commit = (name: string, current: number, input: HTMLInputElement) => {
    const value = input.valueAsNumber;
    if (Number.isFinite(value) && value !== current) onSetParameter(name, value);
    else input.value = String(current);
  };

  return (
    <section className="panel model" aria-label="Model">
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
      <h2>Timeline</h2>
      <ol className="timeline">
        {state.features.map((feature) => (
          <li key={feature.id} className={`feature ${feature.status}`}>
            <span aria-label={feature.status} title={feature.status}>
              {statusSymbol[feature.status] ?? "?"}
            </span>{" "}
            {feature.name}
            {feature.messages.map((message) => (
              <p key={message} className="muted">
                {message}
              </p>
            ))}
          </li>
        ))}
      </ol>
    </section>
  );
}
