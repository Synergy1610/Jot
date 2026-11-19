mod cli;
mod commands;
mod db;
mod tray;
mod window;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    window::prepare_hyprland();

    tauri::Builder::default()
        .setup(|app| {
            if let Some(window) = app.get_webview_window("main") {
                if let Err(error) = window::position_top_right(&window) {
                    eprintln!("jot: could not position window: {error}");
                }
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::health_check,
            commands::hide_pad,
            commands::show_pad
        ])
        .run(tauri::generate_context!())
        .expect("error while running jot");
}
