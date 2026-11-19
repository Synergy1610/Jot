// T-007: real palette and spacing tokens go here. Components must reference
// tokens, never literals, so a later palette swap touches only this file.
export const tokens = {
  palette: {
    background: "#1c1c1f",
    surface: "#242428",
    text: "#e6e6e6",
    muted: "#8a8a93",
    accent: "#7aa2f7",
  },
  space: {
    gap: 12,
    padding: 16,
  },
  radius: 10,
  font: "ui-monospace, monospace",
} as const;
