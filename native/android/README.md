# Jot for Android

A Kotlin/Compose app for the Jot notepad. Notes are local first and save to a
plain file on the phone. Cloud transfer is explicit: nothing syncs by itself.

## Requirements

- JDK 21
- Android SDK platform 35 and build-tools 35

No NDK or Rust toolchain is needed for the Android app.

## Local build

```sh
cp native/android/local.properties.example native/android/local.properties
# set sdk.dir in local.properties
cp native/android/secrets.properties.example native/android/secrets.properties
# set SUPABASE_URL and SUPABASE_ANON_KEY in secrets.properties

cd native/android
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

A release APK is produced with `./gradlew assembleRelease`. Without a keystore
it is signed with the debug key, which is fine for personal sideloading.

## Cloud config

Set cloud config in `secrets.properties` (or the environment):

```
SUPABASE_URL=https://your-project.supabase.co
SUPABASE_ANON_KEY=your-anon-key
```

Keep SDK/signing paths in `local.properties`. Android Studio may rewrite that
file when changing SDK locations; keeping cloud config separate avoids a build
silently embedding an empty URL.

## Signing

Set release signing values in `local.properties` or the environment:

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

The phone is deliberately explicit; the desktop app is unchanged.

- Notes always save on the phone first. Nothing touches the cloud by itself.
- **Get latest** downloads the cloud copy and replaces the notes on this phone.
- **Back up now** uploads the notes on this phone and replaces the cloud copy.
- Both actions live in Settings and in the one-line bar at the bottom of the
  editor, next to the current sync status. The bar hides while the keyboard is
  open.
- Jot asks for confirmation before replacing a copy that has unsaved changes.
  It never appends dated duplicates or merges two divergent pads.
- Status reads *Not backed up*, *Got latest · HH:mm*, *Backed up · HH:mm*, or a
  short error. No background sync is scheduled.

## Migration

If an older Jot Android build stored notes in `pad.db`, the first launch of
this version reads that database once and carries the text over to the new
plain-text note file. The old database is left in place.

## Layout

```
app/src/main/   Kotlin/Compose UI, local storage, Supabase client
app/src/test/   JVM tests for date tags and the two cloud-button rules
```
