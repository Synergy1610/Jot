//! T-005: local SQLite store via @tauri-apps/plugin-sql.
//! The pad is a single row (id = 1). Writes are debounced by the caller.

import Database from "@tauri-apps/plugin-sql";
import { appendDividerIfNeeded } from "./dateDividers";

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

/** Load the pad from SQLite, applying the T-006 daily divider when needed. */
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
    state = {
      content: row?.content ?? "",
      updatedAt: row?.updated_at ?? null,
      dirty: Boolean(row?.dirty),
      syncedAt: row?.synced_at ?? null,
    };
    const adjusted = appendDividerIfNeeded(state.content);
    if (adjusted !== state.content) {
      state.content = adjusted;
      state.dirty = true;
      await savePad(state.content);
    }
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
    const sqlDb = await getDb();
    state.content = content;
    state.updatedAt = now.toISOString();
    state.dirty = true;
    await sqlDb.execute(
      "INSERT INTO pad (id, content, updated_at, dirty) VALUES (?, ?, ?, 1) " +
        "ON CONFLICT(id) DO UPDATE SET content=excluded.content, " +
        "updated_at=excluded.updated_at, dirty=1",
      [PAD_ID, content, state.updatedAt],
    );
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

/**
 * Called on every change; returns the adjusted text (divider appended when a
 * new day began) and schedules the debounced save.
 */
export function onContentChange(text: string): string {
  // The daily divider is only applied on open (loadPad). Typing later in the
  // day must not append another divider.
  state.content = text;
  scheduleSave();
  return text;
}

/** T-009: wipe the local pad and stash the old text for one undo. */
export async function clearPad(): Promise<void> {
  const sqlDb = await getDb();
  await sqlDb.execute(
    "INSERT INTO settings (key, value) VALUES ('last_cleared', ?) " +
      "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
    [state.content],
  );
  await savePad("");
}

/** T-009: restore the most recently cleared text; false when nothing to undo. */
export async function undoClear(): Promise<boolean> {
  const sqlDb = await getDb();
  const rows = await sqlDb.select<Array<{ value: string }>>(
    "SELECT value FROM settings WHERE key = 'last_cleared'",
  );
  if (rows.length === 0) return false;
  await sqlDb.execute("DELETE FROM settings WHERE key = 'last_cleared'");
  await savePad(rows[0].value);
  return true;
}

export async function canUndoClear(): Promise<boolean> {
  try {
    const sqlDb = await getDb();
    const rows = await sqlDb.select<Array<{ value: string }>>(
      "SELECT value FROM settings WHERE key = 'last_cleared'",
    );
    return rows.length > 0;
  } catch {
    return false;
  }
}
