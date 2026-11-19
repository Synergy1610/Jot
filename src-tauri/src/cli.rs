//! T-003: CLI actions and single-instance dispatch.
//!
//! `jot toggle` / `show` / `hide` / `quit` are forwarded to the running
//! instance. No arguments means "show" (launcher click); `--hidden` keeps the
//! window hidden (autostart).

use crate::commands;
use tauri::{AppHandle, Manager};

#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Toggle,
    Show,
    Hide,
    Quit,
    Hidden,
}

/// Parse an argv slice. The first argument is the executable path and is
/// ignored, as are unknown flags; anything unrecognised defaults to Show.
pub fn parse(args: &[String]) -> Action {
    let mut saw_hidden = false;
    for arg in args.iter().skip(1) {
        match arg.as_str() {
            "toggle" => return Action::Toggle,
            "show" => return Action::Show,
            "hide" => return Action::Hide,
            "quit" => return Action::Quit,
            "--hidden" | "-h" => saw_hidden = true,
            _ => {}
        }
    }
    if saw_hidden {
        Action::Hidden
    } else {
        Action::Show
    }
}

/// Run an action against a live app handle.
pub fn handle(app: &AppHandle, action: Action) {
    match action {
        Action::Toggle => {
            let visible = app
                .get_webview_window("main")
                .and_then(|window| window.is_visible().ok())
                .unwrap_or(false);
            let result = if visible {
                commands::hide_pad(app.clone())
            } else {
                commands::show_pad(app.clone())
            };
            if let Err(error) = result {
                eprintln!("jot: toggle failed: {error}");
            }
        }
        Action::Show => {
            if let Err(error) = commands::show_pad(app.clone()) {
                eprintln!("jot: show failed: {error}");
            }
        }
        Action::Hide => {
            if let Err(error) = commands::hide_pad(app.clone()) {
                eprintln!("jot: hide failed: {error}");
            }
        }
        Action::Quit => app.exit(0),
        Action::Hidden => {}
    }
}
