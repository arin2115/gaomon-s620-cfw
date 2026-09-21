"use client";

import { TABLE } from "../lib/settings";
import { SCALE, UNIT, label } from "../lib/ui-meta";

type Props = { name: string; value: number; onChange: (v: number) => void; disabled?: boolean };

export function SettingRow({ name, value, onChange, disabled }: Props) {
  const s = TABLE.find((t) => t.name === name);
  if (!s) return null;
  const scale = SCALE[name] ?? 1;
  const isBool = s.min === 0 && s.max === 1;
  const changed = value !== s.def;
  const shown = (v: number) => (scale === 1 ? v : v / scale);
  const parse = (text: string) => {
    const n = Number(text);
    return Number.isFinite(n) ? Math.min(s.max, Math.max(s.min, Math.round(n * scale))) : value;
  };

  return (
    <div className={`row${changed ? " changed" : ""}`}>
      <div className="row-head">
        <label htmlFor={`s-${name}`}>{label(name)}</label>
        <code className="muted" title="Setting name">{name}</code>
        {changed && (
          <button className="link" onClick={() => onChange(s.def)} disabled={disabled} title={`Default: ${shown(s.def)}`}>
            reset to {shown(s.def)}
          </button>
        )}
      </div>
      <p className="muted small">{s.description}</p>
      {isBool ? (
        <label className="switch">
          <input id={`s-${name}`} type="checkbox" checked={value === 1} disabled={disabled} onChange={(e) => onChange(e.target.checked ? 1 : 0)} />
          <span>{value === 1 ? "On" : "Off"}</span>
        </label>
      ) : (
        <div className="slider">
          <input type="range" min={s.min} max={s.max} value={value} disabled={disabled} onChange={(e) => onChange(Number(e.target.value))} aria-label={label(name)} />
          <input
            id={`s-${name}`}
            className="num"
            type="number"
            step={scale === 1 ? 1 : 1 / scale}
            min={shown(s.min)}
            max={shown(s.max)}
            value={shown(value)}
            disabled={disabled}
            onChange={(e) => onChange(parse(e.target.value))}
          />
          <span className="muted small unit">{UNIT[name] ?? ""}</span>
        </div>
      )}
    </div>
  );
}
