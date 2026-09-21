"use client";

import { useEffect, useState } from "react";
import { TABLET, areaFromValues, areaToValues, type Rect, type Values } from "../lib/settings";

type Props = { values: Values; onChange: (patch: Values) => void; disabled?: boolean };

// Limits the pen to a rectangle of the tablet: outside it the tablet reports the pen as out of range.
// Positions still are absolute over the whole tablet.
export function AreaEditor({ values, onChange, disabled }: Props) {
  const current = areaFromValues(values);
  const [rect, setRect] = useState<Rect>(current);
  const full = values.AREA_X0 === 0 && values.AREA_X1 === TABLET.xMax && values.AREA_Y0 === 0 && values.AREA_Y1 === TABLET.yMax;

  // follow changes that come from outside (reading the tablet, presets, reset)
  useEffect(() => {
    setRect(areaFromValues(values));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [values.AREA_X0, values.AREA_X1, values.AREA_Y0, values.AREA_Y1]);

  const apply = (r: Rect | null) => {
    onChange(r ? areaToValues(r) : { AREA_X0: 0, AREA_X1: TABLET.xMax, AREA_Y0: 0, AREA_Y1: TABLET.yMax });
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
        A pen outside this rectangle counts as out of range, right up to the edge you set. Positions are in mm as the tablet reports them
        (top left = 0, 0).
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
            Now: {full ? "the whole tablet" : `${current.w.toFixed(1)} × ${current.h.toFixed(1)} mm at ${current.x.toFixed(1)}, ${current.y.toFixed(1)}`}
          </p>
        </div>
      </div>
    </section>
  );
}
