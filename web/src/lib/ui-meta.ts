import { TABLE } from "./settings";

export type Group = { id: string; title: string; note?: string; advanced?: boolean; names: string[] };

// Settings that hold a value x1000 are shown divided by scale
export const SCALE: Record<string, number> = { TIP_ON: 1000, TIP_OFF: 1000, F_REST: 1000, F_FULL: 1000 };
export const UNIT: Record<string, string> = {
  GRID_MARGIN: "µs", FREQ_PAUSE_US: "µs", GAIN_SETTLE_US: "µs", SETTLE: "µs", KEY_POLL_MS: "ms",
  MAX_RATE: "Hz",
  HOVER_ZONE_HI: "units", HOVER_ZONE_MID: "units", HOVER_ZONE_LO: "units",
};

export const GROUPS: Group[] = [
  { id: "rate", title: "Report rate", note: "The tablet sends at most this many reports per second to the computer. Lower it if the computer or the game copes badly with 1000 Hz.", names: ["MAX_RATE"] },
  { id: "smoothing", title: "Smoothing", note: "Fewer smoothing means the pen follows faster but shows more jitter.", names: ["SMOOTH_EMA", "SMOOTH_MA", "SMOOTH_FULL_AMP", "SMOOTH_MIN_W"] },
  { id: "hover", title: "Hover dead-zone", note: "While the pen hovers, movements smaller than this radius are ignored (200 units = 1 mm).", names: ["HOVER_ZONE_HI", "HOVER_ZONE_MID", "HOVER_ZONE_LO"] },
  { id: "pen", title: "Pen and pressure", names: ["TIP_ON", "TIP_OFF", "TIP_PRESS_CHECKS", "TIP_RELEASE_CHECKS", "TIP_INVALID_CHECKS", "PRESSURE_GRADED", "F_REST", "F_FULL", "FLIP_X", "FLIP_Y"] },
  { id: "keys", title: "Express keys and LED", names: ["KEYS_ENABLED", "KEY_POLL_MS", "LED_IDLE", "LED_ACTIVE", "KEY_LO_MAX", "KEY_HI_MIN", "KEY_HI_MAX", "KEY_IDLE"] },
  { id: "tracking", title: "Tracking", advanced: true, names: ["FREQ_EVERY", "DET_THRESHOLD", "LOST_THRESHOLD", "AMP_HIGH", "AMP_LOW"] },
  {
    id: "timing", title: "Scan timing", advanced: true,
    note: "Do not go much below the defaults: the pen keeps ringing after each burst and the readings then jump when crossing coils.",
    names: ["BURST_MIN", "BURST_DEF", "BURST_MAX", "SETTLE", "FREQ_BURST", "SEARCH_BURST", "GRID_MARGIN", "FREQ_PAUSE_US", "GAIN_SETTLE_US"],
  },
];

const AREA = new Set(["AREA_X0", "AREA_X1", "AREA_Y0", "AREA_Y1"]);

// Settings that no group lists (added by newer firmware) still show up
export function groupsWithLeftovers(): Group[] {
  const listed = new Set(GROUPS.flatMap((g) => g.names));
  const rest = TABLE.map((s) => s.name as string).filter((n) => !listed.has(n) && !AREA.has(n));
  return rest.length ? [...GROUPS, { id: "other", title: "Other", names: rest }] : GROUPS;
}

const LABELS: Record<string, string> = {
  MAX_RATE: "Max report rate", SMOOTH_EMA: "Smoothing weight", SMOOTH_MA: "Averaged reports", SMOOTH_FULL_AMP: "Full-weight signal", SMOOTH_MIN_W: "Minimum weight",
  HOVER_ZONE_HI: "Dead-zone, strong signal", HOVER_ZONE_MID: "Dead-zone, medium signal", HOVER_ZONE_LO: "Dead-zone, weak signal",
  TIP_ON: "Tip down above", TIP_OFF: "Tip up below", TIP_PRESS_CHECKS: "Press checks", TIP_RELEASE_CHECKS: "Release checks", TIP_INVALID_CHECKS: "No-peak checks", F_REST: "Resonance at rest", F_FULL: "Resonance at full pressure",
  PRESSURE_GRADED: "Graded pressure", FLIP_X: "Mirror X", FLIP_Y: "Mirror Y",
  FREQ_EVERY: "Pressure check interval", DET_THRESHOLD: "Detection threshold", LOST_THRESHOLD: "Lost threshold", AMP_HIGH: "Limiter, upper", AMP_LOW: "Limiter, lower",
  BURST_MIN: "Shortest burst", BURST_DEF: "Normal burst", BURST_MAX: "Longest burst", SETTLE: "Settle time", FREQ_BURST: "Pressure burst", SEARCH_BURST: "Search burst",
  GRID_MARGIN: "Grid margin", FREQ_PAUSE_US: "Pressure ring-down pause", GAIN_SETTLE_US: "Gain settle time",
  LED_IDLE: "LED brightness, idle", LED_ACTIVE: "LED brightness, active", KEYS_ENABLED: "Express keys", KEY_POLL_MS: "Key polling interval",
  KEY_LO_MAX: "Keys 1 / 3 up to", KEY_HI_MIN: "Keys 2 / 4 from", KEY_HI_MAX: "Keys 2 / 4 up to", KEY_IDLE: "No key from",
};

export function label(name: string): string {
  return LABELS[name] ?? name.toLowerCase().replace(/_us$/, "").split("_").map((w, i) => (i === 0 ? w[0].toUpperCase() + w.slice(1) : w)).join(" ");
}
