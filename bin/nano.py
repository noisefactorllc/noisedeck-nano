#!/usr/bin/env python3
"""Talk to a Noisedeck Nano over USB serial.

  nano.py up                         health JSON
  nano.py fx zone | pal magma | mood crit | auto 30 | bright 120 | speed 150 | shuffle
  nano.py text HELLO                 flash a message on the screen
  nano.py listen [seconds]           tail the device log (gestures, fps, shuffles)
  nano.py fleet [--interval 60] [URL ...]
                                     ops mood light: poll /up endpoints, set mood
                                     ok / warn (one down) / crit (two or more down)

Opening the port with default control-line settings does not reset the board;
explicitly toggling DTR/RTS does (the USB-JTAG maps them to reset), so this
script never touches them. Runs with the venv created by bin/flash.sh.
"""
import argparse
import glob
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

SCRIPT = Path(__file__).resolve()
PROJECT_VENV = SCRIPT.parent.parent / ".venv"
PROJECT_PYTHON = PROJECT_VENV / "bin" / "python"
REEXEC_MARKER = "_NOISEDECK_NANO_VENV_REEXEC"

reexec_attempted = os.environ.pop(REEXEC_MARKER, None) == str(PROJECT_VENV)
project_venv_active = Path(sys.prefix).resolve() == PROJECT_VENV.resolve()
project_venv_error = None
if not project_venv_active and reexec_attempted:
    project_venv_error = f"{PROJECT_PYTHON} did not activate {PROJECT_VENV}"
elif not project_venv_active and os.access(PROJECT_PYTHON, os.X_OK):
    child_env = os.environ.copy()
    child_env[REEXEC_MARKER] = str(PROJECT_VENV)
    try:
        os.execve(str(PROJECT_PYTHON), [str(PROJECT_PYTHON), str(SCRIPT), *sys.argv[1:]], child_env)
    except OSError as exc:
        project_venv_error = f"cannot start {PROJECT_PYTHON}: {exc}"

try:
    import serial
except ModuleNotFoundError as exc:
    if exc.name != "serial":
        raise
    setup = f"pyserial missing; run {SCRIPT.parent / 'flash.sh'} build to create the project venv"
    if project_venv_error:
        setup += f" ({project_venv_error})"
    sys.exit(setup)

DEFAULT_FLEET = [
    "https://noisedeck.app/up",
    "https://shuffleset.stream/up",
    "https://forum.noisefactor.io/up",
    "https://dashboard.noisefactor.io/up",
    "https://ehsre.noisefactor.io/up",
]


def find_port(explicit):
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port; is the board plugged in?")
    return ports[0]


def open_port(port):
    return serial.Serial(port, 115200, timeout=0.2)


def read_for(s, seconds):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        out += s.read(4096)
    return out.decode("utf-8", "replace")


def send(s, line, wait=0.6):
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    s.flush()
    return read_for(s, wait)


def check(url):
    try:
        with urllib.request.urlopen(url, timeout=8) as r:
            body = r.read(400)
            if r.status != 200:
                return False, f"http {r.status}"
            try:
                status = json.loads(body).get("status")
                return status == "ok", f"status={status}"
            except ValueError:
                return True, "200"
    except Exception as e:  # noqa: BLE001
        return False, type(e).__name__


def fleet(s, urls, interval):
    last = None
    while True:
        results = [(u, *check(u)) for u in urls]
        down = [u for u, ok, _ in results if not ok]
        mood = "ok" if not down else ("warn" if len(down) == 1 else "crit")
        stamp = time.strftime("%H:%M:%S")
        for u, ok, why in results:
            print(f"{stamp} {'ok  ' if ok else 'DOWN'} {u} ({why})")
        if mood != last:
            reply = send(s, f"mood {mood}").strip().splitlines()
            print(f"{stamp} mood -> {mood} ({reply[-1] if reply else 'no reply'})")
            last = mood
        time.sleep(interval)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--interval", type=int, default=60)
    ap.add_argument("words", nargs="*")
    a = ap.parse_args()
    port = find_port(a.port)
    words = a.words or ["up"]
    with open_port(port) as s:
        time.sleep(0.3)
        if words[0] == "listen":
            secs = float(words[1]) if len(words) > 1 else 30
            s.reset_input_buffer()
            end = time.time() + secs
            while time.time() < end:
                chunk = s.read(4096)
                if chunk:
                    sys.stdout.write(chunk.decode("utf-8", "replace"))
                    sys.stdout.flush()
            return
        if words[0] == "fleet":
            fleet(s, words[1:] or DEFAULT_FLEET, a.interval)
            return
        reply = send(s, " ".join(words))
        lines = [l for l in reply.splitlines() if l.strip() and not l.startswith("[nano] fps=")]
        print("\n".join(lines) if lines else "(no reply)")


if __name__ == "__main__":
    main()
