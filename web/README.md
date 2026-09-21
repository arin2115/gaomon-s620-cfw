# S620 web tool

A Next.js site that flashes the custom or the original firmware (WebUSB) and edits the custom firmware's settings (WebHID). It needs a
Chromium-based browser (Chrome, Edge, Brave, Opera) and https or localhost, and a Node server (the firmware images and the flash counter
are served by API routes).

```
npm install
npm run dev              # http://localhost:3000
npm run build && npm start
```

`npm run dev` and `npm run build` first run `scripts/prebuild.mjs`. It reads `../src/settings.h` (names, defaults, limits, descriptions) and
copies `../dist/s620.bin` to `firmware/`, so the site always matches the firmware in this folder. Build the firmware first (`py build.py`).

## Server
| what | where |
|---|---|
| Firmware images | `firmware/s620.bin` (custom, copied by prebuild) and `firmware/s620_original.bin` (stock). Read from disk on every request: replace a file and the site serves it at once, no rebuild. Both must be exactly 64 KB. `GET /api/firmware` lists them with their sha256, `GET /api/firmware/<custom or original>` downloads one. Set `FIRMWARE_DIR` to use another folder. |
| Flash counter | `GET /api/flashes` returns the number, `POST` adds one (max one per client per 30 s). Kept in `data/flash-count.json`, set `FLASH_COUNT_FILE` to move it. Only successful flashes of the custom firmware count. It is honour based, the server cannot see whether a flash really happened. |

## Flashing (`src/lib/dfu.ts`, `src/components/FlashPanel.tsx`)
Follows `gd32_flasher.py`: erase 1 KB pages, then per chunk set the address pointer and download it as block 2; read with upload from the
address pointer. The bootloader (first 16 KB) is never written, only the application from 0x08004000 on. Steps: put the tablet in DFU
mode (buttons 1+4 while plugging in), choose custom / original / your backup / a file, back up the whole flash (read twice and compared,
downloadable as a `.bin`), flash, read back and compare, restart. Windows needs the WinUSB driver on the DFU device (Zadig).

## Settings (`src/lib/settings.ts`)
Feature report 0x30 on the tablet's settings collection (usage page 0xFF02). Changes apply live, "Save to tablet" stores them in flash.
It also has JSON import/export and an active-area editor in mm.
