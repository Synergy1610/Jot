mod cli;
mod commands;
mod db;
mod tray;
mod window;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![commands::health_check])
        .run(tauri::generate_context!())
        .expect("error while running jot");
}
