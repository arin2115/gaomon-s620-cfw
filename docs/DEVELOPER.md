# Developer notes (HUION HV901 / GD32F350)

A from-scratch firmware: USB stack, EMR pen scanner, position/pressure tracker, express keys and status LED. The tablet's resident DFU
bootloader (first 16 KB of flash) is kept byte for byte, so the tablet can always be recovered.

How the firmware is built and how it works. For installing and using it, see the main [README](../README.md).

`dist/s620.bin` is the ready-to-flash 64 KB image.

## Flashing
1. Unplug the tablet, hold buttons 1 and 4, plug it in (DFU mode, USB ID 28E9:0189).
2. Flash `dist/s620.bin` at address 0x08000000 with your usual flasher (verify against the same file).
3. Unplug and plug it in again normally. To go back, flash any other image the same way.

## Building
```
pip install ziglang            # the Zig compiler (bundles clang for ARM)
py build.py                    # -> dist/s620.bin (run it from the repository root)
```
The build is reproducible: the image in `dist/` is what `build.py` produces from `src/`.

## Behaviour
- Reports as USB 256C:006F "Gaomon Tablet" with the original's descriptors, so OpenTabletDriver detects it as *Gaomon S620*.
- About 1000 reports/s (the USB limit), one per USB frame, always the freshest sample. Each axis is measured about every second report.
- Raw vendor mode (OpenTabletDriver): pen reports plus express-key reports `[8][0xE0][1][1][mask]`. Windows digitizer mode: pen reports only.
- Pressure is binary (tip up / tip down). The tip goes down when the pen's resonance shifts past the level of a medium press.
- Status LED (active-low): bright while the pen is tracked or a key is down, dim otherwise. Error codes: 2.5 s lit, then N blinks
  (1 crystal, 2 PLL, 3 clock switch, 4-6 USB init, 7 CPU fault, 8 cycle counter).

## Settings (runtime, saved in flash)
Every tunable (smoothing, hover dead-zone, tip/pressure thresholds, axis flips, resonance-check rate, limiter and timing values, active
coil area, LED brightness, express-key ladder, ...) is one 16-bit value in a table (`src/settings.h`, 40 values). They are changed over
USB with a vendor feature report (ID 0x30, a third HID collection on interface 0, usage page 0xFF02), using `tools/s620cfg.py`
(`pip install hidapi`, close OpenTabletDriver's exclusive access if it grabs the interface):
```
py tools/s620cfg.py show                          # all values, defaults, limits, descriptions
py tools/s620cfg.py set SMOOTH_EMA=190            # takes effect at once, lost at unplug
py tools/s620cfg.py set SMOOTH_EMA=190 --save     # ... and kept in flash
py tools/s620cfg.py area 25 20 35 26 --save       # track only this rectangle (mm, as reported), like a small osu! area
py tools/s620cfg.py area full --save
py tools/s620cfg.py save | reload | defaults [--save] | factory
```
- Storage: the last flash page (0x0800F800) holds one record (magic, version, count, values, CRC32). It is only written by an explicit save
  (the page erase stalls the tablet for tens of ms). A missing or corrupt record means the defaults. Flashing a full 64 KB image erases it.
- Values are clamped to their limits, so a bad value cannot brick the tablet; `factory` (or a reflash) restores the defaults.
- The active area limits which coils the pen is tracked on (a pen outside it counts as out of range); reported coordinates stay
  absolute over the whole tablet, and the scan time per report does not change.
- New settings are appended to the list in `settings.h`; older saved records stay valid (missing values take their defaults).
- The USB `bcdDevice` is 1.13 (the original: 1.12) so that Windows does not reuse a cached copy of the old report descriptor.
  After flashing this firmware for the first time, unplug and replug the tablet.

Do not shorten the measurement spacing (`BURST_DEF`, `SETTLE`) much below the defaults: the pen keeps ringing after each burst and readings
then depend on their position in the sequence, which shows up as jumps when crossing coils.

## Web tool
`web/` is the Next.js site (tablet.itsarin.dev) that flashes the firmware and edits the settings from a Chromium browser (WebUSB / WebHID), see `web/README.md`.

## Layout
```
src/startup.c      vector table, reset handler
src/main.c         clock setup, LED, main loop, USB frame-synced reports, express keys
src/usb.c/.h       polled USB device (DWC2-style USBFS), HID descriptors and strings
src/hid_desc.h, orig_desc.h   the original tablet's report and configuration descriptors
src/emr.c/.h       coil driver bursts, multiplexer selects, ADC, fixed measurement grid
src/coil_tables.h  coil -> multiplexer map recovered from the original firmware
src/pentrack.c/.h  pen search, window scan, position estimator, filters, resonance/tip logic
src/pen.c/.h       HID report builders
src/keys.c/.h      express-key ladders (decoded like the original)
src/settings.c/.h  runtime settings table, flash load/save, USB report payload
tools/s620cfg.py   command-line tool for the settings
web/               browser tool: flashing and settings
src/link.ld        linker script (app at 0x08004000, 4 KB RAM)
bootloader/        the tablet's original 16 KB DFU bootloader
```
