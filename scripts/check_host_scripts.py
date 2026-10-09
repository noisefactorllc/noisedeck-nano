#!/usr/bin/env python3
"""Host-side regression checks for bin/nano.py and bin/flash.sh.

Exercised by host/test.c (so bin/test.sh and scripts/test run it too):
- bin/nano.py find_port(): /dev/cu.usbmodem* still wins on macOS, /dev/ttyACM* and
  /dev/ttyUSB* are the Linux fallbacks in that order, an explicit --port is passed
  through untouched, and the error names every supported glob. The glob is always
  mocked, so these checks are deterministic on any host.
- bin/flash.sh is executed end-to-end from an isolated temporary project copy
  (byte-identical to the repo's bin/flash.sh; arduino-cli and the project venv
  stubbed) with a sandboxed HOME, so the script's own port selection,
  boot_app0.bin discovery and failure handling run unmodified, and the
  repository's real .venv is never created, modified or removed:
  * with boot_app0.bin under ~/.arduino15 the stub esptool is invoked and
    receives that exact path;
  * with an empty HOME the script exits 1 with its own message before flashing.
Exits nonzero on any failure. No device or network access needed.
"""
import glob as globmod
import os
import re
import shutil
import subprocess
import sys
import tempfile
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bin"))

# bin/nano.py re-execs itself through PROJECT_VENV/bin/python at import time when
# that interpreter exists and the re-exec marker is not exactly the venv path.
# Pre-seed the marker with the venv path, which is nano.py's own "already
# re-executed" signal, so importing the module runs in this interpreter even when
# a project .venv exists; the module then records a harmless venv error instead.
os.environ["_NOISEDECK_NANO_VENV_REEXEC"] = str((ROOT / ".venv").resolve())

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


# --- bin/nano.py find_port (glob always mocked) ---------------------------------
NO_PORTS = {"/dev/cu.usbmodem*": [], "/dev/ttyACM*": [], "/dev/ttyUSB*": []}


def with_glob(fake):
    def fake_glob(pattern, *a, **k):
        return fake.get(pattern, [])
    return fake_glob


real_glob = globmod.glob
globmod.glob = with_glob({"/dev/cu.usbmodem*": ["/dev/cu.usbmodem101"]})
try:
    check("nano.find_port prefers usbmodem", nano.find_port(None) == "/dev/cu.usbmodem101")
finally:
    globmod.glob = real_glob

globmod.glob = with_glob({
    "/dev/cu.usbmodem*": [],
    "/dev/ttyACM*": ["/dev/ttyACM0", "/dev/ttyACM1"],
})
try:
    check("nano.find_port falls back to ttyACM (sorted)", nano.find_port(None) == "/dev/ttyACM0")
finally:
    globmod.glob = real_glob

globmod.glob = with_glob({
    "/dev/cu.usbmodem*": [],
    "/dev/ttyACM*": [],
    "/dev/ttyUSB*": ["/dev/ttyUSB2"],
})
try:
    check("nano.find_port falls back to ttyUSB", nano.find_port(None) == "/dev/ttyUSB2")
finally:
    globmod.glob = real_glob

globmod.glob = with_glob(NO_PORTS)
try:
    nano.find_port(None)
    check("nano.find_port exits when no port", False, "no SystemExit raised")
except SystemExit as e:
    msg = str(e)
    check("nano.find_port exits when no port",
          all(g in msg for g in ("/dev/cu.usbmodem*", "/dev/ttyACM*", "/dev/ttyUSB*")), msg)
finally:
    globmod.glob = real_glob

globmod.glob = real_glob
check("nano.find_port explicit --port passthrough", nano.find_port("/dev/ttyUSB7") == "/dev/ttyUSB7")

# --- bin/flash.sh: port-selection expression, executed in a fake dev dir ---------
flash = (ROOT / "bin" / "flash.sh").read_text()

with tempfile.TemporaryDirectory(prefix="nano-flash-port-") as td:
    dev = Path(td) / "dev"
    dev.mkdir()
    for name in ("cu.usbmodem99", "ttyACM0", "ttyUSB1"):
        (dev / name).touch()
    expr = re.search(r'PORT="\$\{PORT:-\$\((ls [^)]*)\)', flash).group(1)
    expr = expr.replace("/dev/", f"{dev}/")
    run = lambda: subprocess.run(  # noqa: E731
        ["bash", "-euo", "pipefail", "-c", f'ls {expr} 2>/dev/null | head -1 || true'],
        check=False, capture_output=True, text=True,
    ).stdout.strip()
    check("flash.sh port picks usbmodem over ttyACM/ttyUSB", run() == f"{dev}/cu.usbmodem99", run())
    os.remove(dev / "cu.usbmodem99")
    check("flash.sh port falls back to ttyACM", run() == f"{dev}/ttyACM0", run())
    os.remove(dev / "ttyACM0")
    check("flash.sh port falls back to ttyUSB", run() == f"{dev}/ttyUSB1", run())

