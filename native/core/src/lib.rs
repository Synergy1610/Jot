//! Shared storage core for the native Jot prototype.
//!
//! This deliberately opens an existing database rather than creating or
//! migrating one. The prototype must never silently replace a real pad.

use rusqlite::{params, Connection, OpenFlags, OptionalExtension};
use rusqlite::backup::{Backup, StepResult};
use std::ffi::{c_char, CStr, CString};
use std::fs::OpenOptions;
use std::path::Path;
use std::ptr;
use std::time::Duration;

const MONTHS: [&str; 12] = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
];

/// Whitespace helpers used by the small date-tag patterns below. These mirror
/// the regular expressions in src/lib/dateDividers.ts without needing a regex
/// dependency in the core.
fn split_ws(value: &str) -> (&str, &str) {
    let count = value.bytes().take_while(|b| b.is_ascii_whitespace()).count();
    (&value[..count], &value[count..])
}

fn split_digits(value: &str, max: usize) -> Option<(&str, &str)> {
    let count = value.bytes().take_while(|b| b.is_ascii_digit()).count();
    if count == 0 || count > max { return None; }
    Some((&value[..count], &value[count..]))
}

fn split_letters(value: &str, exact: usize) -> Option<(&str, &str)> {
    let count = value.bytes().take_while(|b| b.is_ascii_alphabetic()).count();
    if count != exact { return None; }
    Some((&value[..count], &value[count..]))
}

/// Parse "— Thu 19 Nov 2026 —" into (year, month, day).
fn parse_divider_line(line: &str) -> Option<(i32, u32, u32)> {
    let parts: Vec<&str> = line.trim().split(' ').collect();
    if parts.len() != 6 || parts[0] != "—" || parts[5] != "—" { return None; }
    if split_letters(parts[1], 3).is_none() { return None; }
    if split_digits(parts[2], 2).is_none() { return None; }
    if split_letters(parts[3], 3).is_none() { return None; }
    if split_digits(parts[4], 4).is_none() { return None; }
    let month = MONTHS.iter().position(|m| *m == parts[3])? as u32 + 1;
    Some((parts[4].parse().ok()?, month, parts[2].parse().ok()?))
}

fn inline_tag(year: i32, month: u32, day: u32) -> String {
    format!("| {} {} {}", day, MONTHS[(month - 1) as usize], year)
}

/// Match /\s*\|\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}\s*$/ on one line.
fn has_inline_tag(line: &str) -> bool {
    let Some(index) = line.rfind('|') else { return false; };
    let after = &line[index + 1..];
    let Ok(()) = (|| {
        let (gap, rest) = split_ws(after);
        if gap.is_empty() { return Err(()); }
        let (_, rest) = split_digits(rest, 2).ok_or(())?;
        let (gap, rest) = split_ws(rest);
        if gap.is_empty() { return Err(()); }
        let (_, rest) = split_letters(rest, 3).ok_or(())?;
        let (gap, rest) = split_ws(rest);
        if gap.is_empty() { return Err(()); }
        let (_, rest) = split_digits(rest, 4).ok_or(())?;
        let (_, rest) = split_ws(rest);
        if rest.is_empty() { Ok(()) } else { Err(()) }
    })() else { return false; };
    true
}

/// Locate /\s*·\s+(\d{1,2}\s+[A-Za-z]{3}\s+\d{4})\s*$/ and return the start of
/// the whole match plus the captured date text, as in upgradeTagSeparators.
fn old_tag_match(line: &str) -> Option<(usize, String)> {
    let index = line.rfind('·')?;
    let base = index + '·'.len_utf8();
    let after = &line[base..];
    let (gap, rest) = split_ws(after);
    if gap.is_empty() { return None; }
    let cap_start = base + gap.len();
    let (day, rest) = split_digits(rest, 2)?;
    let (day_gap, rest) = split_ws(rest);
    if day_gap.is_empty() { return None; }
    let (month, rest) = split_letters(rest, 3)?;
    let (month_gap, rest) = split_ws(rest);
    if month_gap.is_empty() { return None; }
    let (year, rest) = split_digits(rest, 4)?;
    let cap_end = cap_start + day.len() + day_gap.len()
        + month.len() + month_gap.len() + year.len();
    let (_, rest) = split_ws(rest);
    if !rest.is_empty() { return None; }
    let captured = line[cap_start..cap_end].to_string();
    let start = line[..index]
        .bytes()
        .rposition(|b| !b.is_ascii_whitespace())
        .map_or(0, |position| position + 1);
    Some((start, captured))
}

