// The tablet's DFU bootloader (GigaDevice, DfuSe-style) over WebUSB. The sequence follows gd32_flasher.py, which is known to work with it:
// erase 1 KB pages, then for every chunk set the address pointer and download it as block 2; upload starts at the address pointer.

export const DFU_IDS = [{ vendorId: 0x28e9, productId: 0x0189 }];
export const FLASH_START = 0x08000000;
export const FLASH_SIZE = 0x10000;
const PAGE_SIZE = 1024;
const MAX_WRITE_CHUNK = 1024;   // the bootloader's EP0 buffer

const Req = { dnload: 1, upload: 2, getStatus: 3, clrStatus: 4, abort: 6 } as const;
const State = { dfuIdle: 2, dnBusy: 4, error: 10 } as const;

export type Progress = (phase: "backup" | "erase" | "write" | "verify", done: number, total: number) => void;

const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));

export function dfuSupported(): boolean {
  return typeof navigator !== "undefined" && "usb" in navigator;
}

export async function requestDfuDevice(): Promise<USBDevice> {
  return navigator.usb.requestDevice({ filters: [...DFU_IDS, { classCode: 0xfe, subclassCode: 0x01 }] });
}

export class DfuDevice {
  private transferSize = 2048;
  private iface = 0;

  constructor(readonly device: USBDevice, readonly log: (msg: string) => void = () => {}) {}

  get description(): string {
    const id = `${this.device.vendorId.toString(16)}:${this.device.productId.toString(16).padStart(4, "0")}`;
    return `${this.device.productName ?? "DFU device"} (${id})`;
  }

  async open() {
    const d = this.device;
    await d.open();
    if (d.configuration === null) await d.selectConfiguration(1);
    const found = d.configuration!.interfaces.find((i) => i.alternates.some((a) => a.interfaceClass === 0xfe && a.interfaceSubclass === 0x01));
    if (!found) throw new Error("This device has no DFU interface.");
    if (found.alternates[0].interfaceProtocol !== 2) throw new Error("The tablet is not in DFU mode (unplug it, hold buttons 1 and 4, plug it in).");
    this.iface = found.interfaceNumber;
    await d.claimInterface(this.iface);
    try {
      await this.readTransferSize();
    } catch {
      // keep the default
    }
    this.log(`Transfer size ${this.transferSize} bytes`);
    await this.ensureIdle();
  }

  async close() {
    try {
      await this.device.close();
    } catch {
      // the device may already be gone after leaving DFU
    }
  }

  // wTransferSize from the DFU functional descriptor in the configuration descriptor
  private async readTransferSize() {
    const r = await this.device.controlTransferIn({ requestType: "standard", recipient: "device", request: 0x06, value: 0x0200, index: 0 }, 1024);
    const v = r.data;
    if (!v) return;
    for (let p = 0; p + 2 <= v.byteLength; ) {
      const len = v.getUint8(p);
      if (len < 2) break;
      if (v.getUint8(p + 1) === 0x21 && len >= 9 && p + 9 <= v.byteLength) {
        const size = v.getUint16(p + 5, true);
        if (size > 0) this.transferSize = Math.min(size, 4096);
        return;
      }
      p += len;
    }
  }

  private out(request: number, value: number, data?: Uint8Array) {
    return this.device.controlTransferOut({ requestType: "class", recipient: "interface", request, value, index: this.iface }, data as BufferSource | undefined);
  }

  private async in(request: number, value: number, length: number) {
    const r = await this.device.controlTransferIn({ requestType: "class", recipient: "interface", request, value, index: this.iface }, length);
    if (r.status !== "ok" || !r.data) throw new Error(`USB transfer failed (${r.status})`);
    return r.data;
  }

  private async getStatus() {
    const d = await this.in(Req.getStatus, 0, 6);
    return { status: d.getUint8(0), pollMs: d.getUint8(1) | (d.getUint8(2) << 8) | (d.getUint8(3) << 16), state: d.getUint8(4) };
  }

  // back to dfuIDLE from whatever state the last operation left
  private async ensureIdle() {
    for (let attempt = 0; attempt < 5; attempt++) {
      const s = await this.getStatus();
      if (s.state === State.dfuIdle) return;
      if (s.state === State.error) await this.out(Req.clrStatus, 0);
      else if (s.state === State.dnBusy) await wait(Math.max(s.pollMs, 10));
      else await this.out(Req.abort, 0);
      await wait(10);
    }
    const s = await this.getStatus();
    if (s.state !== State.dfuIdle) throw new Error(`The bootloader is not idle (state ${s.state}).`);
  }

  // waits while the bootloader is busy, fails on an error status
  private async pollReady() {
    for (let i = 0; i < 3000; i++) {
      const s = await this.getStatus();
      if (s.status !== 0) {
        await this.out(Req.clrStatus, 0).catch(() => {});
        throw new Error(`The bootloader reported an error (status ${s.status}, state ${s.state}).`);
      }
      if (s.state !== State.dnBusy) return;
      await wait(Math.max(s.pollMs, 1));
    }
    throw new Error("The bootloader did not finish (timeout).");
  }

  private async command(cmd: number, addr: number) {
    const p = new Uint8Array(5);
    p[0] = cmd;
    new DataView(p.buffer).setUint32(1, addr, true);
    await this.out(Req.dnload, 0, p);
    await this.pollReady();
  }

  private setAddress(addr: number) {
    return this.command(0x21, addr);
  }

  async read(start: number, length: number, progress: Progress, phase: "backup" | "verify"): Promise<Uint8Array> {
    await this.ensureIdle();
    await this.setAddress(start);
    await this.out(Req.abort, 0);
    await this.ensureIdle();
    const data = new Uint8Array(length);
    let got = 0;
    for (let block = 2; got < length; block++) {
      progress(phase, got, length);
      const chunk = await this.in(Req.upload, block, Math.min(this.transferSize, length - got));
      if (chunk.byteLength === 0) throw new Error("The bootloader returned no data (flash read protected?).");
      data.set(new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength), got);
      got += chunk.byteLength;
    }
    progress(phase, length, length);
    await this.out(Req.abort, 0).catch(() => {});
    return data;
  }

  async write(data: Uint8Array, start: number, progress: Progress) {
    await this.ensureIdle();

    const first = start - (start % PAGE_SIZE);
    const pages = Math.ceil((start + data.length - first) / PAGE_SIZE);
    for (let i = 0; i < pages; i++) {
      progress("erase", i, pages);
      await this.command(0x41, first + i * PAGE_SIZE);
    }
    progress("erase", pages, pages);

    const chunkSize = Math.min(this.transferSize, MAX_WRITE_CHUNK);
    for (let off = 0; off < data.length; off += chunkSize) {
      progress("write", off, data.length);
      await this.setAddress(start + off);
      await this.out(Req.dnload, 2, data.subarray(off, off + chunkSize));
      await this.pollReady();
    }
    progress("write", data.length, data.length);
  }

  // Leaves DFU: address pointer to the start of flash, then an empty download makes the bootloader start the application
  async leave() {
    await this.ensureIdle();
    await this.setAddress(FLASH_START);
    try {
      await this.out(Req.dnload, 0).catch(() => this.out(Req.dnload, 2));
      await this.getStatus();
    } catch {
      // the device resets and drops off the bus while answering
    }
    await this.device.reset().catch(() => {});
  }
}
