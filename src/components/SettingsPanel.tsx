// T-008 / T-009: settings panel with Account, Hotkey, Clear and About.
import { invoke } from "@tauri-apps/api/core";
import { useEffect, useState } from "react";
import {
  canUndoClear,
  clearPad,
  getPadState,
  undoClear,
} from "../lib/store";

type HotkeyInfo = {
  snippet: string;
  configPath: string;
  configured: boolean;
};

type Props = {
  open: boolean;
  onClose: () => void;
  version: string;
  dbPath: string;
  onContentChanged: (content: string) => void;
};

export function SettingsPanel({
  open,
  onClose,
  version,
  dbPath,
  onContentChanged,
}: Props) {
  const [hotkey, setHotkey] = useState<HotkeyInfo | null>(null);
  const [copied, setCopied] = useState(false);
  const [confirmClear, setConfirmClear] = useState(false);
  const [canUndo, setCanUndo] = useState(false);

  useEffect(() => {
    if (!open) return;
    invoke<HotkeyInfo>("get_hotkey_info")
      .then(setHotkey)
      .catch(() => setHotkey(null));
    canUndoClear().then(setCanUndo);
    setConfirmClear(false);
    setCopied(false);
  }, [open]);

  if (!open) return null;

  const copySnippet = async () => {
    if (!hotkey) return;
    try {
      await invoke("copy_to_clipboard", { text: hotkey.snippet });
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      setCopied(false);
    }
  };

  const openConfig = () => {
    void invoke("open_config");
  };

  const doClear = async () => {
    await clearPad();
    setConfirmClear(false);
    setCanUndo(true);
    onContentChanged("");
    onClose();
  };

  const doUndo = async () => {
    const restored = await undoClear();
    if (restored) {
      setCanUndo(false);
      onContentChanged(getPadState().content);
      onClose();
    }
  };

  return (
    <div className="settings-overlay" onClick={onClose}>
      <section
        className="settings-panel"
        onClick={(event) => event.stopPropagation()}
      >
        <header className="settings-header">
          <strong>Jot settings</strong>
          <button className="gear" aria-label="Close settings" onClick={onClose}>
            ✕
          </button>
        </header>

        <div className="settings-section">
          <h3>Account</h3>
          <p className="settings-muted">
            Signed out — cloud backup is off. Email/password sign-in arrives in
            v2. Your notes stay on this machine.
          </p>
        </div>

        <div className="settings-section">
          <h3>Hotkey (Linux / Hyprland)</h3>
          <p className="settings-muted">
            The keybind belongs to the compositor, not the app. Add this line to
            your bindings file, then reload Hyprland.
          </p>
          <code className="hotkey-snippet">{hotkey?.snippet ?? "loading…"}</code>
          <div className="settings-row">
            <button onClick={copySnippet}>
              {copied ? "Copied" : "Copy"}
            </button>
            <button onClick={openConfig}>Open config</button>
          </div>
          <p className={hotkey?.configured ? "settings-ok" : "settings-warn"}>
            {hotkey === null
              ? "Checking config…"
              : hotkey.configured
                ? "Looks configured"
                : "Not found"}
          </p>
          <p className="settings-muted settings-small">{hotkey?.configPath}</p>
        </div>

        <div className="settings-section">
          <h3>Clear pad</h3>
          {confirmClear ? (
            <div className="settings-row">
              <span className="settings-warn">Erase everything?</span>
              <button onClick={() => setConfirmClear(false)}>Cancel</button>
              <button onClick={doClear}>Confirm</button>
            </div>
          ) : (
            <div className="settings-row">
              <button onClick={() => setConfirmClear(true)}>Clear…</button>
              {canUndo && <button onClick={doUndo}>Undo clear</button>}
            </div>
          )}
        </div>

        <div className="settings-section">
          <h3>About</h3>
          <p className="settings-muted settings-small">version {version}</p>
          <p className="settings-muted settings-small">{dbPath}</p>
          <p className="settings-muted settings-small">Hotkey: Super + W</p>
        </div>
      </section>
    </div>
  );
}
