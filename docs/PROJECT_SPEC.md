# Jot — Project Specification

**Status:** Approved
**Version:** 1.0
**Date:** 2026-11-19
**Project root:** `/home/soorya/Projects/jot`

---

## 1. Problem and goals

There is no frictionless place to capture a passing thought. Jot is a summoned scratchpad: one keystroke anywhere on the desktop brings up a small pad, you type, one keystroke dismisses it. Nothing to open, nothing to file, nothing to save. Text survives reboots and network outages, is backed up to the cloud, and is designed to follow the user to other devices in later versions.

### Goals for v1

- One keystroke to summon, one to dismiss; the pad never blocks other work.
- Typing is never lost and never requires an explicit save, with or without network.
- Every note is private to a single account, backed up to Supabase.
- Fully usable with no account at all.

### Definition of done for v1

The user can live with it for a day without ever thinking about saving, signing in, or where the text went.

## 2. Users and use cases

Single user (the project owner). No public signup, no sharing, no collaborators.

- **Jot a thought:** hotkey → type → hotkey. Dominant flow; target under one second from keystroke to focused cursor.
- **Look something up:** summon, scroll the log, dismiss.
- **Start over:** clear the pad, with a safety net.
- **New machine:** install, sign in, text appears.
- **Offline:** keep writing; sync catches up later.
- **Change the keybind:** open settings, follow the platform-appropriate instructions.

## 3. Functional requirements

### 3.1 Summon and dismiss

1. A global hotkey (default **Super+J**) invokes `jot toggle`. On Linux the binding is registered in the Hyprland config, not inside the app.
2. `jot toggle` shows the pad if hidden and hides it if visible. The app is single-instance; a second invocation toggles the existing instance and never spawns a second process.
3. **Escape** hides the pad. Escape never modifies text.
4. When shown, the pad appears at the **top-right of the focused monitor with a 24px gap**, approximately **480×320**, focused, with the cursor at the end of the text and ready to type.
5. While visible the pad is **always on top** and does not demand attention or steal focus from other applications.
6. When hidden, the pad is entirely gone from screen. A tray icon remains, and the hotkey is the only way to bring it back.
7. Tray menu: **Show**, **Settings**, **Quit**.
8. The app autostarts at login.
9. CLI commands: `jot toggle`, `jot show`, `jot hide`, `jot quit`.

### 3.2 Editing and autosave

10. Plain text only. No formatting, attachments, titles, or metadata in the text.
11. Every change is written to local storage within ~300ms. There is no save button and no dirty-state dialog.
12. **Approved change (single continuous pad):** the pad is **one editable textarea** holding the entire log — there is no hidden archive and no separate read-only header. When a line is completed with Enter, a small inline date tag is appended to it (e.g. `Prepare for Hackathon.  · 20 Nov 2026`), so every line carries the date it was written. All days remain visible and editable together, so you can cross things out or rewrite them in place. Old divider-formatted logs are migrated once into this format, tagging each line with the date of the section it appeared under; no text is lost. The `tag_format` setting records that migration has run.
13. The pad has a slim header showing the current date, a status indicator, and a gear icon that opens the settings panel. On Android the header is a Material 3 top app bar, and a one-line bottom bar holds the sync status plus **Get latest** and **Back up now**; the bar hides while the keyboard is open.
14. The header status is shown as two icon badges, not text: **computer + tick** for "saved on this device" and **cloud + tick** for "backed up to the cloud". Each has a hover tooltip explaining its meaning. In v1 the cloud badge is inactive with the tooltip "Cloud backup is off — sign in to enable"; it becomes active when M2 sync lands. Error and in-progress states are conveyed by the computer badge's colour/tick state and tooltip.

### 3.3 Settings panel

15. The gear icon opens a compact settings panel containing:
    - **Account** — email, sign in / sign out, current sync status.
    - **Hotkey** — platform-aware instructions (see 3.4).
    - **Clear pad** — the destructive action defined in 3.6.
    - **About** — app version and the location of the local database.

### 3.4 Hotkey configuration