/// One-time divider-header to inline-tag upgrade. Matches
/// migrateDividersToTags in src/lib/dateDividers.ts, including trailing
/// newline normalization.
fn migrate_dividers_to_tags(text: &str) -> String {
    let mut out: Vec<String> = Vec::new();
    let mut current_tag: Option<String> = None;
    let mut skip_blank = false;
    for line in text.split('\n') {
        if let Some((year, month, day)) = parse_divider_line(line) {
            current_tag = Some(inline_tag(year, month, day));
            skip_blank = true;
            continue;
        }
        if line.trim().is_empty() {
            if !skip_blank { out.push(line.to_string()); }
            continue;
        }
        skip_blank = false;
        let mut value = line.to_string();
        if let Some(tag) = &current_tag {
            if !has_inline_tag(line) {
                value = format!("{}  {}", line.trim_end(), tag);
            }
        }
        out.push(value);
    }
    out.join("\n").trim_matches('\n').to_string()
}

/// One-time "·" to "|" separator upgrade. Matches upgradeTagSeparators.
fn upgrade_tag_separators(text: &str) -> String {
    text.split('\n')
        .map(|line| match old_tag_match(line) {
            Some((start, captured)) => format!("{}  | {}", &line[..start], captured),
            None => line.to_string(),
        })
        .collect::<Vec<_>>()
        .join("\n")
}

pub struct Pad {
    connection: Connection,
}

