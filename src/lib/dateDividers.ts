// T-006 / Option A: the date divider is a read-only header, not editor text.

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

const DIVIDER_RE =
  /^\s*— ([A-Za-z]{3}) (\d{2}) ([A-Za-z]{3}) (\d{4}) —\s*$/;

/** Display form, e.g. "— Thu 19 Nov 2026 —". */
export function dividerForDate(d: Date = new Date()): string {
  const day = String(d.getDate()).padStart(2, "0");
  return `— ${WEEKDAYS[d.getDay()]} ${day} ${MONTHS[d.getMonth()]} ${d.getFullYear()} —`;
}

/** ISO key used for comparisons/storage, e.g. "2026-11-19". */
export function dividerKeyForDate(d: Date = new Date()): string {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

/** Turn an ISO key back into the display form. */
export function formatDividerKey(key: string): string {
  const [year, month, day] = key.split("-").map(Number);
  return dividerForDate(new Date(year, month - 1, day));
}

/** Parse a divider line into its ISO key, or null when it isn't a divider. */
export function parseDividerLine(line: string): string | null {
  const m = line.trim().match(DIVIDER_RE);
  if (!m) return null;
  const monthIndex = MONTHS.indexOf(m[3]);
  if (monthIndex < 0) return null;
  return `${m[4]}-${String(monthIndex + 1).padStart(2, "0")}-${m[2]}`;
}

export type LogParts = {
  archive: string;
  body: string;
  dividerKey: string | null;
  dividerLine: string | null;
};

/**
 * New-format log: archive, then the divider line, then today's editable body.
 * Everything after the last divider belongs to the current day.
 */
export function parseNewLog(text: string): LogParts {
  const lines = text.split("\n");
  let lastIndex = -1;
  let dividerKey: string | null = null;
  let dividerLine: string | null = null;
  for (let i = 0; i < lines.length; i++) {
    const key = parseDividerLine(lines[i]);
    if (key) {
      lastIndex = i;
      dividerKey = key;
      dividerLine = lines[i].trim();
    }
  }
  if (lastIndex === -1) {
    return { archive: "", body: text.trim(), dividerKey: null, dividerLine: null };
  }
  return {
    archive: lines.slice(0, lastIndex).join("\n").trim(),
    body: lines.slice(lastIndex + 1).join("\n").trim(),
    dividerKey,
    dividerLine,
  };
}

/**
 * Legacy log: older versions appended the divider AFTER the user's text.
 * Combine both sides of the divider so no text is lost during migration.
 */
export function parseLegacyLog(text: string): {
  body: string;
  dividerKey: string | null;
  dividerLine: string | null;
} {
  const lines = text.split("\n");
  let lastIndex = -1;
  let dividerKey: string | null = null;
  let dividerLine: string | null = null;
  for (let i = 0; i < lines.length; i++) {
    const key = parseDividerLine(lines[i]);
    if (key) {
      lastIndex = i;
      dividerKey = key;
      dividerLine = lines[i].trim();
    }
  }
  if (lastIndex === -1) {
    return { body: text.trim(), dividerKey: null, dividerLine: null };
  }
  const before = lines.slice(0, lastIndex).join("\n").trim();
  const after = lines.slice(lastIndex + 1).join("\n").trim();
  return {
    body: [before, after].filter(Boolean).join("\n").trim(),
    dividerKey,
    dividerLine,
  };
}

/** Rebuild the full stored log from its parts (legacy divider format). */
export function buildFullContent(
  archive: string,
  dividerLine: string,
  body: string,
): string {
  const parts: string[] = [];
  if (archive.trim()) parts.push(archive.trim());
  parts.push(dividerLine);
  parts.push("");
  if (body.trim()) parts.push(body.trim());
  return parts.join("\n");
}

// --- Inline date tags: one continuous pad, tag appended after a line. ---

/** Small tag, e.g. "| 20 Nov 2026". */
export function inlineTagForDate(d: Date = new Date()): string {
  return `| ${d.getDate()} ${MONTHS[d.getMonth()]} ${d.getFullYear()}`;
}

const TAG_RE = /\s*\|\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}\s*$/;

const OLD_TAG_RE = /\s*·\s+(\d{1,2}\s+[A-Za-z]{3}\s+\d{4})\s*$/;

/** One-time upgrade from the old "·" separator to "|". */
export function upgradeTagSeparators(text: string): string {
  return text
    .split("\n")
    .map((line) => {
      const match = OLD_TAG_RE.exec(line);
      return match ? line.replace(match[0], "  | " + match[1]) : line;
    })
    .join("\n");
}

export function hasInlineTag(line: string): boolean {
  return TAG_RE.test(line);
}

/**
 * One-time upgrade from the old divider-header format. A divider introduced
 * the section BELOW it, so every line in that section gets the divider's date
 * as its small inline tag. No user text is removed or reordered.
 */
export function migrateDividersToTags(text: string): string {
  const out: string[] = [];
  let currentTag: string | null = null;
  let skipBlank = false;

  for (const line of text.split("\n")) {
    const key = parseDividerLine(line);
    if (key) {
      const [year, month, day] = key.split("-").map(Number);
      currentTag = inlineTagForDate(new Date(year, month - 1, day));
      skipBlank = true;
      continue;
    }
    if (!line.trim()) {
      if (!skipBlank) out.push(line);
      continue;
    }
    skipBlank = false;
    out.push(
      currentTag && !hasInlineTag(line)
        ? line.trimEnd() + "  " + currentTag
        : line,
    );
  }

  return out.join("\n").replace(/^\n+|\n+$/g, "");
}