16. The keybind is owned by the platform, and the settings panel must say so plainly rather than pretend otherwise.
17. **Linux:** the panel explains that the binding lives in the compositor (Hyprland), shows the exact line to add, the `jot toggle` command it invokes, a **Copy** button for that line, and an **Open config** button that opens `~/.config/hypr/hyprland.conf` in the user's editor. Changes take effect after a Hyprland reload, with no app restart.
18. **Linux:** the panel scans the Hyprland config for an existing `jot toggle` binding and reports **"Looks configured"** or **"Not found"**, so the user is never guessing.
19. **Windows:** cancelled by the owner. The in-app key-capture and packaging work is not planned; the Linux manual-instructions fallback is the only supported non-Linux path documented here.

### 3.5 Accounts

20. The app is fully functional with no account: the pad works, status reads **Signed out**, and no network calls are made.
21. Sign in with **email + password** through the settings panel. Sign out is available there too. Sessions persist across restarts and refresh automatically.
22. On the first link between a non-empty local pad and a non-empty cloud copy, **never silently overwrite**. Show a one-time choice between "keep this device" and "use the cloud copy".
23. The system is single-user: no public signup, no note sharing, no collaboration. The account is treated as private infrastructure, and email confirmation may be disabled in Supabase for that reason.

### 3.6 Clear pad

24. **Clear pad** wipes both the local document and the cloud copy, after an explicit confirmation dialog.
25. A single **Undo clear** action restores the text and re-syncs it.

### 3.7 Sync

26. When signed in, the document is pushed ~**1.5 seconds after typing stops**, and immediately on hide and on quit.
27. Failed pushes retry with exponential backoff. Status shows **Offline**, and no data is lost locally.
28. On startup and on sign-in, pull the cloud copy when there are no unsynced local changes.
29. A Supabase realtime subscription on the user's row is wired in v1 even though only one device exists, so v2 does not start from scratch.
30. Conflict rule for v1: **last write wins**, guarded server-side by `updated_at` so a stale write never overwrites a newer one. Multi-device conflict UX is a v2 decision and is explicitly deferred.

### 3.8 Appearance

31. Uniform look across all platforms: soft dark card, generous padding, comfortable reading-size text, subtle translucency where the desktop allows it.
32. A placeholder palette is acceptable for now. All colours and spacing live as **tokens in a single file**; components reference tokens only. No Omarchy theme coupling.

## 4. Out of scope for v1

Multiple notes, search, tags, titles, rich text, images, note sharing, public signup, note version history, encryption at rest beyond what Supabase provides, auto-update. Windows support and Windows in-app key capture (cancelled). Android support is delivered; see section 8.

Deferred by version:

- **v2 (shipped with V3):** Android app and cross-device sync semantics, including the conflict-resolution UX.
- **v3: Windows — cancelled by the owner.** No build, packaging or in-app key capture will be produced.

## 5. Data model and integrations

### 5.1 Supabase Postgres — `notes`

- `id uuid` primary key, `user_id uuid` referencing `auth.users`, `content text not null default ''`, `created_at timestamptz`, `updated_at timestamptz`.
- `unique (user_id)` — one pad per user in v1. The table shape leaves room for many rows later (drop the constraint, add a title column).
- **Row Level Security enabled.** Policies for select/insert/update/delete are restricted to `auth.uid() = user_id`.
- `updated_at` is set server-side on write.
- Migration SQL lives in `supabase/migrations/` so the schema is reproducible.

### 5.2 Credentials

- Supabase project URL and anon key are read from an env file and never committed.
- No service-role key ever ships in the application.

### 5.3 Local store

- SQLite via Tauri's SQL plugin: pad row, last-known sync timestamp, and settings.
- Chosen over a JSON file so that v2 multi-note support and a sync outbox require no migration.

### 5.4 Realtime

- A Supabase channel subscribed to changes on the user's own row.

## 6. Architecture and technical constraints

