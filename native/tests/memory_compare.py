"""Controlled, disposable-data PSS comparison of the Tauri and native builds.

Both binaries run hidden with a private HOME/config/runtime and a private D-Bus
session (run this script under dbus-run-session). Nothing touches live notes.
"""

import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

NATIVE = os.environ.get("JOT_NATIVE_BINARY", "/tmp/opencode/jot-native-prod-build/jot")
TAURI = os.environ.get("JOT_TAURI_BINARY", str(Path.home() / ".local/bin/jot"))


def pss_of(pid):
    try:
        for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
            if line.startswith("Pss:"):
                return int(line.split()[1])
    except OSError:
        pass
    return 0


def descendants(pid):
    result = []
    stack = [pid]
    while stack:
        current = stack.pop()
        try:
            children = Path(f"/proc/{current}/task/{current}/children").read_text().split()
        except OSError:
            continue
        for child in children:
            child = int(child)
            result.append(child)
            stack.append(child)
    return result


def measure(label, binary, seconds=6):
    root = Path(tempfile.mkdtemp(prefix=f"jot-mem-{label}-", dir="/tmp/opencode"))
    env = dict(
        os.environ,
        HOME=str(root),
        XDG_CONFIG_HOME=str(root / "config"),
        XDG_DATA_HOME=str(root / "data"),
        XDG_RUNTIME_DIR=str(root / "runtime"),
        QT_ACCESSIBILITY="0",
        NO_AT_BRIDGE="1",
        GTK_USE_PORTAL="0",
        # The test uses a private runtime directory, so point the Wayland
        # client at the real socket by absolute path.
        WAYLAND_DISPLAY="/run/user/1000/wayland-1",
    )
    (root / "runtime").mkdir(mode=0o700)
    (root / "config" / "dev.jot.app").mkdir(parents=True, exist_ok=True)
    (root / "data").mkdir(exist_ok=True)
    # Both applications join this compositor hidden, but neither may install
    # Hyprland rules: unset the instance signature for both.
    # Keep XDG_CURRENT_DESKTOP: GTK uses it to pick the Wayland backend. Only
    # the Hyprland instance signature is removed so no window rule is installed.
    for key in ("DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE"):
        env.pop(key, None)
    process = None
    try:
        process = subprocess.Popen(
            [binary, "--hidden"], env=env, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, start_new_session=True,
        )
        time.sleep(seconds)
        if process.poll() is not None:
            return None, f"{label} exited with {process.returncode}: {process.stderr.read().decode(errors='replace')[-500:]}"
        pids = [process.pid] + descendants(process.pid)
        values = {pid: pss_of(pid) for pid in pids}
        return sum(values.values()), values
    finally:
        if process is not None and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=5)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        shutil.rmtree(root, ignore_errors=True)


def main():
    results = {}
    for label, binary in (("tauri", TAURI), ("native", NATIVE)):
        if not Path(binary).is_file():
            print(f"missing binary for {label}: {binary}", file=sys.stderr)
            return 1
        total, detail = measure(label, binary)
        if total is None:
            print(f"FAILED {label}: {detail}", file=sys.stderr)
            return 1
        results[label] = total
        pids = detail if isinstance(detail, dict) else {}
        print(f"{label}: {total / 1024:.1f} MiB PSS across {len(pids)} processes")
        for pid, value in sorted(pids.items(), key=lambda item: -item[1])[:6]:
            if value:
                print(f"  pid {pid}: {value / 1024:.1f} MiB")
    if "tauri" in results and "native" in results:
        before, after = results["tauri"], results["native"]
        print(f"native uses {100 * (before - after) / max(before, 1):.1f}% less PSS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
