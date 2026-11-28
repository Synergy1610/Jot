# Jot — Project Plan

**Status:** Approved
**Date:** 2026-11-19
**Source specification:** `docs/PROJECT_SPEC.md` (approved v1.0)

---

## 1. Tech stack and architecture

### Stack

| Layer | Choice | Note |
|---|---|---|
| Shell | Tauri v2 | Rust backend, system webview |
| UI | React + TypeScript | Built with Vite |
| Package manager | pnpm | Broken under active Node 26.13.1; repaired in T-001 |
| Cloud | Supabase | Postgres + Auth + Realtime via `@supabase/supabase-js` |
| Local store | SQLite | `tauri-plugin-sql`; chosen so v2 multi-note needs no migration |
| Packaging | Arch `PKGBUILD` | Installs binary, desktop entry, autostart entry |

The approved specification fixes this stack, so it was not reopened.

Local environment at planning time: Node v26.13.1; `rustc`/`cargo` absent; pnpm only provided by Node 26.13.0; present and verified — `webkit2gtk-4.1 2.54.1`, `gtk3 3.24.52`, `libsoup3 3.8.0`, `librsvg`, `libayatana-appindicator`, `base-devel`, `openssl`, `git`, `makepkg`, `hyprctl`. Omarchy 4.5, Hyprland 0.56, no Waybar (quickshell shell).

### Architecture decisions

- **One window**, created at startup and hidden by default. The hotkey changes visibility only.
- **Single instance:** `tauri-plugin-single-instance` forwards a second launch's argv to the running process. Rust parses `toggle`, `show`, `hide`, `quit`.
- **Global hotkey belongs to the compositor.** Omarchy 4.5 uses Lua Hyprland config, so the copyable binding is `o.bind("SUPER + J", "Jot", "jot toggle")` in `~/.config/hypr/bindings.lua`. This corrects the spec's `hyprland.conf` / `bind =` wording; the spec will be updated when T-008 is built.
- **Local schema from day one:** `pad` (single row, `id = 1`, `content`, `updated_at`, `dirty`, `synced_at`) and `settings` (key/value). M2 adds sync behaviour without a local migration.
- **No state library.** React hooks plus a small status module holding `Saved | Syncing | Synced | Offline | Signed out | Error`.
- **Auth session** persisted by `supabase-js` in webview storage; credentials in a git-ignored `.env` (`VITE_SUPABASE_URL`, `VITE_SUPABASE_ANON_KEY`). The anon key is public by design; RLS is the security boundary.
- **Theme tokens** in a single module; components reference tokens only. No Omarchy theme coupling.
- **No telemetry**, and no third-party network calls beyond Supabase.

## 2. Time budget

**No time limit.** The user explicitly stated there is no time limit; the specification is silent on the subject. This plan therefore contains no budget math, no per-task minute estimates, and no deadline-based "must ship" flags. Sizing uses S / M / L only, and no approved feature is cut for time.

## 3. Milestones

### M1 — Local Linux app

Scaffold, window behaviour, CLI and single instance, Hyprland binding and autostart, SQLite autosave, date dividers, pad UI, settings panel, clear/undo, tray-or-fallback, Arch package.

**Done when:** with no account and no network, Super+W summons the pad; text survives restarts; the read-only divider header is always present before the text; settings reports binding status accurately; `makepkg -si` installs a working app.

### M2 — Cloud backup

Supabase project, migration with RLS, email/password auth, sync engine with debounce and retry, first-link safety, realtime wiring, RLS verification.

**Done when:** signing in backs up the pad; offline edits recover with nothing lost; a second account provably cannot touch your row.

### M3 — Docs, package verification, demo pass

README and install instructions, clean-install package check, walkthrough of every v1 acceptance criterion from the spec.

**Done when:** a fresh reader can install the app and bind the hotkey, and every acceptance criterion passes or is explicitly reported as failing.

## 4. Task breakdown