- **Stack:** Tauri v2, React, TypeScript, Vite, pnpm. Supabase accessed through `@supabase/supabase-js`.
- **Global hotkey on Hyprland:** implemented as a compositor binding that calls the app's CLI, because Wayland forbids apps from grabbing global keys. The required line is documented in the README and surfaced in the settings panel.
- **Packaging:** Arch `PKGBUILD` installing `jot`, a `.desktop` entry for the launcher/Walker, and an autostart entry. Uninstalled with pacman.
- **Known risk — always-on-top on Wayland:** the compositor may ignore an application's own always-on-top request. Mitigation: ship a recommended Hyprland `windowrulev2` snippet (`float`, pinned layer) as part of install documentation. This is verified during milestone 1 and reported honestly; if the app API is ignored, the window rule is the supported path.
- **Environment prerequisites:** Rust, Tauri CLI, a Supabase project. The JDK and Android SDK are needed only from v2. Docker is not available on this machine, so there is no local Supabase stack; schema is applied to the remote project via CLI or the SQL editor.

## 7. Security and privacy

- Notes are readable and writable only by their owner via RLS. The anon key is public by design and safe only because RLS is correct; policies are tested with a second account to prove cross-user access fails.
- Passwords are handled by Supabase Auth; the app never stores them.
- Session tokens live in webview local storage for v1. This is acceptable for a single-user desktop app; the OS keyring is a possible later hardening.
- The local SQLite database is unencrypted. This is accepted for a personal machine and stated plainly rather than glossed over.
- No analytics, no telemetry, and no third-party network calls other than Supabase.

## 8. Milestones

### M1 — Linux desktop, local only

Scaffold, window behaviour, Hyprland hotkey and CLI, tray, autostart, SQLite autosave, date dividers, settings panel with hotkey instructions, clear plus undo, placeholder theme tokens, PKGBUILD. No cloud integration yet.

### M2 — Cloud backup

Supabase project, migration with RLS, sign in and sign out, sync engine with debounce and retry, status line, realtime wiring, RLS verification with a second account.

### M3 — v2: Android and cross-device

Android app in Kotlin/Compose over the same Rust core, shipped as a sideload
APK attached to GitHub releases (no Play Store). Shares the desktop sync rules:
local-first autosave and no network while typing. Cloud transfer is explicit:
**Get latest** replaces the phone notes with the cloud copy, and **Back up
now** replaces the cloud copy with the phone notes. Jot confirms before
replacing either side when it has unsaved changes; nothing syncs in the
background. Distribution, minimum version and configuration are documented in
`native/android/README.md`.

### M4 — v3: Windows (cancelled)

Cancelled by the owner. No Windows package or in-app key capture will be built. V3 is final with Linux desktop and Android only.

## 9. Acceptance criteria for v1

- From a cold boot, Super+W puts a focused pad at the top-right in under a second.
- Type, press Escape, reboot, press Super+W: the text is intact.
- The read-only date header is present before typing and survives deleting all editable text.
- Disconnect the network, type, reconnect: status moves Offline → Syncing → Synced and nothing is lost.
- Signed out, the app makes no network requests and remains fully usable.
- Signing in on a fresh install pulls the cloud copy, or offers the one-time choice rather than overwriting.
- A second account cannot read, write, or delete the user's row (verified).
- Clear, confirm, undo restores the text.
- The settings panel shows working hotkey instructions and correctly reports whether the binding is configured.
- `makepkg -si` produces an installable package with launcher and autostart entries.

## 10. Assumptions and open questions

### Assumptions

- One user, one pad, one account, one personal machine.
- Super+W is the summon hotkey in the user's Hyprland config; collisions are resolved at install time.
- The user supplies the Supabase project URL and anon key.
- The pad is placed top-right on the focused monitor.

### Open questions

- Whether Hyprland honours always-on-top natively. To be resolved empirically in M1, not by assumption.
- Final palette values. A placeholder is accepted for now.
- Multi-device conflict UX. Deferred to v2 by agreement.

## 11. MVP summary

A summoned, always-on-top single pad on Linux that autosaves locally, can back up to Supabase behind email login, and installs as an Arch package. V3 adds Android with explicit cross-device sync. Windows was considered for v3 but cancelled.
