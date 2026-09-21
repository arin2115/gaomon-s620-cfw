"""Build the S620 firmware image.

  py build.py          ->  dist/s620.bin   (64 KB: the resident DFU bootloader, untouched, plus the firmware at 0x08004000)

Needs: Python 3 and `pip install ziglang` (the Zig compiler bundles clang for ARM).
Optional: `pip install capstone` prints the first instructions of the reset handler as a sanity check.
"""
import subprocess, sys, os, struct, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'src')
OUT = os.path.join(HERE, 'dist', 's620.bin')
SOURCES = ['startup.c', 'main.c', 'usb.c', 'emr.c', 'pentrack.c', 'pen.c', 'keys.c', 'settings.c']


def zig(*args):
    r = subprocess.run([sys.executable, '-m', 'ziglang', *args], cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(1)
    if r.stderr.strip():
        print(r.stderr)


os.makedirs(os.path.join(HERE, 'build'), exist_ok=True)
os.makedirs(os.path.join(HERE, 'dist'), exist_ok=True)
zig('cc', '-target', 'thumb-freestanding-eabi', '-mcpu=cortex_m4', '-mfloat-abi=soft', '-Os', '-ffreestanding',
    '-fno-builtin', '-nostdlib', '-fno-stack-protector', '-Wall', '-Wl,-T,link.ld', '-Wl,--gc-sections', '-Wl,-e,Reset_Handler',
    '-o', os.path.join(HERE, 'build', 'app.elf'), *SOURCES)
zig('objcopy', '-O', 'binary', os.path.join(HERE, 'build', 'app.elf'), os.path.join(HERE, 'build', 'app.bin'))

app = open(os.path.join(HERE, 'build', 'app.bin'), 'rb').read()
boot = open(os.path.join(HERE, 'bootloader', 'bootloader_16k.bin'), 'rb').read()
assert len(boot) == 0x4000
assert len(app) <= 0xB000, f'firmware too large: {len(app)} bytes'
img = bytearray(b'\xff' * 65536)                   # the chip has 64 KB of flash
img[:0x4000] = boot                                # resident DFU bootloader, byte for byte
img[0x4000:0x4000 + len(app)] = app
open(OUT, 'wb').write(bytes(img))

sp, rst = struct.unpack_from('<II', app, 0)
print(f'firmware {len(app)} bytes | initial SP 0x{sp:08X} | reset vector 0x{rst:08X}')
assert (sp & 0x2FFE0000) == 0x20000000, 'the bootloader requires the stack pointer to be in RAM'
assert 0x08004000 <= rst < 0x08004000 + len(app) and rst & 1
print('written', os.path.relpath(OUT, HERE))
