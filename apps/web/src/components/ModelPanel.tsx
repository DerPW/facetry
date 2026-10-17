import { LENGTH_UNITS, type LengthUnit, type SessionState } from "../core/api";

interface Props {
  state: SessionState;
  onSetParameter: (name: string, value: number) => void;
  onSetUnits: (units: LengthUnit) => void;
}

/**
 * A value as shown: six significant digits, which also hide the noise of converting between
 * units (2.54 mm is 0.1 in). An unchanged field compares equal, so it changes nothing.
 */
const shown = (value: number) => Number(value.toPrecision(6));

/** The parameters of the open model, in the document's units. */
export function ModelPanel({ state, onSetParameter, onSetUnits }: Props) {
  const commit = (name: string, current: number, input: HTMLInputElement) => {
    const value = input.valueAsNumber;
    if (Number.isFinite(value) && value !== shown(current)) onSetParameter(name, value);
    else input.value = String(shown(current));
  };

  return (
    <section className="panel model" aria-label="Parameters">
      <div className="model-header">
        <h2>Parameters</h2>
        <label>
          Units{" "}
          <select
            value={state.units}
            // Converts every length; the model keeps its size.
            onChange={(event) => onSetUnits(event.currentTarget.value as LengthUnit)}
          >
            {LENGTH_UNITS.map((unit) => (
              <option key={unit} value={unit}>
                {unit}
              </option>
            ))}
          </select>
        </label>
      </div>
      {state.parameters.map(({ name, value }) => (
        <label key={name} className="parameter">
          <span>{name}</span>
          <input
            // Remounts with the new value when an edit, undo or redo changes it.
            key={value}
            type="number"
            step="any"
            defaultValue={shown(value)}
            onBlur={(event) => commit(name, value, event.currentTarget)}
            onKeyDown={(event) => {
              if (event.key === "Enter") commit(name, value, event.currentTarget);
            }}
          />
          <span className="muted">{state.units}</span>
        </label>
      ))}
    </section>
  );
}
