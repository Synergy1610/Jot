//! v2 cloud sync for Supabase. Local-first: every operation returns an error
//! rather than changing local notes when auth or network fails.

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use std::sync::OnceLock;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Session {
    pub access_token: String,
    pub refresh_token: String,
    pub expires_at: i64,
    pub user_id: String,
    pub email: String,
}

#[derive(Clone, Debug)]
pub struct CloudPad {
    pub content: String,
    pub updated_at: String,
    pub version: i64,
}

pub struct Cloud {
    url: String,
    anon_key: String,
    session: Option<Session>,
}

fn config_dir() -> PathBuf {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))
        .unwrap_or_else(|| PathBuf::from("."));
    base.join("dev.jot.app")
}

fn session_file() -> PathBuf { config_dir().join("cloud-session.json") }
fn account_file() -> PathBuf { config_dir().join("cloud-account") }
fn state_file() -> PathBuf { config_dir().join("cloud-state.json") }

#[derive(Clone, Debug, Serialize, Deserialize)]
struct BaseState {
    content: String,
    version: i64,
    #[serde(default)]
    user_id: String,
}

fn load_base(user_id: &str) -> Option<BaseState> {
    std::fs::read(state_file()).ok()
        .and_then(|bytes| serde_json::from_slice::<BaseState>(&bytes).ok())
        // A state file from before accounts were tagged still belongs to the
        // only account that could have written it.
        .filter(|state| state.user_id.is_empty() || state.user_id == user_id)
}

fn save_base(user_id: &str, state: &BaseState) -> Result<(), String> {
    let mut state = state.clone();
    state.user_id = user_id.to_string();
    let json = serde_json::to_vec(&state).map_err(|e| format!("Could not encode sync state: {e}"))?;
    write_private(&state_file(), &json)
}

fn append_text(base: &str, extra: &str) -> String {
    if extra.is_empty() { return base.to_string(); }
    if base.is_empty() { return extra.to_string(); }
    format!("{}\n\n{}", base.trim_end_matches('\n'), extra.trim_start_matches('\n'))
}

/// Compare pads without letting a trailing newline look like an edit.
fn without_trailing_newlines(text: &str) -> &str {
    text.trim_end_matches(['\n', '\r'])
}

/// What a sync should do with one device's notes.
#[derive(Debug, PartialEq)]
enum SyncPlan {
    Pull,
    Upload(String),
    Conflict,
}

/// Decide how local, base and remote relate. Only append-only local changes
/// are merged automatically; anything that rewrote earlier text is a conflict.
fn plan_sync(local: &str, base: Option<&str>, remote: &str) -> SyncPlan {
    let local_key = without_trailing_newlines(local);
    let remote_key = without_trailing_newlines(remote);

    if let Some(base) = base.filter(|base| !base.is_empty()) {
        let base_key = without_trailing_newlines(base);
        if local_key == base_key {
            return SyncPlan::Pull;
        }
        if remote_key == base_key {
            return SyncPlan::Upload(local.to_string());
        }
        if let Some(delta) = local.strip_prefix(base) {
            if delta.trim().is_empty() {
                return SyncPlan::Pull;
            }
            return SyncPlan::Upload(append_text(remote, delta));
        }
        return SyncPlan::Conflict;
    }

    // No usable base. This is a fresh device or a signed-out pad, so treat
    // all local text as local-only — but never append text the cloud already
    // contains, or the same notes show up twice.
    if local_key.is_empty() || local_key == remote_key {
        return SyncPlan::Pull;
    }
    if local_key.starts_with(remote_key) {
        return SyncPlan::Upload(local.to_string());
    }
    if remote_key.contains(local_key) {
        return SyncPlan::Pull;
    }
    SyncPlan::Upload(append_text(remote, local))
}

fn write_private(path: &Path, bytes: &[u8]) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|e| format!("Could not create {}: {e}", parent.display()))?;
    }
    let mut options = OpenOptions::new();
    options.write(true).create(true).truncate(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(path)
        .map_err(|e| format!("Could not open {}: {e}", path.display()))?;
    file.write_all(bytes)
        .map_err(|e| format!("Could not write {}: {e}", path.display()))
}

