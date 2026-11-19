//! T-002: window geometry and placement for the summoned pad.

use tauri::{PhysicalPosition, WebviewWindow};

const GAP: i32 = 24;

/// Hyprland ignores GTK's window-position and always-on-top requests, so on
/// Hyprland the compositor owns placement. Register a named window rule at
/// startup: float, pin the pad to the top-right with a 24px gap, and stop it
/// stealing focus when it first maps. Safe to call on every launch.
pub fn prepare_hyprland() {
    if std::env::var("HYPRLAND_INSTANCE_SIGNATURE").is_err() {
        return;
    }

    let rule = r#"hl.window_rule({ name="jot-placement", match={class="^jot$"}, float=true, move="(monitor_w-window_w-24) 24" })"#;
    match std::process::Command::new("hyprctl")
        .args(["eval", rule])
        .output()
    {
        Ok(output) if output.status.success() => {}
        Ok(output) => eprintln!(
            "jot: hyprctl rejected the window rule: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        ),
        Err(error) => eprintln!("jot: could not run hyprctl: {error}"),
    }
}

/// On Wayland a client cannot set input focus, so ask Hyprland to focus and
/// raise the pad when it is summoned.
pub fn focus_hyprland() {
    if std::env::var("HYPRLAND_INSTANCE_SIGNATURE").is_err() {
        return;
    }
    // Move the mouse into the pad so that, because Omarchy/Hyprland focus
    // follows the mouse, the pad gets focused immediately.
    let code = r#"local w=hl.get_window("class:jot"); if w then hl.dispatch(hl.dsp.focus({window=w})); hl.dispatch(hl.dsp.window.bring_to_top({window=w})); hl.dispatch(hl.dsp.cursor.move({x=w.at.x+w.size.x/2, y=w.at.y+w.size.y/2})) end"#;
    for attempt in 1..=6 {
        if let Err(error) = std::process::Command::new("hyprctl")
            .args(["eval", code])
            .output()
        {
            eprintln!("jot: could not run hyprctl: {error}");
            return;
        }
        // The compositor may not have mapped the window yet on early tries.
        std::thread::sleep(std::time::Duration::from_millis(100));
        if let Ok(output) = std::process::Command::new("hyprctl")
            .args(["activewindow", "-j"])
            .output()
        {
            let stdout = String::from_utf8_lossy(&output.stdout);
            if stdout.contains("\"class\": \"jot\"") || stdout.contains("\"class\":\"jot\"") {
                return;
            }
        }
        eprintln!("jot: focus attempt {attempt} did not land on the pad");
    }
}

/// Place the pad in the top-right corner of the monitor it is currently on,
/// leaving a fixed gap from the screen edges. Works on X11/Windows; on
/// Wayland the compositor rule above is the effective path.
pub fn position_top_right(window: &WebviewWindow) -> Result<(), String> {
    let monitor = window
        .current_monitor()
        .map_err(|e| e.to_string())?
        .ok_or_else(|| "no current monitor".to_string())?;
    let window_size = window.outer_size().map_err(|e| e.to_string())?;
    let monitor_pos = monitor.position();
    let monitor_size = monitor.size();

    let x = monitor_pos.x + monitor_size.width as i32 - window_size.width as i32 - GAP;
    let y = monitor_pos.y + GAP;

    window
        .set_position(PhysicalPosition::new(x, y))
        .map_err(|e| e.to_string())
}
