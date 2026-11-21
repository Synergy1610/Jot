//! Local SQLite store: one continuous pad, with small inline date tags.
//! The whole log is a single editable string; tags are plain text appended
//! when a line is completed, so nothing is hidden or read-only.

import Database from "@tauri-apps/plugin-sql";
import { migrateDividersToTags, upgradeTagSeparators } from "./dateDividers";

const DB_URL = "sqlite:pad.db";
const PAD_ID = 1;

export interface PadState {
  content: string;
  updatedAt: string | null;
  dirty: boolean;
  syncedAt: string | null;
}

let db: Database | null = null;
let state: PadState = {
  content: "",
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

async function persist(sqlDb: Database, content: string, now: Date): Promise<void> {
  state.content = content;
  state.updatedAt = now.toISOString();
  state.dirty = true;
  await sqlDb.execute(
    "INSERT INTO pad (id, content, updated_at, dirty) VALUES (?, ?, ?, 1) " +
      "ON CONFLICT(id) DO UPDATE SET content=excluded.content, " +
      "updated_at=excluded.updated_at, dirty=1",
    [PAD_ID, content, state.updatedAt],
  );
}

/** Load the pad, migrating old divider-formatted logs to inline date tags once. */
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
    let content = row?.content ?? "";

    let format = await readSetting(sqlDb, "tag_format");
    if (!format) {
      content = migrateDividersToTags(content);
      await persist(sqlDb, content, new Date());
      await writeSetting(sqlDb, "tag_format", "1");
      format = "1";
    }
    if (format !== "2") {
      content = upgradeTagSeparators(content);
      await persist(sqlDb, content, new Date());
      await writeSetting(sqlDb, "tag_format", "2");
    }

    state = {
      content,
      updatedAt: row?.updated_at ?? null,
      dirty: Boolean(row?.dirty),
      syncedAt: row?.synced_at ?? null,
    };
    onSaveHandler?.();
  } catch (error) {
    console.error("jot: loadPad failed", error);
  }
  return state.content;
}

export function getPadState(): PadState {
  return { ...state };
}

export async function savePad(content: string, now = new Date()): Promise<void> {
  try {
    await persist(await getDb(), content, now);
  } catch (error) {
    console.error("jot: savePad failed", error);
  }
}

export function setOnSave(handler: (() => void) | null): void {
  onSaveHandler = handler;
}

function scheduleSave(): void {
  if (saveTimer !== null) clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    saveTimer = null;
    void savePad(state.content);
    onSaveHandler?.();
  }, 300);
}

/** The whole pad is editable text; tags are inserted by the UI on Enter. */
export function onContentChange(text: string): string {
  state.content = text;
  scheduleSave();
  return text;
}

/** T-009: wipe the pad and stash it for one undo. */
export async function clearPad(): Promise<void> {
  const sqlDb = await getDb();
  await writeSetting(sqlDb, "last_cleared", state.content);
  await savePad("");
}

/** T-009: restore the most recently cleared pad. */
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