fn now_unix() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs() as i64
}

/// Minimal civil-date conversion so timestamps do not need a chrono dependency.
fn rfc3339(seconds: i64) -> String {
    let days = seconds.div_euclid(86_400);
    let rem = seconds.rem_euclid(86_400);
    let z = days + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let mut year = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    if month <= 2 { year += 1; }
    format!("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
        year, month, day, rem / 3600, (rem % 3600) / 60, rem % 60)
}

static AGENT: OnceLock<ureq::Agent> = OnceLock::new();

fn agent() -> &'static ureq::Agent {
    AGENT.get_or_init(|| {
        ureq::AgentBuilder::new()
            .timeout(Duration::from_secs(10))
            .build()
    })
}

fn endpoint(url: &str, path: &str) -> String {
    format!("{}/{}", url.trim_end_matches('/'), path.trim_start_matches('/'))
}

fn http_error(error: ureq::Error) -> String {
    match error {
        ureq::Error::Status(code, response) => {
            let body = response.into_string().unwrap_or_default();
            let detail: Value = serde_json::from_str(&body).unwrap_or(Value::Null);
            let message = detail.get("msg")
                .or_else(|| detail.get("message"))
                .or_else(|| detail.get("error_description"))
                .and_then(Value::as_str)
                .unwrap_or(&body);
            format!("Supabase returned {code}: {}", message.chars().take(200).collect::<String>())
        }
        ureq::Error::Transport(error) => format!("Could not reach Supabase: {error}"),
    }
}

fn post_json(url: &str, anon_key: &str, token: Option<&str>, body: &Value) -> Result<Value, String> {
    let mut request = agent().post(url)
        .set("apikey", anon_key)
        .set("Content-Type", "application/json");
    if let Some(token) = token {
        request = request.set("Authorization", &format!("Bearer {token}"));
    }
    let response = request.send_string(&body.to_string()).map_err(http_error)?;
    let text = response.into_string().map_err(|e| format!("Could not read response: {e}"))?;
    if text.is_empty() { return Ok(Value::Null); }
    serde_json::from_str(&text).map_err(|e| format!("Could not parse Supabase response: {e}"))
}

fn get_json(url: &str, anon_key: &str, token: &str) -> Result<Value, String> {
    let response = agent().get(url)
        .set("apikey", anon_key)
        .set("Authorization", &format!("Bearer {token}"))
        .set("Accept", "application/json")
        .call().map_err(http_error)?;
    let text = response.into_string().map_err(|e| format!("Could not read response: {e}"))?;
    serde_json::from_str(&text).map_err(|e| format!("Could not parse Supabase response: {e}"))
}

fn session_from(value: &Value, email: &str) -> Result<Session, String> {
    let access = value.get("access_token").and_then(Value::as_str)
        .ok_or_else(|| "Supabase did not return an access token".to_string())?;
    let refresh = value.get("refresh_token").and_then(Value::as_str).unwrap_or("");
    let expires_in = value.get("expires_in").and_then(Value::as_i64).unwrap_or(3600);
    let user = value.get("user").unwrap_or(&Value::Null);
    let user_id = user.get("id").and_then(Value::as_str)
        .ok_or_else(|| "Supabase did not return a user id".to_string())?;
    Ok(Session {
        access_token: access.to_string(),
        refresh_token: refresh.to_string(),
        expires_at: now_unix() + expires_in - 30,
        user_id: user_id.to_string(),
        email: email.to_string(),
    })
}

impl Cloud {
    pub fn new(url: String, anon_key: String) -> Self {
        Self { url, anon_key, session: None }
    }

    pub fn is_signed_in(&self) -> bool { self.session.is_some() }

    pub fn account(&self) -> Option<&str> {
        self.session.as_ref().map(|s| s.email.as_str())
    }

    pub fn configure(&mut self, url: String, anon_key: String) {
        self.url = url;
        self.anon_key = anon_key;
    }

