"use client";

import { useEffect, useState } from "react";
import { TABLET, areaToCoils, coilsToArea, type Rect, type Values } from "../lib/settings";

type Props = { values: Values; onChange: (patch: Values) => void; disabled?: boolean };

// Limits tracking to a rectangle of the tablet. The tablet still reports absolute positions over its whole surface.
export function AreaEditor({ values, onChange, disabled }: Props) {
  const current = coilsToArea(values);
  const [rect, setRect] = useState<Rect>(current);
  const full = values.X_MIN === 0 && values.X_MAX === TABLET.xCoils - 1 && values.Y_MIN === 0 && values.Y_MAX === TABLET.yCoils - 1;

  // follow changes that come from outside (reading the tablet, presets, reset)
  useEffect(() => {
    setRect(coilsToArea(values));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [values.X_MIN, values.X_MAX, values.Y_MIN, values.Y_MAX, values.FLIP_X, values.FLIP_Y]);

  const apply = (r: Rect | null) => {
    onChange(
      r
        ? areaToCoils(r, !!values.FLIP_X, !!values.FLIP_Y)
        : { X_MIN: 0, X_MAX: TABLET.xCoils - 1, Y_MIN: 0, Y_MAX: TABLET.yCoils - 1 },
    );
  };

  const field = (key: keyof Rect, text: string) => (
    <label className="field">
      <span>{text}</span>
      <input
        className="num"
        type="number"
        step={0.5}
        min={0}
        value={Number(rect[key].toFixed(1))}
        disabled={disabled}
        onChange={(e) => setRect({ ...rect, [key]: Number(e.target.value) })}
      />
      <span className="muted small">mm</span>
    </label>
  );

  const W = 400;
  const scale = W / TABLET.widthMm;
  const H = TABLET.heightMm * scale;

  return (
    <section className="card">
      <h2>Active area</h2>
      <p className="muted small">
        Only coils inside this rectangle are tracked, a pen outside it counts as out of range. Positions are in mm as reported (top left = 0, 0).
        The coil grid is about 6 mm, so the real edges snap to whole coils plus one coil of margin.
      </p>
      <div className="area">
        <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label="Tablet with the active area">
          <rect x={0} y={0} width={W} height={H} rx={6} className="tablet" />
          <rect x={current.x * scale} y={current.y * scale} width={current.w * scale} height={current.h * scale} className="active" />
        </svg>
        <div className="area-fields">
          <div className="grid2">
            {field("x", "Left")}
            {field("y", "Top")}
            {field("w", "Width")}
            {field("h", "Height")}
          </div>
          <div className="btns">
            <button onClick={() => apply(rect)} disabled={disabled}>Set area</button>
            <button className="ghost" onClick={() => apply(null)} disabled={disabled}>Whole tablet</button>
          </div>
          <p className="muted small">
            Now: X coils {values.X_MIN}–{values.X_MAX}, Y coils {values.Y_MIN}–{values.Y_MAX}
            {full ? " (whole tablet)" : ` ≈ ${current.w.toFixed(0)} × ${current.h.toFixed(0)} mm at ${current.x.toFixed(0)}, ${current.y.toFixed(0)}`}
          </p>
        </div>
      </div>
    </section>
  );
}
