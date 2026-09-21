import { S620_BOOTLOADER_SHA256 } from "../generated/known";

export type TabletId = { id: string | null; isS620: boolean };

const ID_PATTERN = /^[A-Z0-9]{2,8}_[A-Za-z0-9]{2,6}_\d{6}$/;    // like OEM02_T18e_241030
const S620_PATTERN = /^OEM02_T18e_\d{6}$/;                       // an S620, whatever the date of its firmware (OpenTabletDriver uses the same pattern)

export async function sha256(data: Uint8Array): Promise<string> {
  const h = await crypto.subtle.digest("SHA-256", data as BufferSource);
  return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export function findTabletId(flash: Uint8Array): string | null {
  for (const start of [0, 1]) {
    let run = "";
    for (let i = start; i + 1 <= flash.length; i += 2) {
      const ch = flash[i], hi = i + 1 < flash.length ? flash[i + 1] : 1;
      if (hi === 0 && ch >= 0x20 && ch < 0x7f) {
        run += String.fromCharCode(ch);
        continue;
      }
      if (ID_PATTERN.test(run)) return run;
      run = "";
    }
  }
  return null;
}

export async function identify(flash: Uint8Array): Promise<TabletId> {
  const id = findTabletId(flash);
  if (id !== null) return { id, isS620: S620_PATTERN.test(id) };
  return { id, isS620: (await sha256(flash.subarray(0, 0x4000))) === S620_BOOTLOADER_SHA256 };
}
