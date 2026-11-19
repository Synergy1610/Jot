mod cli;
mod commands;
mod db;
mod tray;
mod window;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    window::prepare_hyprland();

    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, args, _cwd| {
            cli::handle(&app, cli::parse(&args));
        }))
        .setup(|app| {
            let args: Vec<String> = std::env::args().collect();
            cli::handle(&app.handle(), cli::parse(&args));
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