impl Pad {
    /// Initialize only a new database. An existing file is never opened or replaced.
    pub fn create(path: &Path) -> Result<(), String> {
        let mut options = OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let file = options.open(path)
            .map_err(|e| format!("Could not create a new database: {e}"))?;
        let result = (|| {
            let connection = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_WRITE)
                .map_err(|e| format!("Could not open the new database: {e}"))?;
            connection.execute_batch(
                "BEGIN IMMEDIATE;
                 CREATE TABLE pad (id INTEGER PRIMARY KEY CHECK (id = 1), content TEXT NOT NULL DEFAULT '', updated_at TEXT, dirty INTEGER NOT NULL DEFAULT 0, synced_at TEXT);
                 CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT);
                 INSERT INTO pad (id, content) VALUES (1, '');
                 INSERT INTO settings (key, value) VALUES ('tag_format', '2');
                 COMMIT;"
            ).map_err(|e| format!("Could not initialize the new database: {e}"))?;
            connection.close().map_err(|(_, e)| format!("Could not close the new database: {e}"))?;
            file.sync_all().map_err(|e| format!("Could not sync the new database: {e}"))?;
            Ok(())
        })();
        result.map_err(|e: String| format!("{e} The newly created file may be incomplete: {}", path.display()))
    }

    pub fn open(path: &Path) -> Result<Self, String> {
        let connection = Connection::open_with_flags(
            path,
            OpenFlags::SQLITE_OPEN_READ_WRITE | OpenFlags::SQLITE_OPEN_NO_MUTEX,
        )
        .map_err(|e| format!("Could not open pad database: {e}"))?;
        connection
            .busy_timeout(Duration::from_secs(3))
            .map_err(|e| e.to_string())?;
        let pad = Self { connection };
        pad.load()?;
        Ok(pad)
    }

    pub fn load(&self) -> Result<String, String> {
        self.connection
            .query_row("SELECT content FROM pad WHERE id = 1", [], |row| row.get(0))
            .map_err(|e| format!("Could not read pad: {e}"))
    }

    /// Create a new SQLite-consistent backup without overwriting any file.
    pub fn backup_to(&self, path: &Path) -> Result<(), String> {
        let mut options = OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let file = options.open(path)
            .map_err(|e| format!("Could not create a new backup: {e}"))?;
        let result = (|| {
            let mut destination = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_WRITE)
                .map_err(|e| format!("Could not open backup: {e}"))?;
            {
                let backup = Backup::new(&self.connection, &mut destination)
                    .map_err(|e| format!("Could not start SQLite backup: {e}"))?;
                match backup.step(-1).map_err(|e| format!("Could not copy SQLite backup: {e}"))? {
                    StepResult::Done => {}
                    _ => return Err("SQLite backup could not obtain a consistent snapshot; retry later.".into()),
                }
            }
            let check: String = destination.query_row("PRAGMA quick_check", [], |row| row.get(0))
                .map_err(|e| format!("Could not verify backup: {e}"))?;
            if check != "ok" {
                return Err(format!("Backup integrity check failed: {check}"));
            }
            let _: String = destination.query_row("SELECT content FROM pad WHERE id = 1", [], |row| row.get(0))
                .map_err(|e| format!("Backup does not contain the pad: {e}"))?;
            destination.close().map_err(|(_, e)| format!("Could not close backup: {e}"))?;
            file.sync_all().map_err(|e| format!("Could not sync backup: {e}"))?;
            Ok(())
        })();
        result.map_err(|e: String| format!("{e} The newly created backup file may be incomplete: {}", path.display()))
    }

    /// Write only if the database still contains the version we loaded.
    /// This prevents another running instance's edits from being overwritten.
    pub fn save(&self, expected: &str, content: &str) -> Result<(), String> {
        let changed = self.connection.execute(
            "UPDATE pad SET content = ?1, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), dirty = 1 WHERE id = 1 AND content = ?2",
            params![content, expected],
        ).map_err(|e| format!("Could not save pad: {e}"))?;
        if changed == 0 {
            return Err("The pad changed in another process; your edits were not saved. Copy them before reloading.".into());
        }
        Ok(())
    }

    pub fn clear(&self, expected: &str, current: &str) -> Result<(), String> {
        let transaction = self.connection.unchecked_transaction()
            .map_err(|e| format!("Could not start clear: {e}"))?;
        let changed = transaction.execute(
            "UPDATE pad SET content = '', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), dirty = 1 WHERE id = 1 AND content = ?1",
            params![expected],
        ).map_err(|e| format!("Could not clear pad: {e}"))?;
        if changed != 1 {
            return Err("The pad changed in another process; clear was cancelled.".into());
        }
        transaction.execute(
            "INSERT INTO settings (key, value) VALUES ('last_cleared', ?1) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            params![current],
        ).map_err(|e| format!("Could not keep undo copy: {e}"))?;
        transaction.commit().map_err(|e| format!("Could not commit clear: {e}"))
    }

    pub fn last_cleared(&self) -> Result<Option<String>, String> {
        self.connection.query_row(
            "SELECT value FROM settings WHERE key = 'last_cleared'",
            [], |row| row.get(0),
        ).optional().map_err(|e| format!("Could not read undo copy: {e}"))
    }

    pub fn undo_clear(&self, backup: &str) -> Result<(), String> {
        let transaction = self.connection.unchecked_transaction()
            .map_err(|e| format!("Could not start undo: {e}"))?;
        let changed = transaction.execute(
            "UPDATE pad SET content = ?1, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), dirty = 1 WHERE id = 1 AND content = '' AND EXISTS (SELECT 1 FROM settings WHERE key = 'last_cleared' AND value = ?1)",
            params![backup],
        ).map_err(|e| format!("Could not undo clear: {e}"))?;
        if changed != 1 {
            return Err("The pad or undo copy changed; undo was cancelled.".into());
        }
        transaction.execute("DELETE FROM settings WHERE key = 'last_cleared'", [])
            .map_err(|e| format!("Could not remove undo copy: {e}"))?;
        transaction.commit().map_err(|e| format!("Could not commit undo: {e}"))
    }

    /// Apply the same one-time log upgrades as the old JavaScript store:
    /// divider headers become inline tags, then "·" separators become "|".
    /// The `tag_format` setting gates each step, so this is safe to call on
    /// every startup. Returns true when something was upgraded.
    pub fn migrate_legacy(&self) -> Result<bool, String> {
        let transaction = self.connection.unchecked_transaction()
            .map_err(|e| format!("Could not start migration: {e}"))?;
        let original: String = transaction.query_row(
            "SELECT content FROM pad WHERE id = 1", [], |row| row.get(0),
        ).map_err(|e| format!("Could not read pad for migration: {e}"))?;
        let format: Option<String> = transaction.query_row(
            "SELECT value FROM settings WHERE key = 'tag_format'", [], |row| row.get(0),
        ).optional().map_err(|e| format!("Could not read tag format: {e}"))?;

        let mut content = original.clone();
        let mut upgraded = false;
        let write_setting = |transaction: &rusqlite::Transaction<'_>, value: &str| -> Result<(), String> {
            transaction.execute(
                "INSERT INTO settings (key, value) VALUES ('tag_format', ?1) \
                 ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                params![value],
            ).map_err(|e| format!("Could not record tag format: {e}"))?;
            Ok(())
        };

        let mut next = format.clone().unwrap_or_else(|| "1".to_string());
        if format.is_none() {
            content = migrate_dividers_to_tags(&content);
            write_setting(&transaction, "1")?;
            upgraded = true;
            next = "1".to_string();
        }
        if next != "2" {
            content = upgrade_tag_separators(&content);
            write_setting(&transaction, "2")?;
            upgraded = true;
        }
        if upgraded {
            let changed = transaction.execute(
                "UPDATE pad SET content = ?1,
                     updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), dirty = 1
                 WHERE id = 1 AND content = ?2",
                params![content, original],
            ).map_err(|e| format!("Could not save migrated pad: {e}"))?;
            if changed != 1 {
                return Err("The pad changed while it was being upgraded; migration was cancelled.".into());
            }
        }
        transaction.commit().map_err(|e| format!("Could not commit migration: {e}"))?;
        Ok(upgraded)
    }
}

