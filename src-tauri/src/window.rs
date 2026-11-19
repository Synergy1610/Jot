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

    let rule = r#"hl.window_rule({ name="jot-placement", match={class="^jot$"}, float=true, move="(monitor_w-window_w-24) 24", no_initial_focus=true })"#;
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
