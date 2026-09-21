import { createHash } from "node:crypto";
import { readFile, stat } from "node:fs/promises";
import { join, resolve } from "node:path";

const DIR = resolve(process.env.FIRMWARE_DIR ?? "firmware");
const SIZE = 0x10000;

export const CATALOG = [
  { id: "custom", file: "s620.bin", name: "Custom firmware", description: "New firmware with about 1000 Hz reports, adjustable settings and express keys." },
  { id: "original", file: "s620_original.bin", name: "Original firmware", description: "The firmware the tablet shipped with. Use it to go back to stock." },
] as const;

export type FirmwareInfo = { id: string; name: string; description: string; size: number; sha256: string };

// The images are read from disk on every request, so replacing a file on the server updates the site without a rebuild.
// The hash is cached until the file's modification time changes.
const hashes = new Map<string, { mtime: number; sha256: string }>();

export async function load(id: string): Promise<{ info: FirmwareInfo; data: Buffer } | null> {
  const entry = CATALOG.find((c) => c.id === id);
  if (!entry) return null;
  const path = join(DIR, entry.file);
  try {
    const [data, st] = await Promise.all([readFile(path), stat(path)]);
    if (data.length !== SIZE) return null;   // a flash image is exactly 64 KB
    let h = hashes.get(id);
    if (!h || h.mtime !== st.mtimeMs) {
      h = { mtime: st.mtimeMs, sha256: createHash("sha256").update(data).digest("hex") };
      hashes.set(id, h);
    }
    return { info: { id, name: entry.name, description: entry.description, size: data.length, sha256: h.sha256 }, data };
  } catch {
    return null;
  }
}

export async function list(): Promise<FirmwareInfo[]> {
  const all = await Promise.all(CATALOG.map((c) => load(c.id)));
  return all.filter((x): x is NonNullable<typeof x> => x !== null).map((x) => x.info);
}
