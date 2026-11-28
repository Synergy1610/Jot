# Jot

A summoned scratchpad: one keystroke brings up a small pad, type, one keystroke
dismisses it. Notes save locally; cloud transfer is explicit.

## V3 — final

This is the finished V3. There is no Windows build: it was cancelled by the
owner. Supported targets are:

- **Linux desktop** — Qt 6 / QML app in `native/desktop`, built against the
  shared Rust core in `native/core`. Installed as `jot` on this machine.
- **Android** — Kotlin / Compose app in `native/android`. It does **not** use
  the Rust core or NDK; it talks to Supabase directly through OkHttp. Sideload
  APK only, published on the GitHub releases page.

The desktop app keeps its original behaviour, including automatic cloud sync on
hide/quit. The phone never syncs by itself: **Get latest** replaces the phone
notes with the cloud copy, and **Back up now** replaces the cloud copy with the
phone notes. Both ask before replacing a copy with unsaved changes.

## Linux desktop

### Prerequisites

```
sudo pacman -S qt6 qt6-quickcontrols2 qt6-dbus
```

Rust (via `cargo`) is needed to build the shared core.

### Build and install

```sh
cd native/core && cargo build --release
cd ../desktop
qmake6 jot.pro
make -j4
sudo install -m755 jot /usr/local/bin/jot   # or: install -Dm755 jot ~/.local/bin/jot
```

The fixed pad window is 480×320. On a fractional-scaled monitor, Qt keeps the
exact scale (`PassThrough`) so text stays sharp when the window moves between
outputs with different DPI.

### Hotkey (Hyprland)

In `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + W", "Jot scratchpad", "jot toggle")
```

Apply with `hyprctl reload config-only`. The pad appears top-right on the
focused monitor. On Hyprland/Wayland the compositor ignores GTK's always-on-top
request, so Jot registers a window rule that floats the pad and raises it on
summon; another window can still cover it until you summon it again. On X11 the
normal always-on-top setting applies.

### CLI

```
jot toggle  # show if hidden, hide if visible
jot show
jot hide
jot quit
```

`--hidden` starts the app without showing the window (used by autostart).

## Android

Build, signing, cloud config and sync rules are documented in
[`native/android/README.md`](native/android/README.md). The release page hosts
a ready-to-install APK; updates must use the same signing key as the installed
build.

## Docs

See `docs/PROJECT_SPEC.md` for the approved design and `docs/PROJECT_PLAN.md`
for the task history.
