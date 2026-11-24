"""Observe prototype xdg-shell size hints on a private, non-rendering server."""
import os
from pathlib import Path
import re
import select
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
BINARY = os.environ.get("JOT_NATIVE_BINARY", "/tmp/opencode/jot-native-release-build/jot-native-prototype")


def run_inside():
    with tempfile.TemporaryDirectory(prefix="jot-wl-", dir="/tmp/opencode") as root:
        root = Path(root)
        xml = subprocess.check_output(
            ["pkg-config", "--variable=pkgdatadir", "wayland-protocols"], text=True
        ).strip() + "/stable/xdg-shell/xdg-shell.xml"
        header = root / "xdg-shell-server.h"
        protocol = root / "xdg-shell-protocol.c"
        probe = root / "probe"
        subprocess.run(["wayland-scanner", "server-header", xml, str(header)], check=True)
        subprocess.run(["wayland-scanner", "private-code", xml, str(protocol)], check=True)
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "wayland-server"], text=True
        ))
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-I" + str(root),
                        str(HERE / "wayland_hints_probe.c"), str(protocol),
                        "-o", str(probe), *flags], check=True)

        env = dict(os.environ, XDG_RUNTIME_DIR=str(root), XDG_CONFIG_HOME=str(root),
                   WAYLAND_DISPLAY="jot-test-wayland", QT_QPA_PLATFORM="wayland",
                   QT_QUICK_BACKEND="software", QT_QPA_PLATFORMTHEME="generic",
                   QT_ACCESSIBILITY="0", NO_AT_BRIDGE="1", GTK_USE_PORTAL="0",
                   GIO_USE_VFS="local")
        for key in ("HYPRLAND_INSTANCE_SIGNATURE", "DISPLAY", "AT_SPI_BUS_ADDRESS",
                    "JOT_NATIVE_EXPERIMENTAL_PLACEMENT"):
            env.pop(key, None)
        database = root / "sample.db"
        subprocess.run([BINARY, "--db", str(database), "--create"],
                       env=env, capture_output=True, check=True)
        server = subprocess.Popen([str(probe)], env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        app = None
        try:
            ready, _, _ = select.select([server.stdout], [], [], 10)
            if not ready or server.stdout.readline().strip() != "ready":
                raise RuntimeError("private Wayland server did not start")
            app = subprocess.Popen([BINARY, "--db", str(database)], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            ready, _, _ = select.select([server.stdout], [], [], 15)
            if not ready:
                raise RuntimeError("prototype did not map on private Wayland server")
            message = server.stdout.readline().strip()
            match = re.fullmatch(
                r"mapped app=jot-native-prototype min=(\d+)x(\d+) max=(\d+)x(\d+)", message
            )
            if not match:
                raise RuntimeError("unexpected probe result: " + message)
            min_w, min_h, max_w, max_h = map(int, match.groups())
            if min_w <= 0 or min_h <= 0 or (min_w, min_h) != (max_w, max_h):
                raise RuntimeError("window was not fixed-size at map: " + message)
            if server.poll() is not None:
                raise RuntimeError("private Wayland server exited unexpectedly")
            print(message)
            print("PASS: fixed xdg-shell hints were present at map")
        finally:
            if app is not None:
                if app.poll() is None:
                    app.terminate()
                try:
                    app.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=5)
                app.stdout.close()
                app.stderr.close()
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=5)
            server.stdout.close()
            server.stderr.close()


if __name__ == "__main__":
    if sys.argv[1:] == ["--inside"]:
        run_inside()
    else:
        sys.exit(subprocess.run(
            ["dbus-run-session", "--config-file=" + str(HERE / "private-dbus.conf"),
             "--", sys.executable, __file__, "--inside"], timeout=60
        ).returncode)
