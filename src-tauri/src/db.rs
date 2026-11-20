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

#[cfg(test)]
mod tests {
    use super::*;
    use rusqlite::Connection;

    fn setup(conn: &Connection) {
        for migration in migrations() {
            conn.execute_batch(migration.sql).unwrap();
        }
    }

    #[test]
    fn clear_and_undo_restore_exact_text() {
        let conn = Connection::open_in_memory().unwrap();
        setup(&conn);
        conn.execute(
            "INSERT INTO pad (id, content, dirty) VALUES (1, ?1, 0)",
            ["hello pad"],
        )
        .unwrap();

        // T-009 clear: stash the old text, empty the pad.
        conn.execute_batch(
            "INSERT INTO settings (key, value) VALUES ('last_cleared', 'hello pad');
             UPDATE pad SET content = '', dirty = 1 WHERE id = 1;",
        )
        .unwrap();
        let cleared: String = conn
            .query_row("SELECT content FROM pad WHERE id = 1", [], |r| r.get(0))
            .unwrap();
        assert_eq!(cleared, "");

        // T-009 undo: restore the stashed text.
        let saved: String = conn
            .query_row(
                "SELECT value FROM settings WHERE key = 'last_cleared'",
                [],
                |r| r.get(0),
            )
            .unwrap();
        conn.execute(
            "DELETE FROM settings WHERE key = 'last_cleared'",
            [],
        )
        .unwrap();
        conn.execute(
            "UPDATE pad SET content = ?1, dirty = 1 WHERE id = 1",
            [&saved],
        )
        .unwrap();
        let restored: String = conn
            .query_row("SELECT content FROM pad WHERE id = 1", [], |r| r.get(0))
            .unwrap();
        assert_eq!(saved, "hello pad");
        assert_eq!(restored, saved);
    }
}
