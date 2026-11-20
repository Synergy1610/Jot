//! T-008/T-010: settings-panel support commands and tray setup.

use serde::Serialize;
use tauri::{
    image::Image,
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    AppHandle, Emitter, Manager, WebviewWindow,
};

const HOTKEY_SNIPPET: &str =
    r#"o.bind("SUPER + W", "Jot scratchpad", "jot toggle")"#;

fn config_path() -> std::path::PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
    std::path::PathBuf::from(home)
        .join(".config/hypr/bindings.lua")
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HotkeyInfo {
    pub snippet: &'static str,
    pub config_path: String,
    pub configured: bool,
}

/// T-008: the exact Lua binding line, the config file path, and whether the
/// config already contains a `jot toggle` binding.
#[tauri::command]
pub fn get_hotkey_info() -> HotkeyInfo {
    let path = config_path();
    let configured = std::fs::read_to_string(&path)
        .map(|text| text.contains("jot toggle"))
        .unwrap_or(false);
    HotkeyInfo {
        snippet: HOTKEY_SNIPPET,
        config_path: path.to_string_lossy().to_string(),
        configured,
    }
}

#[tauri::command]
pub fn open_config() -> Result<(), String> {
    let path = config_path();
    std::process::Command::new("xdg-open")
        .arg(&path)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("could not open config: {e}"))
}

/// Copy text to the Wayland clipboard via wl-copy.
#[tauri::command]
pub fn copy_to_clipboard(text: String) -> Result<(), String> {
    use std::io::Write;
    let mut child = std::process::Command::new("wl-copy")
        .stdin(std::process::Stdio::piped())
        .spawn()
        .map_err(|e| format!("could not start wl-copy: {e}"))?;
    if let Some(stdin) = child.stdin.as_mut() {
        stdin
            .write_all(text.as_bytes())
            .map_err(|e| format!("could not write to clipboard: {e}"))?;
    }
    child
        .wait()
        .map_err(|e| format!("wl-copy failed: {e}"))?;
    Ok(())
}

#[tauri::command]
pub fn get_db_path(app: AppHandle) -> Result<String, String> {
    let dir = app
        .path()
        .app_config_dir()
        .map_err(|e| e.to_string())?;
    Ok(dir.join("pad.db").to_string_lossy().to_string())
}

/// T-010: build the tray icon with Show / Settings / Quit.
pub fn setup_tray(app: &tauri::App) -> Result<(), Box<dyn std::error::Error>> {
    let show = MenuItem::with_id(app, "show", "Show", true, None::<&str>)?;
    let settings = MenuItem::with_id(app, "settings", "Settings", true, None::<&str>)?;
    let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&show, &settings, &quit])?;

    let icon = Image::from_bytes(include_bytes!("../icons/icon.png"))?;

    let _tray = TrayIconBuilder::new()
        .icon(icon)
        .menu(&menu)
        .show_menu_on_left_click(true)
        .on_menu_event(|app, event| match event.id().as_ref() {
            "show" => show_window(&app),
            "settings" => {
                show_window(&app);
                let _ = app.emit("jot://open-settings", ());
            }
            "quit" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            } = event
            {
                show_window(&tray.app_handle());
            }
        })
        .build(app)?;

    Ok(())
}

fn show_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        crate::commands::show_pad(app.clone()).ok();
        let _ = window.set_focus();
    }
}

/// Used by the tray fallback check (no-op if the window is missing).
pub fn _window_exists(window: Option<&WebviewWindow>) -> bool {
    window.is_some()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hotkey_info_has_canonical_snippet_and_config_path() {
        let info = get_hotkey_info();
        assert_eq!(info.snippet, HOTKEY_SNIPPET);
        assert!(info.config_path.ends_with(".config/hypr/bindings.lua"));
    }
}
