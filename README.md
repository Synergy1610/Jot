# Jot

A summoned scratchpad: one keystroke brings up a small pad, type, one keystroke dismisses it.

> **Scaffold status:** skeleton only. No feature is implemented yet. The placeholder
> page calls a Rust health command that opens SQLite, proving the shell is wired.

## Install (development prerequisites)

```
sudo pacman -S rust libxdo patchelf
corepack enable pnpm   # or use a Node version that provides pnpm
```

## Run

```
pnpm install
make dev        # starts Vite and the Tauri window together (pnpm tauri dev)
```

## Check

```
pnpm typecheck
```

## Build / package

```
pnpm build      # Tauri release build
makepkg -si     # Arch package — not functional until T-011
```

## Install (Arch package)

```
cd packaging
makepkg -si
sudo pacman -U jot-0.1.0-1-x86_64.pkg.tar.zst   # if built without -i
```

The package installs `jot`, a launcher entry, and an autostart entry. After installing, change the binding below from the dev path to `jot toggle`.

## Hotkey setup (Hyprland)

Add to `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + W", "Jot scratchpad", "jot toggle")
o.bind("SUPER + J", "Toggle window split", hl.dsp.layout("togglesplit")) -- Omarchy default
```

Apply with `hyprctl reload config-only`. The pad appears top-right; it has focus, and because Hyprland here focuses with the mouse we move the cursor into the pad on summon.

## CLI (T-003)

```
jot toggle  # show if hidden, hide if visible
jot show
jot hide
jot quit
```

`--hidden` starts the app without showing the window (autostart uses this).

## Startup files (T-004)

- Launcher entry: `~/.local/share/applications/jot.desktop` (TODO(T-011): `Exec=jot`)
- Autostart: `~/.config/autostart/jot.desktop` (TODO(T-011): `Exec=jot --hidden`)

## Known desktop limitation

On Hyprland/Wayland the compositor ignores GTK's "always on top" request, and Hyprland offers no always-above window rule. Jot therefore registers a Hyprland window rule at startup that floats the window and places it in the top-right corner with a 24px gap; the pad is raised whenever it is summoned by the hotkey, but another window can cover it until you summon it again. On X11 and Windows the normal always-on-top setting applies.

See `docs/PROJECT_SPEC.md` and `docs/PROJECT_PLAN.md` for the approved design and task map.
