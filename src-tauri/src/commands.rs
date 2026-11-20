//! Skeleton health check only: proves the Rust backend is alive and SQLite
//! can be opened. No application features are implemented here.

use crate::window;
use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Health {
    pub ok: bool,
    pub db: bool,
    pub version: &'static str,
}

#[tauri::command]
pub fn health_check(app: AppHandle) -> Result<Health, String> {
    let dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let conn = rusqlite::Connection::open(dir.join("health.db")).map_err(|e| e.to_string())?;
    let one: i64 = conn
        .query_row("SELECT 1", [], |row| row.get(0))
        .map_err(|e| e.to_string())?;

    let health = Health {
        ok: one == 1,
        db: one == 1,
        version: env!("CARGO_PKG_VERSION"),
    };

    // Skeleton-only: expose the result in the window title so the health
    // round-trip is observable from outside the webview.
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.set_title(&format!(
            "Jot | health {} | db {}",
            if health.ok { "OK" } else { "FAIL" },
            if health.db { "OK" } else { "FAIL" }
        ));
    }

    Ok(health)
}

/// T-002: Escape hides the pad without changing any text.
#[tauri::command]
pub fn hide_pad(app: AppHandle) -> Result<(), String> {
    let window = app
        .get_webview_window("main")
        .ok_or_else(|| "main window not found".to_string())?;
    window.hide().map_err(|e| e.to_string())
}

/// T-002/T-003: re-position on the current monitor, show and focus the pad.
#[tauri::command]
pub fn show_pad(app: AppHandle) -> Result<(), String> {
    let window = app
        .get_webview_window("main")
        .ok_or_else(|| "main window not found".to_string())?;
    // A hidden window has no "current monitor" on Wayland, so show first and
    // treat positioning as best-effort (Hyprland's window rule does the real
    // placement).
    window.show().map_err(|e| e.to_string())?;
    let _ = window::position_top_right(&window);
    window::focus_hyprland();
    // The webview may have been hidden since startup, so its DOM focus was
    // never established (or was lost). Ask the UI to re-focus the textarea.
    let _ = app.emit("jot://focus", ());
    Ok(())
}
