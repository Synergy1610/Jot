import { invoke } from "@tauri-apps/api/core";
import { useEffect, useState } from "react";

type Health = {
  ok: boolean;
  db: boolean;
  version: string;
};

/**
 * Placeholder page. It verifies the skeleton is wired end to end:
 * the React page calls the Rust health command, and the command opens SQLite.
 * No application features live here.
 */
export default function App() {
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    invoke<Health>("health_check")
      .then((result) => {
        setHealth(result);
        document.title = `Jot | health ${result.ok ? "OK" : "FAIL"} | db ${result.db ? "OK" : "FAIL"}`;
      })
      .catch((cause: unknown) => {
        setError(String(cause));
        document.title = "Jot | health ERROR";
      });
  }, []);

  // T-002: Escape only hides the pad; it never touches the text.
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        void invoke("hide_pad");
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

  return (
    <main className="shell">
      <header className="header">Jot · scaffold</header>
      <section className="body">
        <p>Placeholder only — no features implemented yet.</p>
        <p data-testid="health">
          {error
            ? `health error: ${error}`
            : health
              ? `health ${health.ok ? "OK" : "FAIL"} · db ${health.db ? "OK" : "FAIL"} · v${health.version}`
              : "checking health…"}
        </p>
      </section>
    </main>
  );
}
