package app.jot

// Loading happens when this file's class is initialised, before any of the
// external functions below can run.
private val coreLibrary = run {
    System.loadLibrary("jot_android")
    true
}

external fun nativeSetConfigDir(path: String): Boolean

external fun nativeCreate(path: String): String
external fun nativeOpen(path: String): String
external fun nativeLoad(handle: Long): String
external fun nativeSave(handle: Long, expected: String, content: String): String
external fun nativeClear(handle: Long, expected: String, current: String): String
external fun nativeLastCleared(handle: Long): String
external fun nativeUndoClear(handle: Long, backup: String): String
external fun nativeBackup(handle: Long, path: String): String
external fun nativeClose(handle: Long)

external fun nativeCloudConfigure(url: String, anonKey: String): String
external fun nativeCloudRestore(): String
external fun nativeCloudIsSignedIn(): Boolean
external fun nativeCloudBaseContent(): String
external fun nativeCloudAccount(): String
external fun nativeCloudSignIn(email: String, password: String): String
external fun nativeCloudSignUp(email: String, password: String): String
external fun nativeCloudSignOut(): String
external fun nativeCloudDownload(): String
external fun nativeCloudUpload(content: String, version: Long): String
external fun nativeCloudSync(localContent: String): String
external fun nativeCloudSetBaseline(local: String, remote: String, version: Long): String