fn into_c_string(value: &str) -> *mut c_char {
    // SQLite strings can contain NUL; report that rather than truncating notes.
    CString::new(value).map_or(ptr::null_mut(), CString::into_raw)
}

unsafe fn input<'a>(value: *const c_char) -> Result<&'a str, String> {
    if value.is_null() {
        return Err("Null string passed to the storage core".into());
    }
    CStr::from_ptr(value)
        .to_str()
        .map_err(|_| "Invalid UTF-8 passed to the storage core".into())
}

unsafe fn set_error(error: *mut *mut c_char, message: &str) {
    if !error.is_null() {
        *error = into_c_string(message);
    }
}

/// All returned strings belong to the caller and must be freed with
/// `jot_core_string_free`. On failure the error output receives a message.
#[no_mangle]
pub unsafe extern "C" fn jot_core_create(path: *const c_char, error: *mut *mut c_char) -> bool {
    if !error.is_null() { *error = ptr::null_mut(); }
    let result = input(path).and_then(|path| Pad::create(Path::new(path)));
    match result {
        Ok(()) => true,
        Err(message) => { set_error(error, &message); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_open(path: *const c_char, error: *mut *mut c_char) -> *mut Pad {
    if !error.is_null() {
        *error = ptr::null_mut();
    }
    let result = input(path).and_then(|path| Pad::open(Path::new(path)));
    match result {
        Ok(pad) => Box::into_raw(Box::new(pad)),
        Err(message) => {
            set_error(error, &message);
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_load(pad: *mut Pad, error: *mut *mut c_char) -> *mut c_char {
    if !error.is_null() {
        *error = ptr::null_mut();
    }
    let result = pad.as_ref().ok_or("Pad is not open".to_owned()).and_then(Pad::load);
    match result {
        Ok(content) => {
            let value = into_c_string(&content);
            if value.is_null() {
                set_error(error, "Pad contains a NUL character; refusing to truncate it");
            }
            value
        }
        Err(message) => {
            set_error(error, &message);
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_save(
    pad: *mut Pad,
    expected: *const c_char,
    content: *const c_char,
    error: *mut *mut c_char,
) -> bool {
    if !error.is_null() {
        *error = ptr::null_mut();
    }
    let result = pad
        .as_ref()
        .ok_or("Pad is not open".to_owned())
        .and_then(|pad| pad.save(input(expected)?, input(content)?));
    match result {
        Ok(()) => true,
        Err(message) => {
            set_error(error, &message);
            false
        }
    }
}

/// Apply one-time legacy log upgrades. Safe to call on every startup; the
/// tag_format setting makes repeated calls no-ops.
#[no_mangle]
pub unsafe extern "C" fn jot_core_migrate(pad: *mut Pad, error: *mut *mut c_char) -> bool {
    if !error.is_null() { *error = ptr::null_mut(); }
    let result = pad.as_ref()
        .ok_or("Pad is not open".to_owned())
        .and_then(Pad::migrate_legacy);
    match result {
        Ok(_) => true,
        Err(message) => { set_error(error, &message); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_backup(
    pad: *mut Pad, path: *const c_char, error: *mut *mut c_char,
) -> bool {
    if !error.is_null() { *error = ptr::null_mut(); }
    let result = pad.as_ref().ok_or("Pad is not open".to_owned())
        .and_then(|pad| pad.backup_to(Path::new(input(path)?)));
    match result {
        Ok(()) => true,
        Err(message) => { set_error(error, &message); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_clear(
    pad: *mut Pad, expected: *const c_char, current: *const c_char, error: *mut *mut c_char,
) -> bool {
    if !error.is_null() { *error = ptr::null_mut(); }
    let result = pad.as_ref().ok_or("Pad is not open".to_owned())
        .and_then(|pad| pad.clear(input(expected)?, input(current)?));
    match result {
        Ok(()) => true,
        Err(message) => { set_error(error, &message); false }
    }
}

/// Null without an error means there is no undo copy.
#[no_mangle]
pub unsafe extern "C" fn jot_core_last_cleared(
    pad: *mut Pad, error: *mut *mut c_char,
) -> *mut c_char {
    if !error.is_null() { *error = ptr::null_mut(); }
    let result = pad.as_ref().ok_or("Pad is not open".to_owned())
        .and_then(Pad::last_cleared);
    match result {
        Ok(Some(content)) => {
            let value = into_c_string(&content);
            if value.is_null() { set_error(error, "Undo copy contains a NUL character; refusing to truncate it"); }
            value
        }
        Ok(None) => ptr::null_mut(),
        Err(message) => { set_error(error, &message); ptr::null_mut() }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_undo_clear(
    pad: *mut Pad, backup: *const c_char, error: *mut *mut c_char,
) -> bool {
    if !error.is_null() { *error = ptr::null_mut(); }
    let result = pad.as_ref().ok_or("Pad is not open".to_owned())
        .and_then(|pad| pad.undo_clear(input(backup)?));
    match result {
        Ok(()) => true,
        Err(message) => { set_error(error, &message); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_close(pad: *mut Pad) {
    if !pad.is_null() {
        drop(Box::from_raw(pad));
    }
}

#[no_mangle]
pub unsafe extern "C" fn jot_core_string_free(value: *mut c_char) {
    if !value.is_null() {
        drop(CString::from_raw(value));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Pad {
        let connection = Connection::open_in_memory().unwrap();
        connection.execute_batch(
            "CREATE TABLE pad (id INTEGER PRIMARY KEY, content TEXT NOT NULL, updated_at TEXT, dirty INTEGER NOT NULL DEFAULT 0, synced_at TEXT);
             CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT);
             INSERT INTO pad (id, content) VALUES (1, 'existing notes');",
        ).unwrap();
        Pad { connection }
    }

    #[test]
    fn unicode_and_whitespace_round_trip() {
        let pad = sample();
        let text = "  hello 🖋️\n\nsecond line  | 21 Nov 2026\n";
        pad.save("existing notes", text).unwrap();
        assert_eq!(pad.load().unwrap(), text);
        let dirty: i32 = pad.connection.query_row("SELECT dirty FROM pad", [], |row| row.get(0)).unwrap();
        assert_eq!(dirty, 1);
    }

    #[test]
    fn conflicting_edits_are_not_overwritten() {
        let pad = sample();
        pad.save("existing notes", "other editor").unwrap();
        assert!(pad.save("existing notes", "my editor").is_err());
        assert_eq!(pad.load().unwrap(), "other editor");
    }

    #[test]
    fn nonexistent_database_is_not_created() {
        let path = std::env::temp_dir().join(format!("jot-core-missing-{}", std::process::id()));
        assert!(!path.exists());
        assert!(Pad::open(&path).is_err());
        assert!(!path.exists());
    }

    #[test]
    fn clear_and_undo_are_atomic_and_preserve_pending_edits() {
        let pad = sample();
        let pending = "unsaved 🖋️\n\n";
        pad.clear("existing notes", pending).unwrap();
        assert_eq!(pad.load().unwrap(), "");
        assert_eq!(pad.last_cleared().unwrap().as_deref(), Some(pending));
        pad.undo_clear(pending).unwrap();
        assert_eq!(pad.load().unwrap(), pending);
        assert_eq!(pad.last_cleared().unwrap(), None);
    }

    #[test]
    fn conflict_does_not_erase_notes_or_undo_copy() {
        let pad = sample();
        pad.clear("existing notes", "old copy").unwrap();
        pad.save("", "new notes").unwrap();
        assert!(pad.undo_clear("old copy").is_err());
        assert!(pad.clear("", "other copy").is_err());
        assert_eq!(pad.load().unwrap(), "new notes");
        assert_eq!(pad.last_cleared().unwrap().as_deref(), Some("old copy"));
    }

    #[test]
    fn clear_rolls_back_if_backup_fails() {
        let pad = sample();
        pad.connection.execute_batch(
            "CREATE TRIGGER reject_backup BEFORE INSERT ON settings BEGIN SELECT RAISE(ABORT, 'injected failure'); END;"
        ).unwrap();
        assert!(pad.clear("existing notes", "pending notes").is_err());
        assert_eq!(pad.load().unwrap(), "existing notes");
        assert_eq!(pad.last_cleared().unwrap(), None);
    }

    #[test]
    fn undo_rolls_back_if_backup_removal_fails() {
        let pad = sample();
        pad.clear("existing notes", "undo copy").unwrap();
        pad.connection.execute_batch(
            "CREATE TRIGGER reject_delete BEFORE DELETE ON settings BEGIN SELECT RAISE(ABORT, 'injected failure'); END;"
        ).unwrap();
        assert!(pad.undo_clear("undo copy").is_err());
        assert_eq!(pad.load().unwrap(), "");
        assert_eq!(pad.last_cleared().unwrap().as_deref(), Some("undo copy"));
    }

    fn with_content(content: &str, format: Option<&str>) -> Pad {
        let pad = sample();
        pad.connection.execute(
            "UPDATE pad SET content = ?1 WHERE id = 1", params![content],
        ).unwrap();
        if let Some(format) = format {
            pad.connection.execute(
                "INSERT INTO settings (key, value) VALUES ('tag_format', ?1)",
                params![format],
            ).unwrap();
        }
        pad
    }

    #[test]
    fn divider_headers_migrate_to_inline_tags_once() {
        let pad = with_content(
            "archive\n— Thu 19 Nov 2026 —\n\nhello  \nworld\n",
            None,
        );
        assert!(pad.migrate_legacy().unwrap());
        assert_eq!(
            pad.load().unwrap(),
            "archive\nhello  | 19 Nov 2026\nworld  | 19 Nov 2026",
        );
        let format: String = pad.connection.query_row(
            "SELECT value FROM settings WHERE key = 'tag_format'", [], |r| r.get(0),
        ).unwrap();
        assert_eq!(format, "2");
        assert!(!pad.migrate_legacy().unwrap());
        assert_eq!(
            pad.load().unwrap(),
            "archive\nhello  | 19 Nov 2026\nworld  | 19 Nov 2026",
        );
    }

    #[test]
    fn dot_separators_upgrade_from_format_one() {
        let pad = with_content(
            "one  · 1 Jan 2020\ntwo · 22 Dec 1999  \nthree | 3 Feb 2021",
            Some("1"),
        );
        assert!(pad.migrate_legacy().unwrap());
        assert_eq!(
            pad.load().unwrap(),
            "one  | 1 Jan 2020\ntwo  | 22 Dec 1999\nthree | 3 Feb 2021",
        );
        assert!(!pad.migrate_legacy().unwrap());
    }

    #[test]
    fn current_format_is_not_touched_by_migration() {
        let pad = with_content("notes  | 5 Nov 2026\n", Some("2"));
        assert!(!pad.migrate_legacy().unwrap());
        assert_eq!(pad.load().unwrap(), "notes  | 5 Nov 2026\n");
        let dirty: i32 = pad.connection.query_row(
            "SELECT dirty FROM pad WHERE id = 1", [], |r| r.get(0),
        ).unwrap();
        assert_eq!(dirty, 0);
    }
}