| ID | Title | Milestone | Dependencies | Size |
|---|---|---|---|---|
| T-001 | Toolchain setup + Tauri/React scaffold | M1 | none | S |
| T-002 | Window behaviour: size, top-right, always-on-top, Escape | M1 | T-001 | M |
| T-003 | CLI args + single instance + toggle | M1 | T-002 | M |
| T-004 | Hyprland binding docs, autostart, launcher wiring | M1 | T-003 | S |
| T-005 | SQLite store + 300ms debounced autosave | M1 | T-001 | M |
| T-006 | Daily date-divider logic | M1 | T-005 | S |
| T-007 | Pad UI + theme tokens | M1 | T-002, T-005 | M |
| T-008 | Settings panel: hotkey help/scan, about, account slot | M1 | T-004, T-007 | M |
| T-009 | Clear pad with confirm + undo | M1 | T-005, T-007 | S |
| T-010 | Tray menu or verified fallback | M1 | T-003, T-008 | S |
| T-011 | PKGBUILD, desktop/autostart entries | M1 | T-004, T-010 | M |
| T-012 | Supabase project + migration + RLS | M2 | T-001 | M |
| T-013 | Auth: sign in/out, session, signed-out mode | M2 | T-008, T-012 | M |
| T-014 | Sync engine: debounce, retry, pull, stale guard | M2 | T-005, T-013 | L |
| T-015 | First-link choice (device vs cloud) | M2 | T-014 | S |
| T-016 | Realtime subscription wiring | M2 | T-014 | S |
| T-017 | RLS verification with second account | M2 | T-012, T-014 | S |
| T-018 | Docs, packaging verification, demo pass | M3 | all | S |

### Task detail and acceptance criteria

**T-001 — Toolchain setup + Tauri/React scaffold** *(S, none)*
Install `rust`, `libxdo`, `patchelf`; repair pnpm under the active Node (corepack or matching mise version); scaffold Tauri v2 + React + TS + Vite; add `.gitignore` for `node_modules`, `target`, `.env`. No app features.
*Accept:* `cargo --version` and `pnpm --version` succeed; `pnpm tauri dev` opens a window; base structure is in place.

**T-002 — Window behaviour** *(M, T-001)*
480×320, top-right of the focused monitor with a 24px gap, always-on-top request, skip-taskbar, no focus stealing on creation, Escape hides the window without modifying text.
*Accept:* geometry correct on the focused monitor; pad floats above other windows; Escape hides it; the real Hyprland always-on-top behaviour is recorded; if the app API is ignored, a verified window-rule snippet for Hyprland 0.56 becomes the documented path.

**T-003 — CLI and single instance** *(M, T-002)*
Parse `toggle|show|hide|quit`; forward a second launch's argv to the running process; showing focuses the window and puts the cursor at the end of the text.
*Accept:* two `jot toggle` calls show then hide with exactly one process; `show`, `hide`, `quit` behave; no second process ever spawns.

**T-004 — Hyprland and startup wiring** *(S, T-003)*
Document and install the `o.bind("SUPER + J", "Jot", "jot toggle")` snippet for `bindings.lua`; autostart `.desktop` that starts hidden.
*Accept:* Super+J toggles the pad from a focused browser window; login leaves the app running hidden; docs match Omarchy 4.5's Lua config format.

**T-005 — Local store and autosave** *(M, T-001)*
SQLite migrations for `pad` and `settings`; load on startup; 300ms debounced writes; expose the database path for About. No cloud calls.
*Accept:* text survives kill/relaunch; writes are debounced rather than per keystroke; database path is available to the UI.

**T-006 — Date dividers (Option A)** *(S, T-005)*
Store `divider_date` in the local settings table; render `— Thu 19 Nov 2026 —` as a fixed, read-only header above the textarea; keep it out of the editable body. On a new day, archive the old section and start fresh; migrate legacy divider-after-text content without loss.
*Accept:* header is present before typing; it cannot be deleted from the editor; select-all/delete on the body leaves the header intact; legacy content is preserved; one header per day.

**T-007 — Pad UI and tokens** *(M, T-002, T-005)*
Header with date, status, gear; styled scrollable text area; focus cursor at end; empty-state placeholder; one token module for colour, spacing, radii, type.
*Accept:* coherent dark card; no colour literals outside tokens; typing and scrolling smooth; header does not wrap at 480px.

**T-008 — Settings panel** *(M, T-004, T-007)*
Overlay with Account (slot for M2), Hotkey (snippet, Copy, Open config, config scan), Clear pad entry, About (version, DB path).
*Accept:* Copy yields exactly `o.bind("SUPER + J", "Jot", "jot toggle")`; Open config opens `~/.config/hypr/bindings.lua`; scanning `~/.config/hypr/**/*.lua` reports "Looks configured" / "Not found" correctly; About shows real values.

**T-009 — Clear and undo** *(S, T-005, T-007)*
Confirmation dialog, clear the local document, single-step undo restoring the exact content.
*Accept:* cancel changes nothing; confirm empties the pad; undo restores byte-identical text and persists it; undo is unavailable after another edit.

**T-010 — Tray or verified fallback** *(S, T-003, T-008)*
Tray icon with Show / Settings / Quit, or a documented fallback if quickshell hosts no tray.
*Accept:* either the tray renders and its menu works, or the fallback (`jot quit` plus settings Quit) is verified and written into the README; quitting leaves no orphaned process.

