"""End-to-end process tests for the isolated prototype, using disposable databases."""
import os
from pathlib import Path
import select
import sqlite3
import subprocess
import sys
import tempfile
import unittest

BINARY = os.environ.get("JOT_NATIVE_BINARY", "/tmp/opencode/jot-native-build/jot-native-prototype")


def create_database(path):
    with sqlite3.connect(path) as db:
        db.executescript("""
            CREATE TABLE pad (id INTEGER PRIMARY KEY, content TEXT NOT NULL,
                              updated_at TEXT, dirty INTEGER NOT NULL DEFAULT 0, synced_at TEXT);
            CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT);
            INSERT INTO pad (id, content) VALUES (1, 'test note');
        """)


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="jot-cli-", dir="/tmp/opencode")
        self.directory = Path(self.temp.name)
        runtime = self.directory / "runtime"
        runtime.mkdir(mode=0o700)
        self.db = self.directory / "pad.db"
        create_database(self.db)
        self.env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                        QT_FORCE_STDERR_LOGGING="1", QT_QPA_PLATFORMTHEME="generic",
                        QT_ACCESSIBILITY="0", NO_AT_BRIDGE="1", GTK_USE_PORTAL="0",
                        XDG_RUNTIME_DIR=str(runtime),
                        XDG_CONFIG_HOME=str(self.directory / "config"))
        self.processes = []

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            process.stdout.close()
            process.stderr.close()
        self.temp.cleanup()

    def invoke(self, db, *args):
        return subprocess.run([BINARY, "--db", str(db), *args], env=self.env,
                              capture_output=True, text=True, timeout=10)

    def start(self, db):
        process = subprocess.Popen([BINARY, "--db", str(db), "--hidden"], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        ready, _, _ = select.select([process.stdout], [], [], 10)
        self.assertTrue(ready, "prototype startup timed out")
        self.assertEqual(process.stdout.readline().strip(), "ready")
        return process

    def test_single_instance_commands_and_path_alias(self):
        process = self.start(self.db)
        alias = self.directory / "alias.db"
        alias.symlink_to(self.db)
        hardlink = self.directory / "hardlink.db"
        os.link(self.db, hardlink)
        for path, command, expected in (
            (self.db, "show", "ok visible"),
            (alias, "toggle", "ok hidden"),
            (hardlink, "toggle", "ok visible"),
            (alias, "hide", "ok hidden"),
        ):
            result = self.invoke(path, command)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), expected)
            self.assertIsNone(process.poll())
        result = self.invoke(self.db, "quit")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "ok quitting")
        self.assertEqual(process.wait(timeout=5), 0)
        with sqlite3.connect(self.db) as db:
            self.assertEqual(db.execute("SELECT content FROM pad").fetchone()[0], "• test note")

    def test_different_databases_have_separate_instances(self):
        second = self.directory / "second.db"
        create_database(second)
        first_process = self.start(self.db)
        second_process = self.start(second)
        self.assertEqual(self.invoke(self.db, "quit").returncode, 0)
        self.assertEqual(first_process.wait(timeout=5), 0)
        self.assertIsNone(second_process.poll())
        self.assertEqual(self.invoke(second, "quit").returncode, 0)
        self.assertEqual(second_process.wait(timeout=5), 0)

    def test_protects_live_path_and_hardlinks(self):
        live = self.directory / "config" / "dev.jot.app" / "pad.db"
        live.parent.mkdir(parents=True)
        create_database(live)
        hardlink = self.directory / "hardlink.db"
        os.link(live, hardlink)
        for path in (live, hardlink):
            result = self.invoke(path, "show")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Refusing to open the live Jot database", result.stderr)
        with sqlite3.connect(live) as db:
            self.assertEqual(db.execute("SELECT content FROM pad").fetchone()[0], "test note")

    def test_missing_database_is_not_created(self):
        missing = self.directory / "missing.db"
        self.assertNotEqual(self.invoke(missing, "show").returncode, 0)
        self.assertFalse(missing.exists())

    def test_backup_includes_wal_and_never_overwrites(self):
        destination = self.directory / "backup.db"
        with sqlite3.connect(self.db) as writer:
            self.assertEqual(writer.execute("PRAGMA journal_mode=WAL").fetchone()[0], "wal")
            writer.execute("PRAGMA wal_autocheckpoint=0")
            writer.execute("UPDATE pad SET content = ?, dirty = 1 WHERE id = 1",
                           ("uncheckpointed 🖋\n",))
            writer.execute("INSERT INTO settings VALUES (?, ?)", ("test_key", "test_value"))
            writer.commit()
            self.assertTrue(Path(str(self.db) + "-wal").exists())
            result = self.invoke(self.db, "--backup-to", str(destination))
            self.assertEqual(result.returncode, 0, result.stderr)
            with sqlite3.connect(destination) as copy:
                self.assertEqual(copy.execute("PRAGMA quick_check").fetchone()[0], "ok")
                self.assertEqual(copy.execute("SELECT content, dirty FROM pad").fetchone(),
                                 ("uncheckpointed 🖋\n", 1))
                self.assertEqual(copy.execute("SELECT value FROM settings WHERE key = 'test_key'").fetchone()[0],
                                 "test_value")
            before = destination.read_bytes()
            result = self.invoke(self.db, "--backup-to", str(destination))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(destination.read_bytes(), before)
            self.assertEqual(writer.execute("SELECT content FROM pad").fetchone()[0],
                             "uncheckpointed 🖋\n")

    def test_create_only_initializes_new_disposable_database(self):
        target = self.directory / "new.db"
        result = self.invoke(target, "--create")
        self.assertEqual(result.returncode, 0, result.stderr)
        with sqlite3.connect(target) as db:
            self.assertEqual(db.execute("SELECT content, dirty FROM pad WHERE id=1").fetchone(), ("", 0))
            self.assertEqual(db.execute("SELECT value FROM settings WHERE key='tag_format'").fetchone()[0], "2")
            self.assertEqual(db.execute("PRAGMA quick_check").fetchone()[0], "ok")
        before = target.read_bytes()
        result = self.invoke(target, "--create")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_bytes(), before)
        self.assertEqual(target.stat().st_mode & 0o777, 0o600)

    def test_maintenance_does_not_need_display(self):
        target = self.directory / "headless.db"
        backup = self.directory / "headless-backup.db"
        env = dict(self.env, QT_QPA_PLATFORM="nonexistent-platform",
                   DISPLAY="", WAYLAND_DISPLAY="")
        for command in (
            [BINARY, "--db", str(target), "--create"],
            [BINARY, "--db", str(target), "--backup-to", str(backup)],
        ):
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
        with sqlite3.connect(backup) as db:
            self.assertEqual(db.execute("PRAGMA quick_check").fetchone()[0], "ok")
            self.assertEqual(db.execute("SELECT content FROM pad WHERE id=1").fetchone()[0], "")

    def test_create_and_backup_refuse_missing_live_path(self):
        live = self.directory / "config" / "dev.jot.app" / "pad.db"
        live.parent.mkdir(parents=True)
        alias_parent = self.directory / "live-dir-alias"
        alias_parent.symlink_to(live.parent, target_is_directory=True)
        alias = alias_parent / "pad.db"
        for path in (live, alias):
            self.assertNotEqual(self.invoke(path, "--create").returncode, 0)
            self.assertNotEqual(self.invoke(self.db, "--backup-to", str(path)).returncode, 0)
            self.assertFalse(live.exists())

    def test_restart_after_unclean_exit(self):
        first = self.start(self.db)
        first.kill()
        first.wait(timeout=5)
        second = self.start(self.db)
        self.assertEqual(self.invoke(self.db, "show").stdout.strip(), "ok visible")
        self.assertEqual(self.invoke(self.db, "quit").returncode, 0)
        self.assertEqual(second.wait(timeout=5), 0)

    def test_simultaneous_hardlink_start_has_one_server(self):
        hardlink = self.directory / "same-pad.db"
        os.link(self.db, hardlink)
        processes = []
        for path in (self.db, hardlink):
            process = subprocess.Popen([BINARY, "--db", str(path), "--hidden"],
                                       env=self.env, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            processes.append(process)
            self.processes.append(process)
        messages = []
        for process in processes:
            ready, _, _ = select.select([process.stdout], [], [], 10)
            self.assertTrue(ready, "prototype startup timed out")
            messages.append(process.stdout.readline().strip())
        self.assertEqual(messages.count("ready"), 1)
        primary = processes[messages.index("ready")]
        secondary = processes[1 - messages.index("ready")]
        self.assertEqual(secondary.wait(timeout=5), 0)
        self.assertIsNone(primary.poll())
        result = self.invoke(hardlink, "quit")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(primary.wait(timeout=5), 0)

    @unittest.skipUnless(os.environ.get("JOT_TEST_PRIVATE_BUS") == "1",
                         "tray tests require a disposable D-Bus session")
    def test_tray_protocol_and_menu_actions(self):
        process = self.start(self.db)
        service = f"org.kde.StatusNotifierItem-{process.pid}-1"

        def call(path, method, *arguments):
            result = subprocess.run(
                ["gdbus", "call", "--session", "--dest", service, "--object-path", path,
                 "--method", method, "--", *arguments], env=self.env,
                capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout

        item = call("/StatusNotifierItem", "org.freedesktop.DBus.Properties.GetAll",
                    "org.kde.StatusNotifierItem")
        self.assertIn("jot-native-prototype", item)
        self.assertIn("IconPixmap", item)
        self.assertIn("ToolTip", item)
        layout = call("/MenuBar", "com.canonical.dbusmenu.GetLayout", "0", "-1", "[]")
        self.assertNotIn("<<", layout)
        for label in ("Show", "Settings", "Quit"):
            self.assertIn(label, layout)
        call("/StatusNotifierItem", "org.kde.StatusNotifierItem.Activate", "0", "0")
        self.assertEqual(self.invoke(self.db, "toggle").stdout.strip(), "ok hidden")
        call("/MenuBar", "com.canonical.dbusmenu.Event", "2", "clicked", "<0>", "0")
        self.assertEqual(self.invoke(self.db, "toggle").stdout.strip(), "ok hidden")
        call("/MenuBar", "com.canonical.dbusmenu.Event", "3", "clicked", "<0>", "0")
        self.assertEqual(process.wait(timeout=5), 0)

    @unittest.skipUnless(os.environ.get("JOT_TEST_PRIVATE_BUS") == "1",
                         "tray tests require a disposable D-Bus session")
    def test_tray_registers_after_watcher_starts_and_restarts(self):
        process = self.start(self.db)
        expected = f"item:org.kde.StatusNotifierItem-{process.pid}-1"
        script = str(Path(__file__).with_name("fake_tray_watcher.py"))
        for _ in range(2):
            watcher = subprocess.Popen([sys.executable, script], env=self.env,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
            self.processes.append(watcher)
            readable, _, _ = select.select([watcher.stdout], [], [], 10)
            self.assertTrue(readable, "watcher startup timed out")
            self.assertEqual(watcher.stdout.readline().decode().strip(), "ready")
            readable, _, _ = select.select([watcher.stdout], [], [], 10)
            self.assertTrue(readable, "tray did not register")
            self.assertEqual(watcher.stdout.readline().decode().strip(), expected)
            watcher.terminate()
            watcher.wait(timeout=5)
        self.assertEqual(self.invoke(self.db, "quit").returncode, 0)
        self.assertEqual(process.wait(timeout=5), 0)


if __name__ == "__main__":
    unittest.main()
