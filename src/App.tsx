import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { useEffect, useRef, useState, type ReactNode } from "react";
import { SettingsPanel } from "./components/SettingsPanel";
import { dividerForDate } from "./lib/dateDividers";
import { getPadState, loadPad, onArchiveChange, onContentChange, setOnSave } from "./lib/store";

type Health = {
  ok: boolean;
  db: boolean;
  version: string;
};

function displayDate(): string {
  return dividerForDate().slice(1, -1).trim();
}

/** A small icon badge with a tick and a native hover tooltip. */
function StatusBadge({
  active,
  title,
  children,
}: {
  active: boolean;
  title: string;
  children: ReactNode;
}) {
  return (
    <span
      className={`status-icon${active ? " active" : ""}`}
      data-tooltip={title}
      data-testid={title}
      tabIndex={0}
    >
      {children}
      {active && <span className="badge-tick">✓</span>}
    </span>
  );
}

function MonitorIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <rect x="2" y="3" width="20" height="14" rx="2" />
      <path d="M8 21h8M12 17v4" />
    </svg>
  );
}

function CloudIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M6.5 19a4.5 4.5 0 0 1-.6-8.96A6 6 0 0 1 17.4 9.2 4 4 0 0 1 18 19H6.5Z" />
    </svg>
  );
}

export default function App() {
  const [content, setContent] = useState("");
  const [divider, setDivider] = useState("");
  const [archive, setArchive] = useState("");
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [headerDate] = useState(displayDate);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [dbPath, setDbPath] = useState("");
  const [dirty, setDirty] = useState(false);
  const settingsOpenRef = useRef(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const archiveRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    settingsOpenRef.current = settingsOpen;
  }, [settingsOpen]);

  useEffect(() => {
    let disposed = false;
    // Re-focus on every summon: a window launched hidden never established
    // DOM focus, and focus can also be lost while the pad is hidden.
    const focusPad = () => {
      textareaRef.current?.focus();
      setTimeout(() => textareaRef.current?.focus(), 80);
    };

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
        setArchive(getPadState().archive);
      }
      setTimeout(focusPad, 50);
    });

    invoke<string>("get_db_path")
      .then(setDbPath)
      .catch(() => setDbPath("unknown"));

    const unlistenSettings = listen("jot://open-settings", () => {
      setSettingsOpen(true);
    });
    const unlistenFocus = listen("jot://focus", focusPad);
    window.addEventListener("focus", focusPad);

    setOnSave(() => {
      setDirty(false);
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
      void unlistenSettings.then((fn) => fn());
      void unlistenFocus.then((fn) => fn());
      window.removeEventListener("focus", focusPad);
      window.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  const localSaved = !dirty && !error && health?.db !== false;
  const localTitle = error
    ? "Local save failed"
    : dirty
      ? "Saving on this device…"
      : "Saved on this device";
  const cloudTitle = "Cloud backup is off — sign in to enable";

  return (
    <div className="pad">
      <header className="header">
        <span className="header-date">{headerDate}</span>
        <span className="status-icons">
          <StatusBadge active={localSaved} title={localTitle}>
            <MonitorIcon />
          </StatusBadge>
          <StatusBadge active={false} title={cloudTitle}>
            <CloudIcon />
          </StatusBadge>
        </span>
        <button
          className="gear"
          aria-label="Settings"
          onClick={() => setSettingsOpen(true)}
        >
          ⚙
        </button>
      </header>
      {archive && (
        <textarea
          ref={archiveRef}
          className="archive"
          data-testid="archive"
          aria-label="Earlier days"
          value={archive}
          onChange={(event) => {
            setDirty(true);
            setArchive(onArchiveChange(event.target.value));
          }}
        />
      )}
      <div className="day-divider" data-testid="day-divider">
        {divider}
      </div>
      <textarea
        ref={textareaRef}
        className="textarea"
        value={content}
        placeholder="Start typing…"
        autoFocus
        onChange={(event) => {
          setDirty(true);
          setContent(onContentChange(event.target.value));
        }}
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
