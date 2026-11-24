# Native Jot

A shared Rust storage core with two desktop builds:

- `jot-native-prototype` — the isolated test build. It requires `--db PATH`,
  refuses the live database path, and keeps experimental placement opt-in.
- `jot` — the installable build (`native/desktop/jot.pro`). It uses the same
  default path as the installed app, `~/.config/dev.jot.app/pad.db`, migrates
  legacy logs on first startup, and enables Hyprland placement by default.

The Linux switch has been performed with the guarded script: the installed
`~/.local/bin/jot` is now the native build, while the Tauri binary and a
verified SQLite backup are kept under `~/.local/share/jot-native-backups/`.
The Super+W binding was not changed.

## Interface

Settings fills the whole Jot window with a flat `#090809` surface, 16px
padding, no floating card, radius or dim overlay. Text uses the editor's
monospace 13px `#FAFCFB` typography; the Jot settings title and close cross
are 17px, section headings are 15px in the date-tag blue `#87a9b0`, and 1px
dividers separate sections. Black buttons are 34px tall with 8px
horizontal padding, 4px vertical padding and `#b59790` borders that brighten
on hover. It keeps
the Account, Hotkey (Linux / Hyprland), Clear pad and About sections and the
1.5s Copy feedback.

The header has a 1px divider, 16px side padding, a smaller gear icon, and
status badges with room to spare: the device badge is 36px wide and the cloud
badge 48px, with the spinner and tick anchored the same 3px gap to the right
of their icon. The cloud badge reserves a further tick slot at 18px, and the
gap between the badges is reduced to 4px. The gear is a circular 17px icon in
the same `#b59790` accent as the date, at full opacity; it sits inside an Item
wrapper so the Button cannot stretch it. Hovering either badge
shows a delayed, fully opaque black tooltip in the same monospace 13px font
(220ms delay, 4s timeout), clamped inside the window. The editor has 10px top
and 16px side padding below the header divider. The bottom status text is gone: the spinner
appears while saving and the tick when saved. Cloud spinner and tick
properties are present but hidden until cloud backup is enabled. Save
failures tint the device icon `#c38b7b` and show "Local save failed" in the
tooltip.

## Data compatibility

The Rust core uses the same SQLite `pad`/`settings` schema. `Pad::migrate_legacy`
applies the same one-time upgrades as the old JavaScript store, gated by the
`tag_format` setting: divider headers become coloured inline date tags, then
`·` separators become `|`. It is transactional and refuses to overwrite text
that changes during migration.

On the first production start, a SQLite-consistent backup is written to
`pad.db.jot-native-backup.db` and integrity checked before migration. Existing
backups are never overwritten, and a marker prevents a second backup. The C
entry point is `jot_core_migrate`; maintenance commands such as `--backup-to`
never migrate.

Saves compare the last loaded text with the database, so a conflicting edit
fails instead of overwriting it. Text that Qt cannot represent exactly
(including embedded NUL or unpaired UTF-16 surrogates) disables editing, with
a "Copy original notes" recovery action. Clear keeps one undo copy in the
same transaction. Legacy `·` tags remain highlighted and editable.

## Build

```sh
cargo build --manifest-path native/core/Cargo.toml
cargo build --release --manifest-path native/core/Cargo.toml

mkdir -p /tmp/opencode/jot-native-build
cd /tmp/opencode/jot-native-build
qmake6 /home/soorya/Projects/jot/native/desktop/jot-native.pro
make

mkdir -p /tmp/opencode/jot-native-prod-build
cd /tmp/opencode/jot-native-prod-build
qmake6 /home/soorya/Projects/jot/native/desktop/jot.pro
make
```

## Tests

```sh
cargo test --manifest-path native/core/Cargo.toml

mkdir -p /tmp/opencode/jot-native-test-build
cd /tmp/opencode/jot-native-test-build
qmake6 /home/soorya/Projects/jot/native/tests/native-tests.pro
make
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./jot-native-tests

python3 /home/soorya/Projects/jot/native/tests/run_cli_tests.py
python3 /home/soorya/Projects/jot/native/tests/run_wayland_probe.py
dbus-run-session --config-file=/home/soorya/Projects/jot/native/tests/private-dbus.conf \
  python3 /home/soorya/Projects/jot/native/tests/production_tests.py
dbus-run-session --config-file=/home/soorya/Projects/jot/native/tests/private-dbus.conf \
  python3 /home/soorya/Projects/jot/native/tests/memory_compare.py
```

`production_tests.py` exercises the installable binary against a disposable
`XDG_CONFIG_HOME`, verifies the safety backup and migrated content, runs the
`jot` CLI commands, and checks that the real live notes are unchanged.

`memory_compare.py` runs both builds hidden on the current Wayland session
with a private runtime and bus. On this machine it measured 223.3 MiB PSS for
Tauri (three processes) and 50.0 MiB PSS for native (one process), 77.6% less.

Placement was accepted on the live screen with a disposable database:
top-right logical placement at `(936, 24)` on a 1920x1200 monitor at 1.333
scale, focus on 5/5 and 3/3 production show/hide cycles, and a temporary
fullscreen window kept its fullscreen state. An isolated compositor test could
not be completed: headless Hyprland has no DRM allocator here, and Hyprland
0.56 requests `wl_compositor` v6 while the available Weston advertises v5.
Production enables placement by default; set
`JOT_NATIVE_EXPERIMENTAL_PLACEMENT=0` to opt out. The prototype still requires
`JOT_NATIVE_EXPERIMENTAL_PLACEMENT=1`.

## Packaging and switching

`native/packaging/` contains desktop entries, an autostart entry, an Arch
`PKGBUILD`, and `install-native.sh`. The switch script requires `--confirm`,
smoke-tests the binary, verifies a backup of the live notes, preserves the old
binary, and never edits the Hyprland hotkey configuration. It was first tested
against a disposable fake `HOME`, then used for the real switch. Rollback is
to copy the preserved Tauri binary back over `~/.local/bin/jot` and restore
the saved database backup.

## Remaining work

- Cloud setup needs credentials; signed-out local operation is complete.
- Windows and Android builds are planned around the shared core but not built.
- All native work is still untracked in git and has not been committed.
