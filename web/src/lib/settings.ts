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

// ---- active area ----

export const TABLET = { widthMm: 165.1, heightMm: 101.6, xMax: 33020, yMax: 20320 };
const UNITS_PER_MM = 200;

export type Rect = { x: number; y: number; w: number; h: number };   // mm, as reported (origin top left)

export function areaToValues(r: Rect) {
  return {
    AREA_X0: Math.round(r.x * UNITS_PER_MM),
    AREA_X1: Math.round((r.x + r.w) * UNITS_PER_MM),
    AREA_Y0: Math.round(r.y * UNITS_PER_MM),
    AREA_Y1: Math.round((r.y + r.h) * UNITS_PER_MM),
  };
}

export function areaFromValues(v: Values): Rect {
  return {
    x: v.AREA_X0 / UNITS_PER_MM,
    y: v.AREA_Y0 / UNITS_PER_MM,
    w: (v.AREA_X1 - v.AREA_X0) / UNITS_PER_MM,
    h: (v.AREA_Y1 - v.AREA_Y0) / UNITS_PER_MM,
  };
}
