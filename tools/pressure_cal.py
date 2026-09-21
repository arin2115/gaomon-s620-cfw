"""Pressure / tip calibration for the S620 custom firmware.

  py pressure_cal.py                 guided calibration (close OpenTabletDriver first, keep the tablet cover on)
  py pressure_cal.py --restore       put back the settings the tablet had before an interrupted run
  py pressure_cal.py --analyse FILE  work out the settings from an earlier recording (pressure_cal_data.npz), no tablet needed

Needs:  pip install hidapi numpy
It tells you what to do and beeps at the start and the end of every recording, you never have to count seconds.

How it works: the tablet's settings are switched (in RAM only) to a mode where the pressure field carries the pen's raw
resonance index, so the script can see what the pen does when hovering, just touching, pressing and tapping. From that it picks
the tip thresholds and how many checks in a row are needed to go down / let go, applies them, tests them with your pen and only
saves them to the tablet if you say yes.
"""
import argparse, io, json, os, re, struct, sys, time
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
import numpy as np
try:
    import winsound
    def beep(hz, ms): winsound.Beep(hz, ms)
except ImportError:
    def beep(hz, ms): print('\a', end='', flush=True)

VID, PID = 0x256C, 0x006F
USAGE_PAGE, REPORT_ID, PAYLOAD = 0xFF02, 0x30, 95
HERE = os.path.dirname(os.path.abspath(__file__))
BACKUP = os.path.join(HERE, 'pressure_cal_backup.json')
DATA = os.path.join(HERE, 'pressure_cal_data.npz')

STEPS = [   # name, what to do, seconds
    ('hover10', 'Hold the pen tip about 10 mm ABOVE the tablet (a finger or two of air). Keep it still.', 3),
    ('hover5', 'Hold it about 5 mm above the surface, not touching. Keep it still.', 3),
    ('hover2', 'Hold it about 2 mm above the surface, still NOT touching. Keep it still.', 3),
    ('touch', 'Rest the tip on the surface with NO force, only the weight of the pen. Keep it still.', 3),
    ('light', 'LIGHT press, the lightest click you would ever want to register. Hold it steady.', 3),
    ('medium', 'MEDIUM press, normal osu! force. Hold it steady.', 3),
    ('firm', 'FIRM press. Hold it steady.', 3),
    ('hard', 'HARD press, as hard as you ever press. Hold it steady.', 3),
    ('taps', 'TAP the tablet 10 times, exactly like clicking in osu! with your normal, lightest habitual force. Then stop.', 8),
]
NOT_PRESSED = ('hover10', 'hover5', 'hover2', 'touch')
PRESSES = ('light', 'medium', 'firm', 'hard')


# ---- settings over the tablet's settings report (same protocol as the website) ----

def load_table():
    text = open(os.path.join(HERE, '..', 'src', 'settings.h'), encoding='utf-8').read()
    version = int(re.search(r'#define SETTINGS_VERSION\s+(\d+)', text).group(1))
    rows = re.findall(r'X\((\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*"[^"]*"\)', text)
    return version, [(n, int(d), int(lo), int(hi)) for n, d, lo, hi in rows]


VERSION, TABLE = load_table()
NAMES = [t[0] for t in TABLE]


class Tablet:
    def __init__(self):
        import hid
        for i in hid.enumerate(VID, PID):
            if i.get('usage_page') == USAGE_PAGE:
                self.dev = hid.device()
                self.dev.open_path(i['path'])
                return
        sys.exit('Settings interface not found. Is the custom firmware flashed, the tablet plugged in and OpenTabletDriver closed?')

    def read(self):
        r = bytes(self.dev.get_feature_report(REPORT_ID, PAYLOAD + 1))
        if len(r) < PAYLOAD + 1:
            sys.exit(f'Short settings report ({len(r)} bytes).')
        ver, count, handled = r[2], r[3], r[4]
        if ver != VERSION or count != len(TABLE):
            sys.exit(f'The tablet has settings version {ver} with {count} values, this script expects {VERSION} with {len(TABLE)}. '
                     'Flash the matching firmware from the website.')
        return dict(zip(NAMES, struct.unpack_from(f'<{count}H', r, 5))), handled

    def send(self, cmd, values):
        _, before = self.read()
        pl = bytearray(PAYLOAD)
        pl[0], pl[1], pl[2] = cmd, VERSION, len(TABLE)
        struct.pack_into(f'<{len(TABLE)}H', pl, 4, *[values[n] for n in NAMES])
        self.dev.send_feature_report(bytes([REPORT_ID]) + bytes(pl))
        for _ in range(30):
            time.sleep(0.15)
            now, handled = self.read()
            if handled != before:
                return now
        sys.exit('The tablet did not act on the settings write. Flash the latest firmware from the website and try again.')

    def apply(self, values):
        return self.send(1, values)

    def save(self, values):
        return self.send(2, values)


