mod cli;
mod commands;
mod db;
mod settings;
mod tray;
mod window;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    window::prepare_hyprland();

    tauri::Builder::default()
        .plugin(tauri_plugin_sql::Builder::new().add_migrations("sqlite:pad.db", db::migrations()).build())
        .plugin(tauri_plugin_single_instance::init(|app, args, _cwd| {
            cli::handle(&app, cli::parse(&args));
        }))
        .setup(|app| {
            settings::setup_tray(app)?;
            let args: Vec<String> = std::env::args().collect();
            cli::handle(&app.handle(), cli::parse(&args));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::health_check,
            commands::hide_pad,
            commands::show_pad,
            settings::get_hotkey_info,
            settings::open_config,
            settings::copy_to_clipboard,
            settings::get_db_path
        ])
        .run(tauri::generate_context!())
        .expect("error while running jot");
}