**T-011 — Arch package** *(M, T-004, T-010)*
`PKGBUILD` producing a release build; installs `jot`, desktop entry, autostart entry, icon; install/uninstall documentation.
*Accept:* `makepkg -si` succeeds from a clean clone; app launches from the launcher; autostart survives reboot; pacman removal leaves no stale files.

**T-012 — Supabase schema** *(M, T-001)*
Migration: `notes` table, `unique(user_id)`, RLS policies, server-side `updated_at`; credentials into git-ignored `.env`. **Blocked until the user supplies Supabase URL and anon key.**
*Accept:* migration applied to the project; RLS enabled; anon key works only within policy; no service-role key anywhere.

**T-013 — Auth** *(M, T-008, T-012)*
Email/password sign in and out in settings, session restore, signed-out default.
*Accept:* session survives restart; sign-out clears it; signed out makes zero network requests and status reads "Signed out"; wrong password shows a clear error.

**T-014 — Sync engine** *(L, T-005, T-013)*
Push 1.5s after typing stops and on hide/quit; retry with exponential backoff; pull on startup/sign-in when clean; persist dirty state; server-side stale-write guard.
*Accept:* offline → type → online yields Offline → Syncing → Synced with nothing lost; killing the app mid-offline preserves pending state; a stale push cannot overwrite a newer row.

**T-015 — First-link safety** *(S, T-014)*
If both local and remote are non-empty and differ, offer "keep this device" / "use the cloud copy" before any write.
*Accept:* neither side is overwritten before the choice; each option produces the expected end state; the prompt does not reappear once resolved.

**T-016 — Realtime** *(S, T-014)*
Subscribe to the user's row; apply remote changes only when local is clean. Multi-device conflict UX remains v2 work.
*Accept:* an out-of-band row change is received; a dirty local pad is not clobbered; unsubscribe happens on sign-out.

**T-017 — RLS verification** *(S, T-012, T-014)*
Create a second account and attempt select/update/delete against the first user's row.
*Accept:* all three attempts fail; the result is documented; the first account is unaffected.

**T-018 — Docs and demo pass** *(S, all)*
README (install, hotkey setup, troubleshooting, known Wayland caveats), clean-install verification, full walkthrough of the spec's v1 acceptance criteria.
*Accept:* a fresh reader can install and bind the hotkey; every v1 acceptance criterion passes or is explicitly reported as failing.

## 5. Dependencies and setup

### Machine installs (T-001)

```
sudo pacman -S rust libxdo patchelf
corepack enable pnpm    # or align mise to the Node version that provides pnpm
```

Already installed: `webkit2gtk-4.1 2.54.1`, `gtk3 3.24.52`, `libsoup3 3.8.0`, `librsvg 2.63.3`, `libayatana-appindicator 0.6.1`, `base-devel`, `openssl 3.6.5`, `git`, `makepkg`.

### Environment

- `.env` (git-ignored): `VITE_SUPABASE_URL`, `VITE_SUPABASE_ANON_KEY` — user-supplied at T-012.
- No Docker locally, so no local Supabase stack; migrations are applied to the remote project.
- Hotkey config target: `~/.config/hypr/bindings.lua` (Omarchy 4.5 Lua config).
- `SUPER + J` verified currently unbound.

### Commands

```
pnpm install
pnpm tauri dev            # development
pnpm tauri build          # release build
makepkg -si               # package + install
jot toggle | show | hide | quit
```

## 6. Risks and assumptions

### Risks

1. **Always-on-top on Wayland.** Hyprland may ignore the app's request; T-002 records the truth and documents a verified window rule as fallback.
2. **No tray host.** Omarchy 4.5 uses a quickshell shell, not Waybar; a tray icon may never render. T-010 verifies and falls back to settings Quit plus `jot quit`.
3. **pnpm/mise version skew.** The active Node lacks pnpm; T-001 normalises the toolchain first.
4. **External Supabase setup.** Migration application needs project access; blocking dependency for T-012 and T-013.
5. **WebKitGTK focus quirks.** Showing the window may not focus the text area immediately; T-002 and T-007 handle explicit focus and cursor placement.
6. **Wayland global hotkey ownership.** The app cannot register one itself; the documented compositor binding is the only supported path, and it is user-editable.

### Assumptions

- One user, one pad, one account, one Linux machine.
- `SUPER + W` is the chosen summon hotkey (user rebound it); Omarchy's default Super+J binding is restored to "Toggle window split".
- The user supplies Supabase URL and anon key.
- Top-right placement targets the focused monitor.
- Placeholder palette is acceptable; final colours are later work.
- The settings-panel config path correction (Lua instead of `hyprland.conf`) will be applied to the spec when T-008 is built.
- Android (v2) shipped with V3; Windows (v3) is cancelled and out of scope.