    pub fn sign_in(&mut self, email: &str, password: &str) -> Result<(), String> {
        let url = endpoint(&self.url, "auth/v1/token?grant_type=password");
        let value = post_json(&url, &self.anon_key, None,
            &json!({ "email": email, "password": password }))?;
        let session = session_from(&value, email)?;
        self.persist(&session)?;
        self.session = Some(session);
        Ok(())
    }

    pub fn sign_up(&mut self, email: &str, password: &str) -> Result<bool, String> {
        let url = endpoint(&self.url, "auth/v1/signup");
        let value = post_json(&url, &self.anon_key, None,
            &json!({ "email": email, "password": password }))?;
        if value.get("access_token").is_some() {
            let session = session_from(&value, email)?;
            self.persist(&session)?;
            self.session = Some(session);
            return Ok(true);
        }
        Ok(false) // email confirmation required
    }

    pub fn sign_out(&mut self) -> Result<(), String> {
        if let Some(session) = &self.session {
            let url = endpoint(&self.url, "auth/v1/logout");
            let _ = post_json(&url, &self.anon_key, Some(&session.access_token), &json!({}));
        }
        let _ = std::fs::remove_file(session_file());
        let _ = std::fs::remove_file(account_file());
        // Signed-out devices keep no cloud baseline; the first sync after
        // signing in re-establishes it without duplicating known text.
        let _ = std::fs::remove_file(state_file());
        let _ = Command::new("secret-tool")
            .args(["clear", "service", "jot"])
            .stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null())
            .status();
        self.session = None;
        Ok(())
    }

    fn persist(&self, session: &Session) -> Result<(), String> {
        let json = serde_json::to_vec(session)
            .map_err(|e| format!("Could not encode session: {e}"))?;
        write_private(&session_file(), &json)?;
        write_private(&account_file(), session.email.as_bytes())?;
        let mut child = Command::new("secret-tool")
            .args(["store", "--label=Jot Supabase", "service", "jot", "account", &session.email])
            .stdin(Stdio::piped()).stdout(Stdio::null()).stderr(Stdio::null())
            .spawn();
        if let Ok(child) = &mut child {
            if let Some(stdin) = child.stdin.as_mut() {
                let _ = stdin.write_all(&json);
            }
            let _ = child.wait();
        }
        Ok(())
    }

    /// Restore the last session, refreshing it if needed. Returns false when
    /// no usable session exists; that is not an error.
    pub fn restore(&mut self) -> Result<bool, String> {
        let account = std::fs::read_to_string(account_file()).unwrap_or_default();
        let email = account.trim().to_string();
        if email.is_empty() { return Ok(false); }
        let keyring = Command::new("secret-tool")
            .args(["lookup", "service", "jot", "account", &email])
            .output().ok().filter(|o| o.status.success())
            .and_then(|o| serde_json::from_slice::<Session>(&o.stdout).ok());
        let session = keyring.or_else(|| {
            std::fs::read(session_file()).ok()
                .and_then(|bytes| serde_json::from_slice::<Session>(&bytes).ok())
        });
        let Some(mut session) = session else { return Ok(false); };
        if session.expires_at <= now_unix() {
            let url = endpoint(&self.url, "auth/v1/token?grant_type=refresh_token");
            let value = post_json(&url, &self.anon_key, None,
                &json!({ "refresh_token": session.refresh_token }))?;
            session = session_from(&value, &session.email)?;
            self.persist(&session)?;
        }
        self.session = Some(session);
        Ok(true)
    }

    fn bearer(&mut self) -> Result<String, String> {
        let session = self.session.as_ref().ok_or_else(|| "Not signed in".to_string())?;
        if session.expires_at > now_unix() {
            return Ok(session.access_token.clone());
        }
        let refresh = session.refresh_token.clone();
        let email = session.email.clone();
        let url = endpoint(&self.url, "auth/v1/token?grant_type=refresh_token");
        let value = post_json(&url, &self.anon_key, None,
            &json!({ "refresh_token": refresh }))?;
        let session = session_from(&value, &email)?;
        self.persist(&session)?;
        let token = session.access_token.clone();
        self.session = Some(session);
        Ok(token)
    }

    /// The first time a device sees the cloud, record a base only when the
    /// two pads already match. Otherwise the whole local pad is treated as
    /// local-only content to append on the next sync.
    pub fn set_baseline_from_local(&self, local: &str, remote: Option<&CloudPad>) -> Result<(), String> {
        let Some(session) = self.session.as_ref() else { return Ok(()) };
        if load_base(&session.user_id).is_some() { return Ok(()); }
        let Some(pad) = remote else { return Ok(()) };
        if without_trailing_newlines(local) != without_trailing_newlines(&pad.content) {
            return Ok(());
        }
        save_base(&session.user_id, &BaseState {
            content: pad.content.clone(), version: pad.version, user_id: String::new(),
        })
    }

    /// Three-way sync. Only automatic merge is appending local-only text at
    /// the bottom of the cloud pad; conflicting edits are reported, never
    /// overwritten. Returns the pad that should become the local content.
    pub fn sync(&mut self, local: &str) -> Result<CloudPad, String> {
        let remote = self.download()?;
        let user_id = self.session.as_ref().map(|session| session.user_id.clone())
            .ok_or_else(|| "Not signed in".to_string())?;
        let Some(remote) = remote else {
            let pad = self.upload(local, 0)?;
            save_base(&user_id, &BaseState {
                content: local.to_string(), version: pad.version, user_id: String::new(),
            })?;
            return Ok(pad);
        };
        let base = load_base(&user_id).filter(|state| !state.content.is_empty());
        match plan_sync(local, base.as_ref().map(|state| state.content.as_str()), &remote.content) {
            SyncPlan::Pull => {
                save_base(&user_id, &BaseState {
                    content: remote.content.clone(), version: remote.version,
                    user_id: String::new(),
                })?;
                Ok(remote)
            }
            SyncPlan::Upload(content) => {
                let pad = self.upload(&content, remote.version)?;
                save_base(&user_id, &BaseState {
                    content, version: pad.version, user_id: String::new(),
                })?;
                Ok(pad)
            }
            SyncPlan::Conflict => Err("Local and cloud notes have both changed; nothing was overwritten. Download or back up manually after reviewing.".into()),
        }
    }

    pub fn download(&mut self) -> Result<Option<CloudPad>, String> {
        let token = self.bearer()?;
        let user_id = self.session.as_ref().unwrap().user_id.clone();
        let url = endpoint(&self.url,
            &format!("rest/v1/pads?select=content,updated_at,version&user_id=eq.{user_id}"));
        let value = get_json(&url, &self.anon_key, &token)?;
        let rows = value.as_array().ok_or_else(|| "Unexpected pads response".to_string())?;
        let Some(row) = rows.first() else { return Ok(None); };
        Ok(Some(CloudPad {
            content: row.get("content").and_then(Value::as_str).unwrap_or("").to_string(),
            updated_at: row.get("updated_at").and_then(Value::as_str).unwrap_or("").to_string(),
            version: row.get("version").and_then(Value::as_i64).unwrap_or(0),
        }))
    }

    /// Upload the pad. `version` is the last downloaded version; 0 means the
    /// first upload. A conflict is returned as an error so the caller can
    /// decide rather than overwriting cloud notes.
    pub fn upload(&mut self, content: &str, version: i64) -> Result<CloudPad, String> {
        // Always compare against the remote before writing. A version 0 upload
        // must not overwrite a pad that already exists in the cloud.
        let remote = self.download()?;
        if version <= 0 && remote.is_some() {
            return Err("A cloud pad already exists; download it before uploading.".into());
        }
        if version > 0 && remote.as_ref().map(|pad| pad.version) != Some(version) {
            return Err("The cloud pad changed since it was loaded; sync was cancelled.".into());
        }
        let token = self.bearer()?;
        let session = self.session.as_ref().unwrap();
        let user_id = session.user_id.clone();
        let updated_at = rfc3339(now_unix());
        let result = if remote.is_none() {
            let url = endpoint(&self.url, "rest/v1/pads");
            let request = agent().post(&url)
                .set("apikey", &self.anon_key)
                .set("Authorization", &format!("Bearer {token}"))
                .set("Content-Type", "application/json")
                .set("Prefer", "return=representation,resolution=merge-duplicates");
            let body = json!({
                "user_id": user_id, "content": content,
                "updated_at": updated_at, "version": 1,
            });
            request.send_string(&body.to_string()).map_err(http_error)?
        } else {
            let url = endpoint(&self.url,
                &format!("rest/v1/pads?user_id=eq.{user_id}&version=eq.{version}"));
            let request = agent().patch(&url)
                .set("apikey", &self.anon_key)
                .set("Authorization", &format!("Bearer {token}"))
                .set("Content-Type", "application/json")
                .set("Prefer", "return=representation");
            let body = json!({
                "content": content, "updated_at": updated_at, "version": version + 1,
            });
            let response = request.send_string(&body.to_string());
            match response {
                Ok(response) => response,
                Err(ureq::Error::Status(404, response)) => {
                    let _ = response.into_string();
                    return Err("The cloud pad changed since it was loaded; sync was cancelled.".into());
                }
                Err(error) => return Err(http_error(error)),
            }
        };
        let text = result.into_string().map_err(|e| format!("Could not read response: {e}"))?;
        let rows: Value = serde_json::from_str(&text)
            .map_err(|e| format!("Could not parse Supabase response: {e}"))?;
        let row = rows.as_array().and_then(|r| r.first())
            .ok_or_else(|| "Supabase did not return the saved pad".to_string())?;
        Ok(CloudPad {
            content: row.get("content").and_then(Value::as_str).unwrap_or("").to_string(),
            updated_at: row.get("updated_at").and_then(Value::as_str).unwrap_or(&updated_at).to_string(),
            version: row.get("version").and_then(Value::as_i64).unwrap_or(1),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn append_preserves_both_sides() {
        assert_eq!(append_text("cloud notes", "phone notes"), "cloud notes\n\nphone notes");
        assert_eq!(append_text("", "local only"), "local only");
        assert_eq!(append_text("cloud", ""), "cloud");
        assert_eq!(append_text("cloud\n\n", "\nphone"), "cloud\n\nphone");
    }

    #[test]
    fn no_base_never_duplicates_known_text() {
        // Re-signing in on the same device must not append its own notes again.
        assert_eq!(plan_sync("same notes", None, "same notes"), SyncPlan::Pull);
        assert_eq!(plan_sync("same notes\n", None, "same notes"), SyncPlan::Pull);
        // The phone already has the laptop notes plus its own suffix.
        assert_eq!(
            plan_sync("laptop\n\nphone", None, "laptop"),
            SyncPlan::Upload("laptop\n\nphone".into())
        );
        // Local notes are already part of a merged cloud pad.
        assert_eq!(
            plan_sync("phone", None, "laptop\n\nphone"),
            SyncPlan::Pull
        );
    }

    #[test]
    fn no_base_appends_only_new_text() {
        assert_eq!(
            plan_sync("phone-only", None, "laptop-only"),
            SyncPlan::Upload("laptop-only\n\nphone-only".into())
        );
    }

    #[test]
    fn base_distinguishes_pull_push_and_conflict() {
        assert_eq!(plan_sync("base", Some("base"), "remote"), SyncPlan::Pull);
        assert_eq!(
            plan_sync("base changed", Some("base"), "base"),
            SyncPlan::Upload("base changed".into())
        );
        assert_eq!(
            plan_sync("base\n\nphone", Some("base"), "base\n\nlaptop"),
            SyncPlan::Upload("base\n\nlaptop\n\nphone".into())
        );
        assert_eq!(
            plan_sync("rewritten", Some("base"), "remote"),
            SyncPlan::Conflict
        );
    }
}
