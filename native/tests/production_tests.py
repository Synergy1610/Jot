"""End-to-end tests for the installable native build, using a disposable HOME.

The production binary is exercised with its real default database path, but
that path lives under a temporary XDG_CONFIG_HOME. The real live notes are
only ever read, to prove that this test does not change them.
"""

import hashlib
import os
from pathlib import Path
import select
import sqlite3
import subprocess
import sys
import tempfile

BINARY = os.environ.get("JOT_PRODUCTION_BINARY", "/tmp/opencode/jot-native-prod-build/jot")
LIVE_DB = Path.home() / ".config" / "dev.jot.app" / "pad.db"

LEGACY = (
    "archive line\n"
    "— Thu 19 Nov 2026 —\n"
    "\n"
    "first entry\n"
    "second  · 3 Feb 2021\n"
)
# This is also what the old JavaScript app produced: the divider migration
# appends a new tag before the separator upgrade runs, so a "·" tag that is no
# longer at end of line stays untouched. That is intentional parity.
EXPECTED = (
    "• archive line\n"
    "• first entry  | 19 Nov 2026\n"
    "• second  · 3 Feb 2021  | 19 Nov 2026"
)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest() if Path(path).exists() else None


def create_legacy_db(path):
    with sqlite3.connect(path) as db:
        db.executescript("""
            CREATE TABLE pad (id INTEGER PRIMARY KEY CHECK (id = 1), content TEXT NOT NULL DEFAULT '',
                              updated_at TEXT, dirty INTEGER NOT NULL DEFAULT 0, synced_at TEXT);
            CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT);
        """)
        db.execute("INSERT INTO pad (id, content) VALUES (1, ?)", (LEGACY,))


def main():
    if not Path(BINARY).is_file():
        print(f"production binary not found: {BINARY}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="jot-prod-", dir="/tmp/opencode") as root:
        root = Path(root)
        config = root / "config"
        app_dir = config / "dev.jot.app"
        app_dir.mkdir(parents=True)
        runtime = root / "runtime"
        runtime.mkdir(mode=0o700)
        db = app_dir / "pad.db"
        backup = app_dir / "pad.db.jot-native-backup.db"
        create_legacy_db(db)
        env = dict(
            os.environ,
            HOME=str(root),
            XDG_CONFIG_HOME=str(config),
            XDG_RUNTIME_DIR=str(runtime),
            QT_QPA_PLATFORM="offscreen",
            QT_QPA_PLATFORMTHEME="generic",
            QT_QUICK_BACKEND="software",
            QT_ACCESSIBILITY="0",
            NO_AT_BRIDGE="1",
            GTK_USE_PORTAL="0",
        )
        for key in ("WAYLAND_DISPLAY", "DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE",
                    "XDG_CURRENT_DESKTOP", "DESKTOP_SESSION"):
            env.pop(key, None)
        live_before = digest(LIVE_DB)
        processes = []

        def start():
            process = subprocess.Popen(
                [BINARY, "--hidden"], env=env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            processes.append(process)
            ready, _, _ = select.select([process.stdout], [], [], 15)
            assert ready, f"startup timed out: {process.stderr.read()}"
            line = process.stdout.readline().strip()
            assert line == "ready", f"unexpected startup output {line!r}: {process.stderr.read()}"
            return process

        def call(*args):
            result = subprocess.run([BINARY, *args], env=env, capture_output=True,
                                    text=True, timeout=15)
            assert result.returncode == 0, result.stderr
            return result.stdout.strip()

        try:
            first = start()
            with sqlite3.connect(db) as check:
                content = check.execute("SELECT content FROM pad WHERE id=1").fetchone()[0]
                assert content == EXPECTED, f"unexpected migrated content: {content!r}"
                assert check.execute(
                    "SELECT value FROM settings WHERE key='tag_format'"
                ).fetchone()[0] == "2"
            assert backup.exists(), "no safety backup was made before migration"
            with sqlite3.connect(backup) as check:
                assert check.execute("PRAGMA quick_check").fetchone()[0] == "ok"
                assert check.execute("SELECT content FROM pad WHERE id=1").fetchone()[0] == LEGACY
            backup_digest = digest(backup)

            assert call("toggle") == "ok visible"
            assert call("toggle") == "ok hidden"
            assert call("show") == "ok visible"
            assert call("hide") == "ok hidden"
            assert call("quit") == "ok quitting"
            assert first.wait(timeout=10) == 0

            second = start()
            assert digest(backup) == backup_digest, "backup changed on a later launch"
            with sqlite3.connect(db) as check:
                assert check.execute("SELECT content FROM pad WHERE id=1").fetchone()[0] == EXPECTED
            assert call("quit") == "ok quitting"
            assert second.wait(timeout=10) == 0
            assert digest(LIVE_DB) == live_before, "the real live notes were modified"
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                process.stdout.close()
                process.stderr.close()
    print("production migration and CLI: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