# ---- pen reports ----

def open_pen_streams():
    import hid
    devs = []
    for i in hid.enumerate(VID, PID):
        if i.get('usage_page') == USAGE_PAGE:
            continue
        try:
            d = hid.device()
            d.open_path(i['path'])
            d.set_nonblocking(True)
            d.read(64)
            devs.append(d)
        except (OSError, ValueError):
            pass
    return devs


def parse(r):
    """-> (in_range, tip, pressure) or None"""
    if r[0] == 0x0A and len(r) >= 10:
        return bool(r[1] & 0x40), bool(r[1] & 1), r[6] | r[7] << 8
    if r[0] == 8 and len(r) >= 12 and r[1] & 0x80:
        return True, bool(r[1] & 1), r[6] | r[7] << 8
    return None


def record(devs, secs):
    rows = []
    end = time.time() + secs
    while time.time() < end:
        got = False
        for d in devs:
            try:
                r = d.read(64)
            except OSError:
                continue
            if r:
                got = True
                q = parse(r)
                if q and q[0]:
                    rows.append((time.time(), q[1], q[2]))
        if not got:
            time.sleep(0.0004)
    return np.array(rows, float).reshape(-1, 3)


def guided(devs, text, secs=3.0, settle=0.4):
    print(f'\n{text}')
    input('   Get into position, then press Enter... ')
    record(devs, 0.2)            # drop what piled up while waiting
    beep(1000, 150)
    rows = record(devs, secs)
    beep(600, 250)
    return rows[rows[:, 0] > rows[:, 0].min() + settle] if len(rows) else rows


# ---- analysis ----

def resonance(rows):
    """(index of every report, clear-peak flag). In measurement mode a report without a clear peak has tip 0 and pressure 0."""
    valid = rows[:, 1] > 0
    return 6 + rows[:, 2] * 6 / 8191.0, valid


def simulate(f, valid, on, off, press, release, invalid, every):
    """Runs the firmware's tip logic over a recording. Returns (times the tip went down, times it let go)."""
    tip = False; downs = ups = 0; prun = rrun = 0; inv_run = 0; cur = 0.0; started = False
    for k in range(len(f)):
        if valid[k]:
            cur = f[k]
            started = True
        check = k % every == 0
        if check:
            inv_run = 0 if valid[k] else inv_run + 1
        if not started:
            continue
        if tip:
            up = inv_run >= invalid or cur < off
            if not up:
                rrun = 0
            elif check:
                rrun += 1
                if rrun >= release:
                    tip = False; ups += 1
        elif inv_run < 2 and cur > on:
            if check:
                prun += 1
                if prun >= press:
                    tip = True; downs += 1; rrun = 0; prun = 0
        else:
            prun = 0
    return downs, ups


def episodes(tips):
    """number of separate tip-down periods in a recording of the tip flag"""
    t = tips > 0
    return int(t[0]) + int(np.sum(t[1:] & ~t[:-1])) if len(t) else 0


def tap_peaks(f, ok, floor):
    """the height of every tap: stretches where the reading is above `floor`, merged when the gap is short"""
    idx = np.where(ok & (f > floor))[0]
    if not len(idx):
        return []
    groups = np.split(idx, np.where(np.diff(idx) > 60)[0] + 1)
    return [float(np.percentile(f[g], 90)) for g in groups if len(g) >= 15]


