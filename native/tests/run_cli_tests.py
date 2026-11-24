"""Run the process tests with a private bus and private runtime directory."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    with tempfile.TemporaryDirectory(prefix="jot-test-runtime-", dir="/tmp/opencode") as runtime:
        os.chmod(runtime, 0o700)
        env = dict(os.environ, XDG_RUNTIME_DIR=runtime, JOT_TEST_PRIVATE_BUS="1",
                   QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="generic",
                   QT_ACCESSIBILITY="0", NO_AT_BRIDGE="1", GTK_USE_PORTAL="0")
        tests = str(Path(__file__).with_name("cli_tests.py"))
        return subprocess.run(["dbus-run-session", "--", sys.executable, tests, *sys.argv[1:]],
                              env=env, timeout=120).returncode


if __name__ == "__main__":
    sys.exit(main())
