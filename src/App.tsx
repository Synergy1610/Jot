import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import {
  Fragment,
  useEffect,
  useRef,
  useState,
  type KeyboardEvent as ReactKeyboardEvent,
  type ReactNode,
} from "react";
import { SettingsPanel } from "./components/SettingsPanel";
import {
  dividerForDate,
  hasInlineTag,
  inlineTagForDate,
} from "./lib/dateDividers";
import { getPadState, loadPad, onContentChange, setOnSave } from "./lib/store";

type Health = {
  ok: boolean;
  db: boolean;
  version: string;
};

function displayDate(): string {
  return dividerForDate().slice(1, -1).trim();
}

const TAG_RE = /^(.*?)(\s*\|\s*\d{1,2}\s+[A-Za-z]{3}\s+\d{4})$/;

/** Mirror the editable text with date tags wrapped in coloured spans. */
function mirrorContent(text: string) {
  return text.split("\n").map((line, index) => {
    const match = TAG_RE.exec(line);
    return (
      <Fragment key={index}>
        {index > 0 ? "\n" : null}
        {match ? (
          <>
            <span>{match[1]}</span>
            <span className="date-tag">{match[2]}</span>
          </>
        ) : (
          line
        )}
      </Fragment>
    );
  });
}

/** A small icon badge with a tick and a delayed, state-driven tooltip. */
function StatusBadge({
  active,
  title,
  children,
}: {
  active: boolean;
  title: string;
  children: ReactNode;
}) {
  const [visible, setVisible] = useState(false);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const autoHide = useRef<ReturnType<typeof setTimeout> | null>(null);

  const show = () => {
    if (timer.current) clearTimeout(timer.current);
    if (autoHide.current) clearTimeout(autoHide.current);
    timer.current = setTimeout(() => {
      setVisible(true);
      // Safety net: a warped pointer can skip mouseleave entirely.
      autoHide.current = setTimeout(() => setVisible(false), 4000);
    }, 220);
  };
  const hide = () => {
    if (timer.current) clearTimeout(timer.current);
    if (autoHide.current) clearTimeout(autoHide.current);
    timer.current = null;
    autoHide.current = null;
    setVisible(false);
  };

  useEffect(() => {
    window.addEventListener("blur", hide);
    document.addEventListener("mouseleave", hide);
    return () => {
      window.removeEventListener("blur", hide);
      document.removeEventListener("mouseleave", hide);
      if (timer.current) clearTimeout(timer.current);
      if (autoHide.current) clearTimeout(autoHide.current);
    };
  }, []);

  return (
    <span
      className={`status-icon${active ? " active" : ""}`}
      data-testid={title}
      tabIndex={0}
      onMouseEnter={show}
      onMouseLeave={hide}
      onFocus={show}
      onBlur={hide}
    >
      {children}
      {active && <span className="badge-tick">✓</span>}
      {visible && (
        <span className="status-tooltip" role="tooltip">
          {title}
        </span>
      )}
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

function GearIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <circle cx="12" cy="12" r="3.2" />
      <path d="M19.4 15a1.7 1.7 0 0 0 .34 1.88l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.7 1.7 0 0 0-1.88-.34 1.7 1.7 0 0 0-1.03 1.56V21a2 2 0 1 1-4 0v-.07a1.7 1.7 0 0 0-1.03-1.56 1.7 1.7 0 0 0-1.88.34l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.7 1.7 0 0 0 4.6 15a1.7 1.7 0 0 0-1.56-1.03H3a2 2 0 1 1 0-4h.07A1.7 1.7 0 0 0 4.6 8.94a1.7 1.7 0 0 0-.34-1.88l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.7 1.7 0 0 0 9 4.6h.07A1.7 1.7 0 0 0 10.1 3.04V3a2 2 0 1 1 4 0v.07a1.7 1.7 0 0 0 1.03 1.56 1.7 1.7 0 0 0 1.88-.34l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.7 1.7 0 0 0 19.4 9v.07a1.7 1.7 0 0 0 1.56 1.03H21a2 2 0 1 1 0 4h-.07A1.7 1.7 0 0 0 19.4 15Z" />
    </svg>
  );
}

export default function App() {
  const [content, setContent] = useState("");
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [headerDate] = useState(displayDate);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [dbPath, setDbPath] = useState("");
  const [dirty, setDirty] = useState(false);
  const settingsOpenRef = useRef(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const mirrorRef = useRef<HTMLDivElement>(null);

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
      if (!disposed) setContent(text);
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

  /** Enter finishes a line: append today's small tag, then start a new line. */
  const onEnter = (event: ReactKeyboardEvent<HTMLTextAreaElement>) => {
    event.preventDefault();
    const el = event.currentTarget;
    const text = el.value;
    const start = el.selectionStart;
    const end = el.selectionEnd;
    const lineStart = text.lastIndexOf("\n", start - 1) + 1;
    const nextBreak = text.indexOf("\n", end);
    const lineEnd = nextBreak === -1 ? text.length : nextBreak;
    const line = text.slice(lineStart, lineEnd);
    const finished = hasInlineTag(line) ? line : line.trimEnd() + "  " + inlineTagForDate();
    const next = text.slice(0, lineStart) + finished + "\n" + text.slice(lineEnd);
    const caret = lineStart + finished.length + 1;

    setDirty(true);
    setContent(onContentChange(next));
    requestAnimationFrame(() => {
      el.selectionStart = el.selectionEnd = caret;
    });
  };

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
          <GearIcon />
        </button>
      </header>
      <div className="editor">
        <div className="editor-mirror" ref={mirrorRef} aria-hidden="true">
          {content ? mirrorContent(content) : null}
        </div>
        <textarea
          ref={textareaRef}
          className="textarea"
          data-testid="pad"
          value={content}
          placeholder="Start typing…"
          autoFocus
          onChange={(event) => {
            setDirty(true);
            setContent(onContentChange(event.target.value));
          }}
          onKeyDown={(event) => {
            if (event.key === "Enter") onEnter(event);
          }}
          onScroll={(event) => {
            if (mirrorRef.current) {
              mirrorRef.current.scrollTop = event.currentTarget.scrollTop;
            }
          }}
        />
      </div>
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