def analyse(rec, every):
    stats = {}
    for name, _, _ in STEPS:
        if name == 'taps':
            continue
        rows = rec.get(name)
        if rows is None or len(rows) < 100:
            print(f'   {name}: too few reports (pen out of range?)')
            return None
        f, ok = resonance(rows)
        if not ok.any():
            print(f'   {name}: the tablet never found a clear peak')
            return None
        stats[name] = dict(n=len(rows), valid=ok.mean(), p2=np.percentile(f[ok], 2), med=np.median(f[ok]), hi=np.percentile(f[ok], 98),
                           p995=np.percentile(f[ok], 99.5), sd=f[ok].std())
    print('\nWhat the pen did (resonance index, higher = more pressed):')
    print('  step      reports  clear peak   low     median  high')
    for name, s in stats.items():
        print(f'  {name:8s} {s["n"]:7d}   {s["valid"] * 100:5.1f}%   {s["p2"]:6.2f}  {s["med"]:6.2f}  {s["hi"]:6.2f}')

    hover_hi = max(stats[k]['p995'] for k in ('hover10', 'hover5', 'hover2'))
    not_pressed = max(stats[k]['p995'] for k in NOT_PRESSED)        # a pen resting on the surface without force must not click
    lowest_press = stats['light']['p2']

    taps = rec.get('taps')
    peaks = []
    if taps is not None and len(taps) > 100:
        f, ok = resonance(taps)
        peaks = tap_peaks(f, ok, hover_hi + 0.05)
    if len(peaks) >= 5:
        tap_lo = float(np.percentile(peaks, 10))
        print(f'\nYour taps: {len(peaks)} found, the lightest reach {tap_lo:.2f}, the median tap {np.median(peaks):.2f}')
        lowest_press = min(lowest_press, tap_lo)
    else:
        print('\nYour taps: fewer than 5 clear taps were recorded, so only the light press is used to place the threshold.')
        peaks = []

    gap = lowest_press - not_pressed
    print(f'Highest reading without pressing (hover or resting on the surface): {not_pressed:.2f}')
    print(f'Lowest reading of a press you want to register: {lowest_press:.2f}   (gap {gap:.2f})')
    if gap < 0.05:
        print('These are too close: a resting pen and your lightest taps read the same, no threshold can separate them.')
        print('Tap a little harder (or rest the pen more gently) and run it again. The values below are the best possible compromise.')
        on = not_pressed + 0.02
    else:
        on = not_pressed + 0.5 * gap
    hysteresis = max(0.15, 4 * stats['touch']['sd'])
    off = max(on - hysteresis, hover_hi + 0.02)
    off = min(off, on - 0.03)
    on, off = round(on, 2), round(off, 2)
    rest = round(float(stats['touch']['med']), 2)
    full = max(round(float(stats['hard']['med']), 2), rest + 0.3)

    series = {n: resonance(rec[n]) for n in NOT_PRESSED + PRESSES}
    tap_series = resonance(taps) if peaks else None

    def score(press, release, invalid):
        bad = 0
        for n in NOT_PRESSED:                                        # any click here is a false click
            bad += simulate(*series[n], on, off, press, release, invalid, every)[0]
        for n in PRESSES:                                            # one click, and no letting go while held
            d, u = simulate(*series[n], on, off, press, release, invalid, every)
            bad += max(0, d - 1) + u
        if tap_series is not None:
            d, u = simulate(*tap_series, on, off, press, release, invalid, every)
            bad += max(0, d - len(peaks)) + max(0, u - d)
        return bad

    options = [(score(p, r, i), p + r, i, p, r, i) for p in (1, 2, 3) for r in (1, 2, 3, 4) for i in (4, 8, 12, 16, 24, 40)]
    best = min(options)
    bad, _, _, press_n, release_n, invalid_n = best
    raw = score(1, 1, 2)
    print(f'\nWrong clicks over all recordings: {raw} with no protection, {bad} with the settings below.')
    if bad:
        print(f'Some remain, the recording needs a look ({os.path.basename(DATA)}).')
    if peaks:
        d, _ = simulate(*tap_series, on, off, press_n, release_n, invalid_n, every)
        print(f'Taps: {len(peaks)} taps found in the recording, the tip went down {d} times.')
    print(f'One check takes about {every} ms: going down is delayed by about {press_n * every} ms, letting go by about {release_n * every} ms.')
    return {'TIP_ON': int(round(on * 1000)), 'TIP_OFF': int(round(off * 1000)), 'F_REST': int(round(rest * 1000)), 'F_FULL': int(round(full * 1000)),
            'TIP_PRESS_CHECKS': press_n, 'TIP_RELEASE_CHECKS': release_n, 'TIP_INVALID_CHECKS': invalid_n}


