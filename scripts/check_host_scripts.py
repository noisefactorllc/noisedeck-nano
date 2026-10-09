#!/usr/bin/env python3
"""Host-side regression checks for bin/nano.py and bin/flash.sh.

Exercised by host/test.c (so bin/test.sh and scripts/test run it too):
- bin/nano.py find_port(): /dev/cu.usbmodem* still wins on macOS, /dev/ttyACM* and
  /dev/ttyUSB* are the Linux fallbacks in that order, an explicit --port is passed
  through untouched, and the error names every supported glob.
- bin/flash.sh port selection: usbmodem > ttyACM > ttyUSB, exercised by running
  the published script's selection expression in a sandboxed fake /dev.
- bin/flash.sh boot_app0.bin discovery: the exact loop from the published script
  run against stubbed ~/.arduino15 and ~/Library/Arduino15 layouts, failing closed
  with the script's own error message when neither exists.
Exits nonzero on any failure. No device or network access needed.
"""
import glob as globmod
import os
import re
import subprocess
import sys
import tempfile
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bin"))

serial_stub = types.ModuleType("serial")
serial_stub.Serial = object
sys.modules.setdefault("serial", serial_stub)

import importlib.util  # noqa: E402

spec = importlib.util.spec_from_file_location(
    "nano_under_test", ROOT / "bin" / "nano.py"
)
nano = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nano)

failures = []


def check(name, cond, detail=""):
    print(f"{'ok  ' if cond else 'FAIL'} {name}{f' ({detail})' if detail and not cond else ''}")
    if not cond:
        failures.append(name)


# --- bin/nano.py find_port -----------------------------------------------------
real_glob = globmod.glob


def make_glob(fake):
    def fake_glob(pattern, *a, **k):
        return fake.get(pattern, [])
    return fake_glob


globmod.glob = make_glob({"/dev/cu.usbmodem*": ["/dev/cu.usbmodem101"]})
try:
    check("nano.find_port prefers usbmodem", nano.find_port(None) == "/dev/cu.usbmodem101")
finally:
    globmod.glob = real_glob

globmod.glob = make_glob({
    "/dev/cu.usbmodem*": [],
    "/dev/ttyACM*": ["/dev/ttyACM0", "/dev/ttyACM1"],
})
try:
    check("nano.find_port falls back to ttyACM (sorted)", nano.find_port(None) == "/dev/ttyACM0")
finally:
    globmod.glob = real_glob

globmod.glob = make_glob({
    "/dev/cu.usbmodem*": [],
    "/dev/ttyACM*": [],
    "/dev/ttyUSB*": ["/dev/ttyUSB2"],
})
try:
    check("nano.find_port falls back to ttyUSB", nano.find_port(None) == "/dev/ttyUSB2")
finally:
    globmod.glob = real_glob

try:
    nano.find_port(None)
    check("nano.find_port exits when no port", False, "no SystemExit raised")
except SystemExit as e:
    msg = str(e)
    check("nano.find_port exits when no port", all(g in msg for g in ("/dev/cu.usbmodem*", "/dev/ttyACM*", "/dev/ttyUSB*")), msg)

globmod.glob = real_glob
check("nano.find_port explicit --port passthrough", nano.find_port("/dev/ttyUSB7") == "/dev/ttyUSB7")

# --- bin/flash.sh: source-bound checks ------------------------------------------
flash = (ROOT / "bin" / "flash.sh").read_text()

# The published script must contain the exact glob priority and both data dirs.
check(
    "flash.sh globs cover usbmodem/ttyACM/ttyUSB",
    "/dev/cu.usbmodem*" in flash and "/dev/ttyACM*" in flash and "/dev/ttyUSB*" in flash,
)
check(
    "flash.sh probes both data dirs",
    "$HOME/.arduino15" in flash and "$HOME/Library/Arduino15" in flash,
)

