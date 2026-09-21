import { SETTINGS_PAYLOAD, SETTINGS_TABLE, SETTINGS_VERSION } from "../generated/settings-table";

export const TABLE = SETTINGS_TABLE;
export type Values = Record<string, number>;

export const USB_ID = { vendorId: 0x256c, productId: 0x006f };
export const USAGE_PAGE = 0xff02;
export const REPORT_ID = 0x30;

export const Cmd = { none: 0, apply: 1, applySave: 2, defaults: 3, save: 4, reload: 5, factory: 6 } as const;
export const Status = { savedValid: 1, dirty: 2, failed: 4 } as const;

// handled: how many commands the firmware has processed (wraps at 256). diag: USB counters of the settings write, for troubleshooting.
export type Snapshot = { values: Values; status: number; handled: number; diag: { writes: number; bytes: number; wLength: number } };

export function defaults(): Values {
  return Object.fromEntries(TABLE.map((s) => [s.name, s.def]));
}

export function clamp(name: string, v: number): number {
  const s = TABLE.find((t) => t.name === name);
  if (!s) return v;
  return Math.min(s.max, Math.max(s.min, Math.round(v)));
}

// Report layout (after the report ID): [0] command / status, [1] version, [2] count, [3] unused, then u16 values (little endian).
export function encode(cmd: number, values: Values): Uint8Array {
  const out = new Uint8Array(SETTINGS_PAYLOAD);
  const view = new DataView(out.buffer);
  out[0] = cmd;
  out[1] = SETTINGS_VERSION;
  out[2] = TABLE.length;
  TABLE.forEach((s, i) => view.setUint16(4 + 2 * i, values[s.name] ?? s.def, true));
  return out;
}

export function decode(data: Uint8Array): Snapshot {
  if (data.length === SETTINGS_PAYLOAD + 1 && data[0] === REPORT_ID) data = data.subarray(1);   // some stacks keep the report ID
  if (data.length < SETTINGS_PAYLOAD) throw new Error(`Short settings report (${data.length} bytes)`);
  const version = data[1];
  const count = data[2];
  if (version !== SETTINGS_VERSION || count !== TABLE.length) {
    throw new Error(
      `The tablet firmware has settings version ${version} with ${count} values, this site expects version ${SETTINGS_VERSION} with ${TABLE.length}. Flash the matching firmware.`,
    );
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const values: Values = {};
  TABLE.forEach((s, i) => (values[s.name] = view.getUint16(4 + 2 * i, true)));
  return { values, status: data[0], handled: data[3], diag: { writes: data[SETTINGS_PAYLOAD - 3], bytes: data[SETTINGS_PAYLOAD - 2], wLength: data[SETTINGS_PAYLOAD - 1] } };
}

export function statusText(status: number): string {
  const parts = [status & Status.savedValid ? "Saved settings found in flash" : "Nothing saved yet (defaults are used at start)"];
  parts.push(status & Status.dirty ? "unsaved changes on the tablet" : "matches the saved settings");
  if (status & Status.failed) parts.push("the last save or load FAILED");
  return parts.join(", ");
}

// ---- WebHID connection ----

const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));

export class TabletSettings {
  private queue: Promise<unknown> = Promise.resolve();
  private handled: number | null = null;

  constructor(readonly device: HIDDevice) {}

  static async request(): Promise<TabletSettings> {
    const [device] = await navigator.hid.requestDevice({ filters: [{ ...USB_ID, usagePage: USAGE_PAGE, usage: 1 }] });
    if (!device) throw new Error("No device selected");
    return TabletSettings.open(device);
  }

  // Devices the user already allowed on an earlier visit
  static async previouslyAllowed(): Promise<TabletSettings | null> {
    const device = (await navigator.hid.getDevices()).find(
      (d) => d.vendorId === USB_ID.vendorId && d.productId === USB_ID.productId && d.collections.some((c) => c.usagePage === USAGE_PAGE),
    );
    return device ? TabletSettings.open(device) : null;
  }

  static async open(device: HIDDevice): Promise<TabletSettings> {
    if (!device.opened) await device.open();
    return new TabletSettings(device);
  }

  async close() {
    if (this.device.opened) await this.device.close();
  }

  async read(): Promise<Snapshot> {
    const view = await this.device.receiveFeatureReport(REPORT_ID);
    const snap = decode(new Uint8Array(view.buffer, view.byteOffset, view.byteLength));
    this.handled = snap.handled;
    return snap;
  }

