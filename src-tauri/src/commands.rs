//! Skeleton health check only: proves the Rust backend is alive and SQLite
//! can be opened. No application features are implemented here.

use serde::Serialize;
use tauri::{AppHandle, Manager};

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

    Ok(Health {
        ok: one == 1,
        db: one == 1,
        version: env!("CARGO_PKG_VERSION"),
    })
}
