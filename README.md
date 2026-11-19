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

See `docs/PROJECT_SPEC.md` and `docs/PROJECT_PLAN.md` for the approved design and task map.
