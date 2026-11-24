#!/usr/bin/env bash
# Switch the user-local Jot launcher from Tauri to the native build.
#
# This script is deliberately explicit: it does nothing without --confirm,
# keeps the old binary and a verified SQLite backup, and never edits the
# Hyprland hotkey configuration. Run it only after acceptance tests pass.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: install-native.sh --binary /path/to/jot --confirm

  --binary PATH   Native production binary built from native/desktop/jot.pro
  --confirm       Required; without it nothing is changed.

Rollback: copy ~/.local/bin/jot.tauri.bak back to ~/.local/bin/jot and
restore the SQLite backup printed by this script.
EOF
}

BINARY=""
CONFIRM=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --binary) BINARY="${2:-}"; shift 2 ;;
        --confirm) CONFIRM=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 1 ;;
    esac
done

if [ "$CONFIRM" -ne 1 ]; then
    echo "Refusing to switch without --confirm. Nothing was changed." >&2
    exit 1
fi
if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    echo "A readable, executable native binary is required: --binary PATH" >&2
    exit 1
fi

BIN_DIR="$HOME/.local/bin"
LIVE_DB="${XDG_CONFIG_HOME:-$HOME/.config}/dev.jot.app/pad.db"
OLD_BINARY="$BIN_DIR/jot"
BACKUP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/jot-native-backups"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP_DB="$BACKUP_DIR/pad-$STAMP.db"
BACKUP_BIN="$BACKUP_DIR/jot-tauri-$STAMP"

if [ ! -f "$LIVE_DB" ]; then
    echo "Live database not found: $LIVE_DB" >&2
    exit 1
fi
if pgrep -u "$(id -u)" -x jot >/dev/null 2>&1; then
    echo "Jot is running. Quit it before switching." >&2
    exit 1
fi
if [ ! -d "$BIN_DIR" ]; then
    echo "Missing user bin directory: $BIN_DIR" >&2
    exit 1
fi

mkdir -p "$BACKUP_DIR"

# Smoke test against a disposable database with an isolated runtime and bus.
SMOKE="$(mktemp -d /tmp/opencode/jot-switch-smoke-XXXXXX)"
trap 'rm -rf "$SMOKE"' EXIT
mkdir -p "$SMOKE/runtime" "$SMOKE/config"
clean_env() {
    env -u WAYLAND_DISPLAY -u DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE \
        -u XDG_CURRENT_DESKTOP -u DESKTOP_SESSION \
        HOME="$SMOKE" XDG_CONFIG_HOME="$SMOKE/config" \
        XDG_RUNTIME_DIR="$SMOKE/runtime" QT_QPA_PLATFORM=offscreen \
        QT_QPA_PLATFORMTHEME=generic QT_QUICK_BACKEND=software \
        QT_ACCESSIBILITY=0 NO_AT_BRIDGE=1 GTK_USE_PORTAL=0 "$@"
}
clean_env "$BINARY" --db "$SMOKE/pad.db" --create >/dev/null
clean_env "$BINARY" --db "$SMOKE/pad.db" --hidden >"$SMOKE/out" 2>"$SMOKE/err" &
PID=$!
for _ in $(seq 1 100); do
    if grep -qx ready "$SMOKE/out"; then break; fi
    if ! kill -0 "$PID" 2>/dev/null; then
        cat "$SMOKE/err" >&2
        echo "Native smoke test failed to start." >&2
        exit 1
    fi
    sleep 0.1
done
if ! grep -qx ready "$SMOKE/out"; then
    kill "$PID" 2>/dev/null || true
    echo "Native smoke test timed out." >&2
    exit 1
fi
clean_env "$BINARY" --db "$SMOKE/pad.db" quit >/dev/null
wait "$PID"

# Make a SQLite-consistent backup of the real notes before touching anything.
if ! clean_env "$BINARY" --db "$LIVE_DB" --backup-to "$BACKUP_DB"; then
    echo "Could not make a verified backup of the live notes; aborting." >&2
    exit 1
fi
python3 - "$BACKUP_DB" <<'PY'
import sqlite3, sys
with sqlite3.connect(sys.argv[1]) as db:
    assert db.execute("PRAGMA quick_check").fetchone()[0] == "ok"
    assert db.execute("SELECT content FROM pad WHERE id=1").fetchone() is not None
PY

cp -p "$OLD_BINARY" "$BACKUP_BIN"
install -m 755 "$BINARY" "$BIN_DIR/jot.new"
mv -f "$BIN_DIR/jot.new" "$OLD_BINARY"

install -Dm644 "$(dirname "$0")/jot.desktop" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/applications/jot.desktop"
install -Dm644 "$(dirname "$0")/jot-autostart.desktop" \
    "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/jot.desktop"
install -Dm644 "$(cd "$(dirname "$0")/../.." && pwd)/src-tauri/icons/128x128.png" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/128x128/apps/jot.png"

echo
echo "Native Jot installed."
echo "Previous binary: $BACKUP_BIN"
echo "Verified notes backup: $BACKUP_DB"
echo "Hotkey configuration was not changed."
echo "Rollback: cp -p '$BACKUP_BIN' '$OLD_BINARY'"
