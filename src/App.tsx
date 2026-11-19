import { invoke } from "@tauri-apps/api/core";
import { useEffect, useRef, useState } from "react";
import { dividerForDate } from "./lib/dateDividers";
import { getPadState, loadPad, onContentChange, setOnSave } from "./lib/store";

type Health = {
  ok: boolean;
  db: boolean;
  version: string;
};

function displayDate(): string {
  // "Thu 19 Nov 2026" — strips the leading dash and trailing dash of the divider form.
  return dividerForDate().slice(1, -1).trim();
}

export default function App() {
  const [content, setContent] = useState("");
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [headerDate] = useState(displayDate);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

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
      if (!disposed) setContent(text);
      // Focus the pad and place the cursor at the end when it is summoned.
      setTimeout(() => textareaRef.current?.focus(), 50);
    });

    setOnSave(() => {
      const state = getPadState();
      if (!state.dirty) {
        setError(null);
      }
    });

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        void invoke("hide_pad");
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => {
      disposed = true;
      window.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  const status =
    error
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
        <button className="gear" aria-label="Settings">
          ⚙
        </button>
      </header>
      <textarea
        ref={textareaRef}
        className="textarea"
        value={content}
        placeholder="Start typing…"
        autoFocus
        onChange={(event) => setContent(onContentChange(event.target.value))}
      />
    </div>
  );
}
