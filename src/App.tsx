import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { useEffect, useRef, useState } from "react";
import { SettingsPanel } from "./components/SettingsPanel";
import { dividerForDate } from "./lib/dateDividers";
import { getPadState, loadPad, onContentChange, setOnSave } from "./lib/store";

type Health = {
  ok: boolean;
  db: boolean;
  version: string;
};

function displayDate(): string {
  return dividerForDate().slice(1, -1).trim();
}

export default function App() {
  const [content, setContent] = useState("");
  const [divider, setDivider] = useState("");
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [headerDate] = useState(displayDate);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [dbPath, setDbPath] = useState("");
  const settingsOpenRef = useRef(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    settingsOpenRef.current = settingsOpen;
  }, [settingsOpen]);

  useEffect(() => {
    let disposed = false;

    invoke<Health>("health_check")
      .then((result) => {
        if (!disposed) setHealth(result);
        document.title = `Jot | health ${result.ok ? "OK" : "FAIL"} | db ${result.db ? "OK" : "FAIL"}`;
      })
      .catch((cause: unknown) => {
        if (!disposed) setError(String(cause));
        document.title = "Jot | health ERROR";
      });

    loadPad().then((text) => {
      if (!disposed) {
        setContent(text);
        setDivider(getPadState().divider);
      }
      setTimeout(() => textareaRef.current?.focus(), 50);
    });

    invoke<string>("get_db_path")
      .then(setDbPath)
      .catch(() => setDbPath("unknown"));

    const unlisten = listen("jot://open-settings", () => {
      setSettingsOpen(true);
    });

    setOnSave(() => {
      if (!getPadState().dirty) setError(null);
    });

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        if (settingsOpenRef.current) {
          setSettingsOpen(false);
        } else {
          void invoke("hide_pad");
        }
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => {
      disposed = true;
      void unlisten.then((fn) => fn());
      window.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  const status = error
    ? "Error"
    : health && !health.ok
      ? "Error"
      : getPadState().dirty
        ? "Saved locally"
        : "Saved";

  return (
    <div className="pad">
      <header className="header">
        <span className="header-date">{headerDate}</span>
        <span className="status" data-testid="status">
          {status}
        </span>
        <button
          className="gear"
          aria-label="Settings"
          onClick={() => setSettingsOpen(true)}
        >
          ⚙
        </button>
      </header>
      <div className="day-divider" data-testid="day-divider">
        {divider}
      </div>
      <textarea
        ref={textareaRef}
        className="textarea"
        value={content}
        placeholder="Start typing…"
        autoFocus
        onChange={(event) => setContent(onContentChange(event.target.value))}
      />
      <SettingsPanel
        open={settingsOpen}
        onClose={() => setSettingsOpen(false)}
        version={health?.version ?? "0.1.0"}
        dbPath={dbPath}
        onContentChanged={setContent}
      />
    </div>
  );
}