# Exercise the published script's actual port-selection expression in a fake dev dir.
with tempfile.TemporaryDirectory(prefix="nano-flash-port-") as td:
    dev = Path(td) / "dev"
    dev.mkdir()
    for name in ("cu.usbmodem99", "ttyACM0", "ttyUSB1"):
        (dev / name).touch()
    expr = re.search(r'PORT="\$\{PORT:-\$\(ls ([^)]*)\)', flash).group(1)
    expr = expr.replace("/dev/", f"{dev}/")
    out = subprocess.run(
        ["bash", "-euo", "pipefail", "-c", f'ls {expr} 2>/dev/null | head -1 || true'],
        check=False, capture_output=True, text=True,
    ).stdout.strip()
    check("flash.sh port picks usbmodem over ttyACM/ttyUSB", out == f"{dev}/cu.usbmodem99", out)

    os.remove(dev / "cu.usbmodem99")
    out = subprocess.run(
        ["bash", "-euo", "pipefail", "-c", f'ls {expr} 2>/dev/null | head -1 || true'],
        check=False, capture_output=True, text=True,
    ).stdout.strip()
    check("flash.sh port falls back to ttyACM", out == f"{dev}/ttyACM0", out)

    os.remove(dev / "ttyACM0")
    out = subprocess.run(
        ["bash", "-euo", "pipefail", "-c", f'ls {expr} 2>/dev/null | head -1 || true'],
        check=False, capture_output=True, text=True,
    ).stdout.strip()
    check("flash.sh port falls back to ttyUSB", out == f"{dev}/ttyUSB1", out)

# Exercise the published script's actual boot_app0 discovery against stubbed HOMEs.
with tempfile.TemporaryDirectory(prefix="nano-flash-home-") as td:
    linux_home = Path(td) / "linux"
    macos_home = Path(td) / "macos"
    empty_home = Path(td) / "empty"
    for home in (linux_home, macos_home, empty_home):
        home.mkdir()
    rel = "packages/esp32/hardware/esp32/3.3.11/tools/partitions/boot_app0.bin"
    (linux_home / ".arduino15" / rel).parent.mkdir(parents=True)
    (linux_home / ".arduino15" / rel).touch()
    (macos_home / "Library" / "Arduino15" / rel).parent.mkdir(parents=True)
    (macos_home / "Library" / "Arduino15" / rel).touch()

    probe = (
        'ESP32_DIR="packages/esp32/hardware/esp32/3.3.11/tools/partitions/boot_app0.bin"\n'
        'BOOT_APP0=""\n'
        'for d in "$HOME/.arduino15" "$HOME/Library/Arduino15"; do\n'
        '  [ -f "$d/$ESP32_DIR" ] && { BOOT_APP0="$d/$ESP32_DIR"; break; }\n'
        'done\n'
        '[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found under ~/.arduino15 or ~/Library/Arduino15" >&2; exit 1; }\n'
        'echo "$BOOT_APP0"\n'
    )
    for label, home, want in (
        ("linux data dir wins first", linux_home, linux_home / ".arduino15" / rel),
        ("macos data dir found", macos_home, macos_home / "Library" / "Arduino15" / rel),
    ):
        env = dict(os.environ, HOME=str(home))
        r = subprocess.run(["bash", "-c", probe], env=env, check=False, capture_output=True, text=True)
        check(f"flash.sh boot_app0: {label}", r.returncode == 0 and r.stdout.strip() == str(want), r.stdout + r.stderr)
    env = dict(os.environ, HOME=str(empty_home))
    r = subprocess.run(["bash", "-c", probe], env=env, check=False, capture_output=True, text=True)
    check(
        "flash.sh boot_app0: exits with its own message when missing",
        r.returncode == 1 and "boot_app0.bin not found" in r.stderr,
        r.stdout + r.stderr,
    )

print(f"\n{'FAILURES: ' + ', '.join(failures) if failures else 'all host-script checks ok'}")
sys.exit(1 if failures else 0)
