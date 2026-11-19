// T-006: append a dated divider once per local day when the pad is first opened.

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

/**
 * The plain-text divider format, e.g. "— Thu 19 Nov 2026 —".
 * Kept as plain text so the log stays fully inspectable.
 */
export function dividerForDate(d: Date = new Date()): string {
  const day = String(d.getDate()).padStart(2, "0");
  return `— ${WEEKDAYS[d.getDay()]} ${day} ${MONTHS[d.getMonth()]} ${d.getFullYear()} —`;
}

function dividerDateString(d: Date): string {
  const day = String(d.getDate()).padStart(2, "0");
  return `${WEEKDAYS[d.getDay()]} ${day} ${MONTHS[d.getMonth()]} ${d.getFullYear()}`;
}

const DIVIDER_RE =
  /^\s*— ([A-Za-z]{3}) (\d{2}) ([A-Za-z]{3}) (\d{4}) —\s*$/;

/** Returns the date string of the last divider anywhere in the text, if any. */
export function lastDivider(text: string): string | null {
  let last: string | null = null;
  for (const line of text.split("\n")) {
    const m = line.match(DIVIDER_RE);
    if (m) {
      last = `${m[1]} ${m[2]} ${m[3]} ${m[4]}`;
    }
  }
  return last;
}

/**
 * Append today's divider when the document is non-empty and the last
 * divider is from another day or missing. Returns the text unchanged when a
 * divider for today already exists, so callers can skip the save.
 */
export function appendDividerIfNeeded(
  text: string,
  now: Date = new Date(),
): string {
  const today = dividerDateString(now);
  if (lastDivider(text) === today) {
    return text;
  }
  if (text.trim().length === 0) {
    return text;
  }
  const separator = text.endsWith("\n") ? "" : "\n";
  return `${text}${separator}${dividerForDate(now)}\n`;
}
