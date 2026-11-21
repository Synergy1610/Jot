//! T-005 / Option A: local SQLite store.
//! pad.content keeps the full log (archive + divider + body), but the editor
//! only ever receives the editable body. The divider is a read-only header.

import Database from "@tauri-apps/plugin-sql";
import {
  buildFullContent,
  dividerForDate,
  dividerKeyForDate,
  formatDividerKey,
  parseLegacyLog,
  parseNewLog,
} from "./dateDividers";

const DB_URL = "sqlite:pad.db";
const PAD_ID = 1;

export interface PadState {
  content: string; // editable body only (no divider)
  archive: string; // earlier days' full log, kept read-only but visible
  divider: string; // read-only display string
  updatedAt: string | null;
  dirty: boolean;
  syncedAt: string | null;
}

let db: Database | null = null;
let archive = "";
let dividerKey = "";
let dividerLine = "";
let state: PadState = {
  content: "",
  archive: "",
  divider: "",
  updatedAt: null,
  dirty: false,
  syncedAt: null,
};
let saveTimer: ReturnType<typeof setTimeout> | null = null;
let onSaveHandler: (() => void) | null = null;

async function getDb(): Promise<Database> {
  if (!db) {
    db = await Database.load(DB_URL);
  }
  return db;
}

async function readSetting(sqlDb: Database, key: string): Promise<string | null> {
  const rows = await sqlDb.select<Array<{ value: string }>>(
    "SELECT value FROM settings WHERE key = ?",
    [key],
  );
  return rows.length > 0 ? rows[0].value : null;
}

async function writeSetting(sqlDb: Database, key: string, value: string): Promise<void> {
  await sqlDb.execute(
    "INSERT INTO settings (key, value) VALUES (?, ?) " +
      "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
    [key, value],
  );
}

/**
 * Load the pad. Migrates legacy divider-after-text content, rotates to a new
 * day when needed, and returns only the editable body to the textarea.
 */
export async function loadPad(): Promise<string> {
  try {
    const sqlDb = await getDb();
    const rows = await sqlDb.select<
      Array<{
        id: number;
        content: string | null;
        updated_at: string | null;
        dirty: number | null;
        synced_at: string | null;
      }>
    >("SELECT id, content, updated_at, dirty, synced_at FROM pad");
    const row = rows.find((r) => r.id === PAD_ID);
    const rawContent = row?.content ?? "";
    const storedKey = await readSetting(sqlDb, "divider_date");
    const today = dividerKeyForDate();

    let body: string;
    let key: string;
    let line: string;

    if (storedKey) {
      // New format: archive + divider + body.
      const parts = parseNewLog(rawContent);
      archive = parts.archive;
      body = parts.body;
      key = storedKey;
      line = parts.dividerLine ?? formatDividerKey(storedKey);
      if (key !== today) {
        // Archive yesterday's section and start a fresh day.
        archive = buildFullContent(archive, line, body);
        body = "";
        key = today;
        line = dividerForDate();
      }
    } else {
      // Legacy migration: divider was appended after the text.
      const legacy = parseLegacyLog(rawContent);
      archive = "";
      body = legacy.body;
      key = legacy.dividerKey ?? today;
      line = legacy.dividerLine ?? dividerForDate();
      if (key !== today) {
        archive = buildFullContent("", line, body);
        body = "";
        key = today;
        line = dividerForDate();
      }
      await writeSetting(sqlDb, "divider_date", key);
    }

    dividerKey = key;
    dividerLine = line;
    state = {
      content: body,
      archive,
      divider: formatDividerKey(key),
      updatedAt: row?.updated_at ?? null,
      dirty: Boolean(row?.dirty),
      syncedAt: row?.synced_at ?? null,
    };
    await savePad(body);
  } catch (error) {
    console.error("jot: loadPad failed", error);
  }
  return state.content;
}

export function getPadState(): PadState {
  return { ...state };
}

/** Save the editable body; the divider and archive are restored automatically. */
export async function savePad(body: string, now = new Date()): Promise<void> {
  try {
    const sqlDb = await getDb();
    state.content = body;
    state.updatedAt = now.toISOString();
    state.dirty = true;
    const full = buildFullContent(archive, dividerLine, body);
    await sqlDb.execute(
      "INSERT INTO pad (id, content, updated_at, dirty) VALUES (?, ?, ?, 1) " +
        "ON CONFLICT(id) DO UPDATE SET content=excluded.content, " +
        "updated_at=excluded.updated_at, dirty=1",
      [PAD_ID, full, state.updatedAt],
    );
    await writeSetting(sqlDb, "divider_date", dividerKey);
  } catch (error) {
    console.error("jot: savePad failed", error);
  }
}

export function setOnSave(handler: (() => void) | null): void {
  onSaveHandler = handler;
}

function scheduleSave(): void {
  if (saveTimer !== null) {
    clearTimeout(saveTimer);
  }
  saveTimer = setTimeout(() => {
    saveTimer = null;
    void savePad(state.content);
    onSaveHandler?.();
  }, 300);
}

export function onContentChange(text: string): string {
  state.content = text;
  scheduleSave();
  return text;
}

/** Edit earlier days in place; the read-only dividers are left untouched. */
export function onArchiveChange(text: string): string {
  archive = text;
  state.archive = text;
  scheduleSave();
  return text;
}

/** T-009: wipe today's body and stash it for one undo. */
export async function clearPad(): Promise<void> {
  const sqlDb = await getDb();
  await writeSetting(sqlDb, "last_cleared", state.content);
  await savePad("");
}

/** T-009: restore the most recently cleared body. */
export async function undoClear(): Promise<boolean> {
  const sqlDb = await getDb();
  const saved = await readSetting(sqlDb, "last_cleared");
  if (saved === null) return false;
  await sqlDb.execute("DELETE FROM settings WHERE key = 'last_cleared'");
  await savePad(saved);
  return true;
}

export async function canUndoClear(): Promise<boolean> {
  try {
    const sqlDb = await getDb();
    return (await readSetting(sqlDb, "last_cleared")) !== null;
  } catch {
    return false;
  }
}
