//! T-005: local SQLite schema (pad + settings) via tauri-plugin-sql.
//! The pad table is a single row per user in v1; dirty/synced_at columns
//! leave room for the M2 sync engine without a later migration.

use tauri_plugin_sql::{Migration, MigrationKind};

pub fn migrations() -> Vec<Migration> {
    vec![
        Migration {
            version: 1,
            description: "Create pad and settings tables",
            sql: "
                CREATE TABLE IF NOT EXISTS pad (
                    id        INTEGER PRIMARY KEY CHECK (id = 1),
                    content   TEXT    NOT NULL DEFAULT '',
                    updated_at TEXT,
                    dirty     INTEGER NOT NULL DEFAULT 0,
                    synced_at TEXT
                );
                CREATE TABLE IF NOT EXISTS settings (
                    key   TEXT PRIMARY KEY,
                    value TEXT
                );
            ",
            kind: MigrationKind::Up,
        },
    ]
}