# --- bin/flash.sh: executed end-to-end with stubbed toolchain --------------------
ARDUINO_STUB = r'''#!/usr/bin/env bash
case "$1 $2" in
  "core list") echo "esp32:esp32 3.3.11";;
  "lib list")  echo "XPowersLib 0.3.3";;
  compile*) echo "Sketch uses 916224 bytes (69%) of 1310720 bytes maximum";;
  *) echo "stub arduino-cli: $*" >&2; exit 1;;
esac
'''

PY_STUB = r'''#!/usr/bin/env bash
printf '%s\n' "$@" >> "$NANO_ESPTOOL_LOG"
echo "Wrote 400000 bytes"
echo "Hash of data is verified."
'''

REL = "packages/esp32/hardware/esp32/3.3.11/tools/partitions/boot_app0.bin"


def venv_snapshot():
    venv = ROOT / ".venv"
    if not venv.exists():
        return None
    snap = []
    for p in sorted(venv.rglob("*")):
        st = p.stat()
        snap.append((str(p.relative_to(venv)), st.st_size, st.st_mtime_ns))
    return snap


def run_flash(project: Path, home: Path, stubdir: Path, esptool_log: Path):
    env = dict(os.environ)
    env["HOME"] = str(home)
    env["PATH"] = f"{stubdir}:/usr/bin:/bin"
    env["NANO_ESPTOOL_LOG"] = str(esptool_log)
    env["PORT"] = "/dev/null"  # no board attached in a test container
    return subprocess.run(
        ["bash", str(project / "bin" / "flash.sh")],
        env=env, check=False, capture_output=True, text=True, cwd=str(project),
    )


with tempfile.TemporaryDirectory(prefix="nano-flash-e2e-") as td:
    tdp = Path(td)
    stubdir = tdp / "stub"
    stubdir.mkdir()
    (stubdir / "arduino-cli").write_text(ARDUINO_STUB)
    (stubdir / "arduino-cli").chmod(0o755)

    # Isolated project copy: the real bin/flash.sh bytes under a temp ROOT so the
    # script's hardcoded $ROOT/.venv resolves to a stub we own, never the repo's.
    project = tdp / "proj"
    (project / "bin").mkdir(parents=True)
    shutil.copy2(ROOT / "bin" / "flash.sh", project / "bin" / "flash.sh")
    check("flash.sh e2e: project copy is byte-identical to the repo script",
          (project / "bin" / "flash.sh").read_bytes() == (ROOT / "bin" / "flash.sh").read_bytes())
    (project / ".venv" / "bin").mkdir(parents=True)
    (project / ".venv" / "bin" / "python").write_text(PY_STUB)
    (project / ".venv" / "bin" / "python").chmod(0o755)

    linux_home = tdp / "linux"
    empty_home = tdp / "empty"
    for home in (linux_home, empty_home):
        home.mkdir()
    (linux_home / ".arduino15" / REL).parent.mkdir(parents=True)
    (linux_home / ".arduino15" / REL).touch()

    esptool_log = tdp / "esptool.log"
    before = venv_snapshot()

    # Success case: run the real bin/flash.sh; it must find boot_app0.bin in the
    # sandboxed ~/.arduino15 and hand that exact path to the stub esptool.
    r = run_flash(project, linux_home, stubdir, esptool_log)
    check("flash.sh e2e: succeeds with linux data dir", r.returncode == 0, r.stdout + r.stderr)
    args = esptool_log.read_text() if esptool_log.exists() else ""
    check("flash.sh e2e: esptool invoked", args.strip() != "", "stub was not called")
    want_boot = f"{linux_home / '.arduino15' / REL}"
    check("flash.sh e2e: esptool receives boot_app0 path",
          re.search(r"(?m)^0xe000\n" + re.escape(want_boot) + r"$", args) is not None,
          args)
    check("flash.sh e2e: hash-verified line reached", "Hash of data is verified." in r.stdout, r.stdout)

    # Failure case: empty HOME must exit 1 with the script's own message, and the
    # stub esptool must never be invoked.
    esptool_log.unlink(missing_ok=True)
    r = run_flash(project, empty_home, stubdir, esptool_log)
    check("flash.sh e2e: exits 1 when boot_app0 missing", r.returncode == 1, r.stdout + r.stderr)
    check("flash.sh e2e: missing-data-dir message",
          "boot_app0.bin not found under ~/.arduino15 or ~/Library/Arduino15" in r.stderr, r.stderr)
    args = esptool_log.read_text() if esptool_log.exists() else ""
    check("flash.sh e2e: esptool not invoked on failure", args.strip() == "", args)

    # The repository's own project environment must be untouched by the suite.
    after = venv_snapshot()
    check("flash.sh e2e: repo .venv preserved", before == after,
          "ROOT/.venv changed during the run" if before != after else "")

print(f"\n{'FAILURES: ' + ', '.join(failures) if failures else 'all host-script checks ok'}")
sys.exit(1 if failures else 0)
