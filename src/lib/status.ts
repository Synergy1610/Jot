// T-007 / T-014: the status line reflects sync and auth state.
export type PadStatus =
  | "Saved"
  | "Syncing"
  | "Synced"
  | "Offline"
  | "Signed out"
  | "Error";
export {};