  // Sends a command and waits until the firmware has handled it (its counter goes up), then returns the result.
  // Commands run one after another. If the counter never moves the write did not arrive, and this throws instead of
  // returning the old values as if nothing happened.
  send(cmd: number, values: Values): Promise<Snapshot> {
    const run = async () => {
      const before = this.handled ?? (await this.read()).handled;
      await this.device.sendFeatureReport(REPORT_ID, encode(cmd, values) as BufferSource);
      for (let i = 0; i < 20; i++) {
        await wait(150);
        const snap = await this.read();
        if (snap.handled !== before) return snap;
      }
      const s = await this.read();
      throw new Error(
        `The tablet did not act on the write (USB writes seen ${s.diag.writes}, last length ${s.diag.wLength}, bytes kept ${s.diag.bytes}). ` +
          "Flash the latest firmware from this site, then try again.",
      );
    };
    const next = this.queue.then(run, run);
    this.queue = next.catch(() => {});
    return next;
  }
}

// ---- active area <-> coil positions (same maths as tools/s620cfg.py) ----

export const TABLET = { widthMm: 165.1, heightMm: 101.6, xMax: 33020, yMax: 20320, xCoils: 30, yCoils: 19 };
const UNITS_PER_MM = 200;

// A coil position's cell starts at pitch * (pos - 1) + offset (raw units, before flipping)
const X_PITCH = 1179, X_OFFSET = 4, Y_PITCH = 1195, Y_OFFSET = 2;

function coilPos(raw: number, offset: number, pitch: number, count: number): number {
  const k = Math.floor((raw - offset) / pitch) + 2;
  return Math.max(0, Math.min(count - 1, k - 1));
}

export type Rect = { x: number; y: number; w: number; h: number };   // mm, as reported (origin top left)

export function areaToCoils(r: Rect, flipX: boolean, flipY: boolean) {
  let ux0 = Math.round(r.x * UNITS_PER_MM), ux1 = Math.round((r.x + r.w) * UNITS_PER_MM);
  let uy0 = Math.round(r.y * UNITS_PER_MM), uy1 = Math.round((r.y + r.h) * UNITS_PER_MM);
  if (flipX) [ux0, ux1] = [TABLET.xMax - ux1, TABLET.xMax - ux0];
  if (flipY) [uy0, uy1] = [TABLET.yMax - uy1, TABLET.yMax - uy0];
  ux0 = Math.max(ux0, 0); ux1 = Math.min(ux1, TABLET.xMax);
  uy0 = Math.max(uy0, 0); uy1 = Math.min(uy1, TABLET.yMax);
  // one coil of margin each side, so edge positions keep their neighbours for the estimator
  return {
    X_MIN: Math.max(0, coilPos(ux0, X_OFFSET, X_PITCH, TABLET.xCoils) - 1),
    X_MAX: Math.min(TABLET.xCoils - 1, coilPos(ux1, X_OFFSET, X_PITCH, TABLET.xCoils) + 1),
    Y_MIN: Math.max(0, coilPos(uy0, Y_OFFSET, Y_PITCH, TABLET.yCoils) - 1),
    Y_MAX: Math.min(TABLET.yCoils - 1, coilPos(uy1, Y_OFFSET, Y_PITCH, TABLET.yCoils) + 1),
  };
}

// The rectangle (mm, as reported) that the current coil range covers
export function coilsToArea(v: Values): Rect {
  const rawX0 = Math.max(0, X_PITCH * (v.X_MIN - 1) + X_OFFSET), rawX1 = Math.min(TABLET.xMax, X_PITCH * v.X_MAX + X_OFFSET);
  const rawY0 = Math.max(0, Y_PITCH * (v.Y_MIN - 1) + Y_OFFSET), rawY1 = Math.min(TABLET.yMax, Y_PITCH * v.Y_MAX + Y_OFFSET);
  const [x0, x1] = v.FLIP_X ? [TABLET.xMax - rawX1, TABLET.xMax - rawX0] : [rawX0, rawX1];
  const [y0, y1] = v.FLIP_Y ? [TABLET.yMax - rawY1, TABLET.yMax - rawY0] : [rawY0, rawY1];
  return { x: x0 / UNITS_PER_MM, y: y0 / UNITS_PER_MM, w: (x1 - x0) / UNITS_PER_MM, h: (y1 - y0) / UNITS_PER_MM };
}
