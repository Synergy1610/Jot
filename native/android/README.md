# Jot for Android

A Kotlin/Compose shell around the same Rust core and SQLite database as the
desktop app. Notes are local first; cloud backup is append-only and never
overwrites either side.

## Requirements

- JDK 21
- Android SDK platform 35 and build-tools 35
- Android NDK r27 or newer
- Rust targets `aarch64-linux-android` (and `x86_64-linux-android` for the emulator)
- `cargo install cargo-ndk --locked`

## Local build

```sh
cp native/android/local.properties.example native/android/local.properties
# set sdk.dir, SUPABASE_URL and SUPABASE_ANON_KEY

export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.2.12479018
JOT_ABIS="arm64-v8a x86_64" native/android/build-rust.sh

cd native/android
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

`build-rust.sh` cross-compiles `native/core` plus the JNI shim in `jni/` and
copies `libjot_android.so` into `app/src/main/jniLibs`. The libraries are build
outputs and are not committed.

A release APK is produced with `./gradlew assembleRelease`. Without a keystore
it is signed with the debug key, which is fine for personal sideloading.

## Signing

Set these in `local.properties` or the environment:

```
JOT_KEYSTORE=/absolute/path/to/jot-release.jks
JOT_KEYSTORE_PASSWORD=…
JOT_KEY_ALIAS=jot
JOT_KEY_PASSWORD=…
```

Generate the key once and keep it safe:

```sh
keytool -genkeypair -v -keystore jot-release.jks -alias jot \
  -keyalg RSA -keysize 4096 -validity 10000
```

Updates only install over builds signed with the same key. Losing the keystore
means uninstalling and reinstalling, so back it up.

## Sync behaviour

- Nothing touches the network while you type.
- On launch the app may **pull only**, and only when this device has no
  unsynced text (the local pad is empty or matches the last synced baseline).
- Leaving the app (background) runs the normal three-way sync; a WorkManager
  job retries later if the process is killed first.
- **Back up now** syncs immediately. **Download** deliberately replaces this
  phone's notes with the cloud copy and asks first.
- If both sides changed, the core reports a conflict and writes nothing.

## Layout

```
jni/            Rust cdylib exposing the C core over JNI
app/src/main/   Kotlin/Compose UI, worker, manifest, icons
app/src/test/   JVM tests for the date-tag behaviour
```

## CI

`.github/workflows/android.yml` builds on `android-v*` tags and publishes a
signed APK plus `SHA256SUMS` to the GitHub release. It needs repository secrets
`SUPABASE_URL`, `SUPABASE_ANON_KEY`, and optionally `JOT_KEYSTORE_B64`,
`JOT_KEYSTORE_PASSWORD`, `JOT_KEY_ALIAS`, `JOT_KEY_PASSWORD`. Without the
keystore secrets the workflow still publishes a debug-signed APK.
