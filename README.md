# Jot

A scratchpad I can summon with one keystroke, type into, and dismiss with another.

> **V3 is final.** I shipped it and I'm done with it. I cancelled the Windows build — I only needed this on my Linux machine and my phone.

## What I built

| Where | What it is |
|---|---|
| **Linux desktop** | A Qt 6 / QML pad in `native/desktop`, linked against the shared Rust core in `native/core`. This is the one I use every day. |
| **Android** | A Kotlin / Compose app in `native/android`. I deliberately kept it free of JNI, Rust and the NDK — it talks to Supabase directly through OkHttp. |
| **Windows** | Cancelled. I decided I didn't need it. |

The desktop app keeps the behaviour I always had: it syncs to the cloud when I hide or close it. On the phone I wanted control, so nothing syncs by itself — I tap **Get latest** to pull the cloud copy down, or **Back up now** to push my phone notes up. Either way, I get a confirmation before one copy replaces another with unsaved text, and I never end up with dated duplicates.

## My Linux setup

I summon the pad with `Super + W`. In `~/.config/hypr/bindings.lua` I have:

```lua
o.bind("SUPER + W", "Jot scratchpad", "jot toggle")
```

Then I run `hyprctl reload config-only`. The pad floats at the top-right of whichever monitor has focus. Hyprland ignores the usual always-on-top request, so I register a window rule that floats the window and raises it whenever I summon it. Another window can still cover it until I summon it again — that's a Hyprland/Wayland limitation I worked around, not a bug in Jot.

I control the app from the command line too:

```sh
jot toggle   # show if hidden, hide if visible
jot show
jot hide
jot quit
```

`jot --hidden` starts it without showing the window; I use that for autostart.

### Building the desktop app

```sh
cd native/core && cargo build --release
cd ../desktop
qmake6 jot.pro
make -j4
install -Dm755 jot ~/.local/bin/jot
```

The window is a fixed 480×320. I had to keep Qt's fractional-DPI scaling exact, otherwise the text looked fuzzy when I moved the window between my laptop screen (1.333×) and my external monitor (1×).

## My phone

I publish the APK on the [V3 release page](https://github.com/Synergy1610/Jot/releases/tag/v3.0.0) and sideload it:

```sh
adb install -r jot-android-3.0.0.apk
```

It's debug-signed, so future updates from me have to use the same key. I keep the build and signing details in [`native/android/README.md`](native/android/README.md).

The phone saves my notes to a plain text file with atomic writes, so I never lose text to a half-finished write. It only touches the cloud when I ask it to, and the conflict prompts have saved me more than once.

## Where I put things

```
native/core/      Rust core shared with the desktop app
native/desktop/   Qt 6 / QML desktop app
native/android/   Kotlin / Compose phone app
docs/             The design spec and task history I worked from
packaging/        The Arch package files
```

I wrote up the approved design in `docs/PROJECT_SPEC.md` and the task history in `docs/PROJECT_PLAN.md`.
