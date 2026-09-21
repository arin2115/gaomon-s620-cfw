# Gaomon S620 custom firmware

A replacement firmware for the Gaomon S620 drawing tablet, made for osu!. The stock firmware reports the pen position about 266 times per
second. This one gets close to 1000.

You flash and configure it from your browser: **[tablet.itsarin.dev](https://tablet.itsarin.dev)**

## What you get

- Up to 1000 position reports per second instead of 266.
- Settings you can change from the website while you play, and save on the tablet: smoothing, how much the cursor is held still while the pen
  hovers, flipping the axes, LED brightness and more.
- An active area: track only the part of the tablet you actually use.
- The express keys work (through OpenTabletDriver).
- It still shows up in OpenTabletDriver as a Gaomon S620, so your existing setup keeps working.

## What is different from the stock firmware

- **Pressure is on or off.** The tablet only tells the computer whether the pen is touching. There is no pressure curve. That is fine for
  osu!, but not for drawing.
- No tilt and no pen side buttons.
- The express keys only work with OpenTabletDriver, not with the Windows Ink driver alone.

## Installing

You need a Chromium-based browser (Chrome, Edge, Brave, Opera). Firefox and Safari do not support what the site needs.

1. Open [tablet.itsarin.dev](https://tablet.itsarin.dev) and go to the **Flash** tab.
2. Put the tablet in DFU mode: unplug it, hold **buttons 1 and 4**, plug it in while holding them, then let go.
3. Choose **Custom firmware**.
4. Click **Connect and back up**, pick the DFU device in the browser's list, and **download the backup**. Keep that file somewhere safe.
5. Click **Flash**. It takes well under a minute.
6. Unplug the tablet and plug it in again.

On Windows the tablet in DFU mode needs the WinUSB driver, or the browser will not list it. See [Troubleshooting](#troubleshooting).

## Changing settings

1. Plug the tablet in normally and open the **Configure** tab.
2. Click **Connect** and choose "Gaomon Tablet".
3. Move the sliders. With "Apply changes live" on, the tablet reacts straight away.
4. Click **Save to tablet** to keep your settings after unplugging it.

A few settings people usually want:

- **Smoothing weight**: higher means less smoothing. The default is 150 out of 256.
- **Active area**: type the part of the tablet you use, in millimetres, and click **Set area**. Outside it the pen counts as out of range,
  right up to the edge you set.
- **Mirror X / Mirror Y**: if an axis is the wrong way round.

Everything is reset with **Defaults**. **Erase saved** also deletes what is stored on the tablet.

## Going back to the stock firmware

Put the tablet in DFU mode again and use the **Flash** tab. Pick **Original firmware** to get the stock one, or **My backup** to restore
exactly what was on your tablet before (only while the page from step 4 is still open, otherwise choose your backup file with
"Choose a file…").

## Troubleshooting

**The tablet is dead or acts strange after flashing.**
Enter DFU mode (buttons 1 and 4 while plugging in) and flash again. The tablet's bootloader is never overwritten, so this always works.

**The browser does not list the DFU device (Windows).**
Chrome can only talk to devices that use the WinUSB driver. Install it once with [Zadig](https://zadig.akeo.ie/): put the tablet in DFU mode,
open Zadig, choose Options > List All Devices, select the device with ID 28E9 0189, pick WinUSB as the driver and click Install.
**Only install it on 28E9 0189.** Never on 256C 006F (the tablet in normal mode): that removes the tablet's HID driver, and the Configure
tab cannot find it any more.

**The Configure tab does not find the tablet, and Device Manager shows it as a "USB device" instead of HID.**
WinUSB was installed on the wrong device. In Device Manager, right-click the entries with VID_256C, choose Uninstall device and tick
"Attempt to remove the driver for this device". Unplug the tablet, plug it back in and Windows installs the HID driver again.

**The browser does not list the DFU device (Linux).**
Add a udev rule so you are allowed to use it:
`SUBSYSTEM=="usb", ATTR{idVendor}=="28e9", ATTR{idProduct}=="0189", MODE="0666"`

**"Unable to claim interface".**
Close other programs that use the tablet (dfu-util, vendor tools).

**The Configure tab does not find the tablet.**
Close OpenTabletDriver and try again. Make sure the custom firmware is flashed and you replugged the tablet after flashing.

**The pen clicks twice, or the click is glitchy.**
Run the calibration script: `pip install hidapi numpy`, close OpenTabletDriver, then `py tools/pressure_cal.py` in the repository folder. It
tells you what to do (hold the pen at different heights and pressures, it beeps when to start and stop), works out the settings for your
pen and tablet, tests them with you and only saves them if you say yes. If it goes wrong, `py tools/pressure_cal.py --restore` puts your old
settings back.

**The LED flashes in a pattern.**
Long solid light, then a number of blinks, is an error code: 1 crystal, 2 clock, 3 clock switch, 4 to 6 USB start-up, 7 crash, 8 timer. Replug the
tablet, and if it comes back, please open an issue with the number.

## For developers

How the firmware works and how to build it is in [docs/DEVELOPER.md](docs/DEVELOPER.md). The website's code is in [web/](web/).

## License

GPL-3.0, see [LICENSE](LICENSE). The bootloader (`bootloader/`) and the original firmware offered on the site belong to Gaomon/Huion and are
not covered by it.

## Disclaimer

This is an unofficial project and has nothing to do with Gaomon or Huion. Flashing firmware is always a risk, use it at your own risk. The
original firmware offered on the site is Gaomon's, provided only so you can go back to it.
