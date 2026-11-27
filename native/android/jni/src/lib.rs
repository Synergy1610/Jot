//! JNI surface for the Android shell.
//!
//! Every call returns a small JSON envelope so Kotlin never has to guess at
//! out-parameters: `{"ok":true,"value":…}` or `{"ok":false,"error":"…"}`.
//! All work happens on one Kotlin dispatcher thread, so the single SQLite
//! connection is never touched concurrently.

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use jni::objects::{JClass, JString};
use jni::sys::{jboolean, jlong, jstring};
use jni::JNIEnv;
use serde_json::{json, Value};

use jot_core::Pad;

fn java_text(env: &mut JNIEnv, value: &JString) -> Result<String, String> {
    env.get_string(value)
        .map(|text| text.to_string_lossy().into_owned())
        .map_err(|_| "Could not read text from Android".to_string())
}

fn c_text(value: &str) -> Result<CString, String> {
    CString::new(value).map_err(|_| "Text contains a NUL byte".to_string())
}

fn reply(env: &mut JNIEnv, payload: Value) -> jstring {
    match env.new_string(payload.to_string()) {
        Ok(text) => text.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

fn ok(env: &mut JNIEnv, value: Value) -> jstring {
    reply(env, json!({ "ok": true, "value": value }))
}

fn ok_empty(env: &mut JNIEnv) -> jstring {
    reply(env, json!({ "ok": true }))
}

fn fail(env: &mut JNIEnv, message: impl Into<String>) -> jstring {
    reply(env, json!({ "ok": false, "error": message.into() }))
}

/// Take ownership of a string allocated by the core and free it.
unsafe fn take(pointer: *mut c_char) -> String {
    if pointer.is_null() { return String::new(); }
    let value = CStr::from_ptr(pointer).to_string_lossy().into_owned();
    jot_core::jot_core_string_free(pointer);
    value
}

unsafe fn take_error(error: *mut *mut c_char) -> String {
    if error.is_null() { return String::new(); }
    take(*error)
}

fn error_text(message: String, fallback: &str) -> String {
    if message.is_empty() { fallback.to_string() } else { message }
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeSetConfigDir(
    mut env: JNIEnv, _class: JClass, path: JString,
) -> jboolean {
    let path = match java_text(&mut env, &path) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let Ok(cpath) = c_text(&path) else { return 0 };
    (unsafe { jot_core::jot_core_set_config_dir(cpath.as_ptr()) }) as jboolean
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCreate(
    mut env: JNIEnv, _class: JClass, path: JString,
) -> jstring {
    let path = match java_text(&mut env, &path) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let Ok(cpath) = c_text(&path) else {
        return fail(&mut env, "Path contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let created = unsafe { jot_core::jot_core_create(cpath.as_ptr(), &mut error) };
    if !created {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not create the notes database"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeOpen(
    mut env: JNIEnv, _class: JClass, path: JString,
) -> jstring {
    let path = match java_text(&mut env, &path) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let cpath = match c_text(&path) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let mut error: *mut c_char = ptr::null_mut();
    let handle = unsafe { jot_core::jot_core_open(cpath.as_ptr(), &mut error) };
    if handle.is_null() {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not open the notes database"));
    }
    ok(&mut env, json!(handle as i64))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeLoad(
    mut env: JNIEnv, _class: JClass, handle: jlong,
) -> jstring {
    let mut error: *mut c_char = ptr::null_mut();
    let content = unsafe { jot_core::jot_core_load(handle as *mut Pad, &mut error) };
    if content.is_null() {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not read the notes"));
    }
    ok(&mut env, json!(unsafe { take(content) }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeSave(
    mut env: JNIEnv, _class: JClass, handle: jlong, expected: JString, content: JString,
) -> jstring {
    let expected = match java_text(&mut env, &expected) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let content = match java_text(&mut env, &content) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let (Ok(expected), Ok(content)) = (c_text(&expected), c_text(&content)) else {
        return fail(&mut env, "Text contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let saved = unsafe {
        jot_core::jot_core_save(handle as *mut Pad, expected.as_ptr(), content.as_ptr(), &mut error)
    };
    if !saved {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not save the notes"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeClear(
    mut env: JNIEnv, _class: JClass, handle: jlong, expected: JString, current: JString,
) -> jstring {
    let expected = match java_text(&mut env, &expected) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let current = match java_text(&mut env, &current) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let (Ok(expected), Ok(current)) = (c_text(&expected), c_text(&current)) else {
        return fail(&mut env, "Text contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let cleared = unsafe {
        jot_core::jot_core_clear(
            handle as *mut Pad, expected.as_ptr(), current.as_ptr(), &mut error,
        )
    };
    if !cleared {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not clear the notes"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeLastCleared(
    mut env: JNIEnv, _class: JClass, handle: jlong,
) -> jstring {
    let mut error: *mut c_char = ptr::null_mut();
    let backup = unsafe { jot_core::jot_core_last_cleared(handle as *mut Pad, &mut error) };
    if backup.is_null() {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "No cleared notes are available"));
    }
    ok(&mut env, json!(unsafe { take(backup) }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeUndoClear(
    mut env: JNIEnv, _class: JClass, handle: jlong, backup: JString,
) -> jstring {
    let backup = match java_text(&mut env, &backup) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let Ok(backup) = c_text(&backup) else {
        return fail(&mut env, "Text contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let undone = unsafe {
        jot_core::jot_core_undo_clear(handle as *mut Pad, backup.as_ptr(), &mut error)
    };
    if !undone {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not restore the cleared notes"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeBackup(
    mut env: JNIEnv, _class: JClass, handle: jlong, path: JString,
) -> jstring {
    let path = match java_text(&mut env, &path) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let Ok(path) = c_text(&path) else {
        return fail(&mut env, "Path contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let backed_up = unsafe {
        jot_core::jot_core_backup(handle as *mut Pad, path.as_ptr(), &mut error)
    };
    if !backed_up {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not back up the notes"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeClose(
    _env: JNIEnv, _class: JClass, handle: jlong,
) {
    if handle != 0 {
        unsafe { jot_core::jot_core_close(handle as *mut Pad) };
    }
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudConfigure(
    mut env: JNIEnv, _class: JClass, url: JString, anon_key: JString,
) -> jstring {
    let url = match java_text(&mut env, &url) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let anon_key = match java_text(&mut env, &anon_key) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let (Ok(url), Ok(anon_key)) = (c_text(&url), c_text(&anon_key)) else {
        return fail(&mut env, "Configuration contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let configured = unsafe {
        jot_core::jot_cloud_configure(url.as_ptr(), anon_key.as_ptr(), &mut error)
    };
    if !configured {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not configure cloud backup"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudRestore(
    mut env: JNIEnv, _class: JClass,
) -> jstring {
    let mut error: *mut c_char = ptr::null_mut();
    let restored = unsafe { jot_core::jot_cloud_restore(&mut error) };
    if !restored {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not restore the cloud session"));
    }
    ok(&mut env, json!(unsafe { jot_core::jot_cloud_is_signed_in() }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudIsSignedIn(
    _env: JNIEnv, _class: JClass,
) -> jboolean {
    (unsafe { jot_core::jot_cloud_is_signed_in() }) as jboolean
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudBaseContent(
    mut env: JNIEnv, _class: JClass,
) -> jstring {
    let base = unsafe { jot_core::jot_cloud_base_content() };
    if base.is_null() {
        return ok(&mut env, Value::Null);
    }
    ok(&mut env, json!(unsafe { take(base) }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudAccount(
    mut env: JNIEnv, _class: JClass,
) -> jstring {
    let account = unsafe { jot_core::jot_cloud_account() };
    if account.is_null() {
        return ok(&mut env, Value::Null);
    }
    ok(&mut env, json!(unsafe { take(account) }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudSignIn(
    mut env: JNIEnv, _class: JClass, email: JString, password: JString,
) -> jstring {
    let email = match java_text(&mut env, &email) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let password = match java_text(&mut env, &password) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let (Ok(email), Ok(password)) = (c_text(&email), c_text(&password)) else {
        return fail(&mut env, "Credentials contain a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let signed_in = unsafe {
        jot_core::jot_cloud_sign_in(email.as_ptr(), password.as_ptr(), &mut error)
    };
    if !signed_in {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not sign in"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudSignUp(
    mut env: JNIEnv, _class: JClass, email: JString, password: JString,
) -> jstring {
    let email = match java_text(&mut env, &email) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let password = match java_text(&mut env, &password) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let (Ok(email), Ok(password)) = (c_text(&email), c_text(&password)) else {
        return fail(&mut env, "Credentials contain a NUL byte");
    };
    let mut needs_confirmation = false;
    let mut error: *mut c_char = ptr::null_mut();
    let signed_up = unsafe {
        jot_core::jot_cloud_sign_up(
            email.as_ptr(), password.as_ptr(), &mut needs_confirmation, &mut error,
        )
    };
    if !signed_up {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not create the account"));
    }
    ok(&mut env, json!({ "needsConfirmation": needs_confirmation }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudSignOut(
    mut env: JNIEnv, _class: JClass,
) -> jstring {
    let mut error: *mut c_char = ptr::null_mut();
    let signed_out = unsafe { jot_core::jot_cloud_sign_out(&mut error) };
    if !signed_out {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not sign out"));
    }
    ok_empty(&mut env)
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudDownload(
    mut env: JNIEnv, _class: JClass,
) -> jstring {
    let mut content: *mut c_char = ptr::null_mut();
    let mut updated_at: *mut c_char = ptr::null_mut();
    let mut version: i64 = 0;
    let mut error: *mut c_char = ptr::null_mut();
    let downloaded = unsafe {
        jot_core::jot_cloud_download(&mut content, &mut updated_at, &mut version, &mut error)
    };
    if !downloaded {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not download the cloud notes"));
    }
    if content.is_null() {
        return ok(&mut env, Value::Null);
    }
    let content = unsafe { take(content) };
    let updated_at = unsafe { take(updated_at) };
    ok(&mut env, json!({
        "content": content,
        "updatedAt": updated_at,
        "version": version,
    }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudUpload(
    mut env: JNIEnv, _class: JClass, content: JString, version: jlong,
) -> jstring {
    let content = match java_text(&mut env, &content) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let Ok(content) = c_text(&content) else {
        return fail(&mut env, "Text contains a NUL byte");
    };
    let mut new_version: i64 = 0;
    let mut error: *mut c_char = ptr::null_mut();
    let uploaded = unsafe {
        jot_core::jot_cloud_upload(
            content.as_ptr(), version as i64, &mut new_version, &mut error,
        )
    };
    if !uploaded {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not back up the notes"));
    }
    ok(&mut env, json!(new_version))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudSync(
    mut env: JNIEnv, _class: JClass, local_content: JString,
) -> jstring {
    let local_content = match java_text(&mut env, &local_content) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let Ok(local_content) = c_text(&local_content) else {
        return fail(&mut env, "Text contains a NUL byte");
    };
    let mut merged: *mut c_char = ptr::null_mut();
    let mut version: i64 = 0;
    let mut error: *mut c_char = ptr::null_mut();
    let synced = unsafe {
        jot_core::jot_cloud_sync(local_content.as_ptr(), &mut merged, &mut version, &mut error)
    };
    if !synced {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not sync the notes"));
    }
    let merged = unsafe { take(merged) };
    ok(&mut env, json!({ "merged": merged, "version": version }))
}

#[no_mangle]
pub extern "system" fn Java_app_jot_NativeKt_nativeCloudSetBaseline(
    mut env: JNIEnv, _class: JClass,
    local_content: JString, remote_content: JString, version: jlong,
) -> jstring {
    let local_content = match java_text(&mut env, &local_content) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let remote_content = match java_text(&mut env, &remote_content) {
        Ok(value) => value,
        Err(message) => return fail(&mut env, message),
    };
    let (Ok(local_content), Ok(remote_content)) =
        (c_text(&local_content), c_text(&remote_content)) else {
        return fail(&mut env, "Text contains a NUL byte");
    };
    let mut error: *mut c_char = ptr::null_mut();
    let stored = unsafe {
        jot_core::jot_cloud_set_baseline(
            local_content.as_ptr(), remote_content.as_ptr(), version as i64, &mut error,
        )
    };
    if !stored {
        let message = unsafe { take_error(&mut error) };
        return fail(&mut env, error_text(message, "Could not record the sync baseline"));
    }
    ok_empty(&mut env)
}