def main():
    ap = argparse.ArgumentParser(description='S620 pressure calibration')
    ap.add_argument('--restore', action='store_true', help='restore the settings saved at the start of an interrupted run')
    ap.add_argument('--analyse', metavar='FILE', help='analyse an earlier recording instead of recording a new one')
    ap.add_argument('--every', type=int, default=6, help='with --analyse: FREQ_EVERY of the tablet (default 6)')
    args = ap.parse_args()

    if args.analyse:
        z = np.load(args.analyse)
        result = analyse({k: z[k] for k in z.files}, args.every)
        if result:
            print('\nSettings: ' + ', '.join(f'{k}={v}' for k, v in result.items()))
        return

    tablet = Tablet()
    if args.restore:
        if not os.path.exists(BACKUP):
            sys.exit('No backup file found, nothing to restore.')
        tablet.apply(json.load(open(BACKUP)))
        print('Settings restored.')
        return

    original, _ = tablet.read()
    json.dump(original, open(BACKUP, 'w'))
    devs = open_pen_streams()
    if not devs:
        sys.exit('No pen data interface found. Close OpenTabletDriver and try again.')
    every = original['FREQ_EVERY']

    measure = dict(original, TIP_ON=6000, TIP_OFF=6000, F_REST=6000, F_FULL=12000, PRESSURE_GRADED=1,
                   TIP_PRESS_CHECKS=1, TIP_RELEASE_CHECKS=1, TIP_INVALID_CHECKS=1)
    try:
        tablet.apply(measure)
        print('The tablet is now in measurement mode (temporary, nothing is saved). Bring the pen close and follow the steps.')
        rec = {name: guided(devs, f'{name}: {text}', secs=secs, settle=0.4 if name != 'taps' else 0.0) for name, text, secs in STEPS}
        np.savez(DATA, **rec)
        result = analyse(rec, every)
        if result is None:
            print('\nNot enough data. Settings put back, run the script again.')
            tablet.apply(original)
            return

        chosen = dict(original, **result)
        print('\nNew settings: ' + ', '.join(f'{k}={v}' for k, v in result.items()))
        tablet.apply(chosen)
        print('\nNow the test with the real settings.')
        rest = guided(devs, 'Test 1: rest the pen on the surface with NO force, and let it sit there.', secs=3.0)
        hold = guided(devs, 'Test 2: a LIGHT press held steady.', secs=3.0)
        taps = guided(devs, 'Test 3: tap the tablet 10 times like in osu! (do not count seconds, just tap and stop).', secs=8.0, settle=0.0)
        print(f'\nResting pen: the tip went down {episodes(rest[:, 1])} time(s) (it should be 0).')
        print(f'Held press: the tip went down {episodes(hold[:, 1])} time(s) (it should be 1).')
        print(f'Taps: the tip went down {episodes(taps[:, 1])} time(s) (you tapped about 10 times, a lot more means double clicks).')
        if input('\nSave these settings on the tablet? [y/N] ').strip().lower() == 'y':
            tablet.save(chosen)
            print('Saved.')
        else:
            tablet.apply(original)
            print('Not saved, the tablet is back to its old settings.')
    except BaseException:
        tablet.apply(original)
        print('\nStopped, the tablet is back to its old settings.')
        raise


if __name__ == '__main__':
    main()
