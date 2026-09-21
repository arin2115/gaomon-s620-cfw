"use client";

import { useEffect, useRef, useState } from "react";
import { DfuDevice, FLASH_SIZE, FLASH_START, requestDfuDevice, type Progress } from "../lib/dfu";

type Info = { id: string; name: string; description: string; size: number; sha256: string };
type Blob64 = { name: string; data: Uint8Array; sha256: string };
type Choice = "custom" | "original" | "file" | "backup";

const APP_START = FLASH_START + 0x4000;        // the bootloader below this is never touched
const SETTINGS_PAGE = FLASH_START + 0xf800;    // saved settings of the custom firmware
const FIRMWARE_URL = "api/firmware";
const COUNTER_URL = "api/flashes";

async function sha256(data: Uint8Array): Promise<string> {
  const h = await crypto.subtle.digest("SHA-256", data as BufferSource);
  return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

const same = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((v, i) => v === b[i]);

// The bootloader only starts an application whose first word (the stack pointer) points into RAM
function looksLikeFirmware(image: Uint8Array): boolean {
  const sp = new DataView(image.buffer, image.byteOffset).getUint32(APP_START - FLASH_START, true);
  return (sp & 0x2ffe0000) === 0x20000000;
}

function stamp(): string {
  const d = new Date();
  const p = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}`;
}

export function FlashPanel({ supported, onFlashed }: { supported: boolean; onFlashed: (count: number) => void }) {
  const [catalog, setCatalog] = useState<Info[]>([]);
  const [choice, setChoice] = useState<Choice>("custom");
  const [file, setFile] = useState<Blob64 | null>(null);
  const [backup, setBackup] = useState<Blob64 | null>(null);
  const [connected, setConnected] = useState(false);
  const [noBackup, setNoBackup] = useState(false);
  const [backupFailed, setBackupFailed] = useState(false);
  const [busy, setBusy] = useState(false);
  const [phase, setPhase] = useState("");
  const [percent, setPercent] = useState(0);
  const [log, setLog] = useState<string[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [done, setDone] = useState(false);
  const dfu = useRef<DfuDevice | null>(null);

  const say = (m: string) => setLog((l) => [...l, m]);

  useEffect(() => {
    fetch(FIRMWARE_URL).then((r) => r.json()).then(setCatalog).catch(() => {});
    return () => { dfu.current?.close(); };
  }, []);

  const progress: Progress = (p, n, total) => {
    setPhase({ backup: "Reading", erase: "Erasing", write: "Writing", verify: "Verifying" }[p]);
    setPercent(Math.round((100 * n) / Math.max(total, 1)));
  };

  const run = async (fn: () => Promise<void>) => {
    setBusy(true); setError(null); setDone(false);
    try {
      await fn();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false); setPhase("");
    }
  };

  const drop = async () => {
    await dfu.current?.close();
    dfu.current = null;
    setConnected(false);
  };

  const connect = async () => {
    if (dfu.current) return dfu.current;
    const d = new DfuDevice(await requestDfuDevice(), say);
    say(`Opening ${d.description}`);
    try {
      await d.open();
    } catch (e) {
      await d.close();
      const msg = e instanceof Error ? e.message : String(e);
      throw new Error(/access denied|claim/i.test(msg) ? `${msg} On Windows the DFU device needs the WinUSB driver (install it with Zadig); Chrome cannot use the vendor driver.` : msg);
    }
    dfu.current = d;
    setConnected(true);
    return d;
  };

  const makeBackup = () =>
    run(async () => {
      setLog([]);
      setBackupFailed(false);
      try {
        await readBackup();
      } catch (e) {
        setBackupFailed(true);
        throw e;
      }
    });

  const readBackup = async () => {
    const d = await connect();
    say("Reading the flash (twice, to be sure)...");
    const first = await d.read(FLASH_START, FLASH_SIZE, progress, "backup");
    const second = await d.read(FLASH_START, FLASH_SIZE, progress, "backup");
    if (!same(first, second)) throw new Error("Two reads of the flash gave different data. Unplug the tablet, enter DFU mode again and retry.");
    if (first.every((v) => v === 0xff) || first.every((v) => v === 0)) throw new Error("The flash reads back empty: the bootloader does not allow reading it.");
    setBackup({ name: `s620_backup_${stamp()}.bin`, data: first, sha256: await sha256(first) });
    say("Backup ready. Download it before you flash.");
    setPercent(100);
  };

  const downloadBackup = () => {
    if (!backup) return;
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([backup.data as BlobPart], { type: "application/octet-stream" }));
    a.download = backup.name;
    a.click();
    URL.revokeObjectURL(a.href);
  };

  const pickFile = async (f: File) => {
    const data = new Uint8Array(await f.arrayBuffer());
    setFile({ name: f.name, data, sha256: await sha256(data) });
    setChoice("file");
    setDone(false);
  };

  const selected: { size: number; sha256: string } | null =
    choice === "file" ? file && { size: file.data.length, sha256: file.sha256 } :
    choice === "backup" ? backup && { size: backup.data.length, sha256: backup.sha256 } :
    catalog.find((c) => c.id === choice) ?? null;

  const isCustom = choice === "custom";
  const backupOk = backup !== null || noBackup;
  const problem =
    !selected ? (choice === "file" ? "Choose a firmware file." : choice === "backup" ? "Make a backup first." : "This firmware is not available on the server.") :
    selected.size !== FLASH_SIZE ? `The image must be exactly 64 KB (this one is ${selected.size} bytes).` : null;

  const fetchImage = async (): Promise<Uint8Array> => {
    if (choice === "file") return file!.data;
    if (choice === "backup") return backup!.data;
    const info = catalog.find((c) => c.id === choice)!;
    const r = await fetch(`${FIRMWARE_URL}/${choice}`, { cache: "no-store" });
    if (!r.ok) throw new Error("Could not download the firmware from the server.");
    const data = new Uint8Array(await r.arrayBuffer());
    if ((await sha256(data)) !== info.sha256) throw new Error("The downloaded firmware does not match its checksum. Reload the page and try again.");
    return data;
  };

  const flash = () =>
    run(async () => {
      if (problem || !backupOk) return;
      const image = await fetchImage();
      if (!looksLikeFirmware(image)) throw new Error("This file does not look like firmware for this tablet (no valid start address at 0x08004000).");
      const end = isCustom ? SETTINGS_PAGE - FLASH_START : image.length;   // the custom firmware keeps the settings saved on the tablet
      const part = image.subarray(APP_START - FLASH_START, end);

      setLog([]);
      const d = await connect();
      try {
        say(`Flashing ${part.length} bytes at 0x${APP_START.toString(16)} (the bootloader is not touched)${isCustom ? ", saved settings kept" : ""}`);
        await d.write(part, APP_START, progress);
        say("Written. Reading it back...");
        const back = await d.read(APP_START, part.length, progress, "verify");
        if (!same(back, part)) throw new Error("Verification failed: the flash does not match the image. Flash again before you unplug the tablet.");
        say("Verified: the flash matches the image.");
        setPhase("Restarting");
        await d.leave();
        say("Done. The tablet restarts with the new firmware.");
        await drop();
        setPercent(100);
        setDone(true);
        if (isCustom) {
          // the counter is optional, a failure here must not look like a failed flash
          fetch(COUNTER_URL, { method: "POST" }).then((r) => r.json()).then((j) => typeof j.count === "number" && onFlashed(j.count)).catch(() => {});
        }
      } catch (e) {
        await drop();
        throw e;
      }
    });

  if (!supported) {
    return <section className="card"><p>Flashing needs WebUSB, which this browser does not have.</p></section>;
  }

  const options: { id: Choice; name: string; description: string; disabled?: boolean }[] = [
    ...catalog.map((c) => ({ id: c.id as Choice, name: c.name, description: c.description })),
    { id: "backup", name: "My backup", description: "Put back what was on the tablet before (needs a backup from step 3).", disabled: !backup },
    { id: "file", name: file ? `File: ${file.name}` : "Another file", description: "A 64 KB image of your own.", disabled: !file },
  ];

  return (
    <>
      <section className="card">
        <h2>1. Put the tablet in DFU mode</h2>
        <ol className="steps">
          <li>Unplug the tablet.</li>
          <li>Hold <b>all the buttons</b>.</li>
          <li>Plug it in while holding them, then let go.</li>
        </ol>
        <p className="muted small">
          The tablet then shows up as a DFU device (28E9:0189). Its bootloader is never modified, so DFU mode always works and you can put any
          firmware back the same way.
        </p>
      </section>

      <section className="card">
        <h2>2. Choose the firmware</h2>
        <div className="choices" role="radiogroup">
          {options.map((o) => (
            <label key={o.id} className={`choice${choice === o.id ? " on" : ""}${o.disabled ? " off" : ""}`}>
              <input type="radio" name="fw" checked={choice === o.id} disabled={o.disabled} onChange={() => setChoice(o.id)} />
              <span>
                <b>{o.name}</b>
                <span className="muted small block">{o.description}</span>
              </span>
            </label>
          ))}
        </div>
        <div className="btns pad-top">
          <label className="button ghost">
            Choose a file…
            <input type="file" accept=".bin" hidden onChange={(e) => e.target.files?.[0] && pickFile(e.target.files[0])} />
          </label>
        </div>
        {selected && <p className="muted small mono">{selected.size} bytes · sha256 {selected.sha256}</p>}
        {problem && <div className="banner err">{problem}</div>}
      </section>

      <section className="card">
        <h2>3. Back up the tablet</h2>
        <p className="muted small">Reads the whole flash of your tablet into this page (nothing is uploaded). Download it and keep it: you can flash it back later.</p>
        <div className="btns">
          <button onClick={makeBackup} disabled={busy}>{busy && !backup ? "Working…" : backup ? "Back up again" : "Connect and back up"}</button>
          {backup && <button className="ghost" onClick={downloadBackup}>Download backup</button>}
        </div>
        {backup && <p className="muted small mono">{backup.name} · sha256 {backup.sha256}</p>}
        {!backup && backupFailed && (
          <label className="switch pad-top">
            <input type="checkbox" checked={noBackup} onChange={(e) => setNoBackup(e.target.checked)} />
            <span>I could not make a backup and want to flash anyway</span>
          </label>
        )}
      </section>

      <section className="card">
        <h2>4. Flash</h2>
        <div className="btns">
          <button onClick={flash} disabled={busy || !!problem || !backupOk}>{busy && backupOk ? "Working…" : "Flash"}</button>
        </div>
        <p className="muted small">
          {backupOk ? "Do not unplug the tablet or close this page while it is working." : "Make a backup first (step 3)."}
          {connected ? " The tablet is connected." : " You will be asked to pick the DFU device."}
        </p>
        {(busy || percent > 0) && (
          <div className="progress" aria-label="Progress">
            <div style={{ width: `${percent}%` }} />
            <span>{phase || (done ? "Finished" : "")} {percent}%</span>
          </div>
        )}
        {error && <div className="banner err">{error}</div>}
        {done && <div className="banner ok">Flashed. If the tablet does not restart by itself, unplug it and plug it in again{isCustom ? ", then open Configure" : ""}.</div>}
        {log.length > 0 && <pre className="log">{log.join("\n")}</pre>}
      </section>

      <section className="card">
        <h2>Troubleshooting</h2>
        <ul className="muted small">
          <li><b>The DFU device is not listed (Windows):</b> Chrome can only talk to devices that use the WinUSB driver. Install it for the DFU device with Zadig (28E9:0189, driver WinUSB) once.
            Only for 28E9:0189, never for 256C:006F (the tablet in normal mode), or the Configure tab cannot find the tablet any more.</li>
          <li><b>Configure does not find the tablet and Device Manager shows a "USB device" instead of HID:</b> WinUSB is on the wrong device.
            In Device Manager, uninstall the entries with VID_256C (tick "Attempt to remove the driver for this device"), then unplug and replug the tablet.</li>
          <li><b>Linux:</b> add a udev rule for 28E9:0189 (<code>SUBSYSTEM=="usb", ATTR{"{idVendor}"}=="28e9", ATTR{"{idProduct}"}=="0189", MODE="0666"</code>).</li>
          <li><b>“Unable to claim interface”:</b> close other tools that use the tablet (dfu-util, vendor updaters).</li>
          <li>Something went wrong while flashing: the tablet is still in DFU mode and its bootloader is intact. Flash again, or flash your backup.</li>
        </ul>
      </section>
    </>
  );
}
