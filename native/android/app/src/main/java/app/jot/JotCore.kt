package app.jot

import org.json.JSONObject
import java.io.Closeable
import java.io.File

class JotException(message: String) : Exception(message)

class CloudPad(val content: String, val updatedAt: String, val version: Long)

/** Every core call replies with `{"ok":…}`; this turns that into an exception. */
private fun reply(raw: String): JSONObject {
    if (raw.isEmpty()) throw JotException("Jot core did not respond")
    val json = JSONObject(raw)
    if (!json.optBoolean("ok")) {
        throw JotException(json.optString("error").ifEmpty { "Jot core failed" })
    }
    return json
}

private fun JSONObject.valueOrNull(): JSONObject? =
    if (isNull("value")) null else optJSONObject("value")

private fun JSONObject.stringOrNull(): String? =
    if (isNull("value")) null else optString("value")

/**
 * Thin wrapper over the shared Rust core. Not thread safe: the app funnels
 * every call through one dispatcher, and the background worker only runs
 * while the app is not in front.
 */
class JotCore(private val dbPath: String, private val configDir: String) : Closeable {
    private var handle = 0L

    fun open(): String {
        nativeSetConfigDir(configDir)
        if (!File(dbPath).exists()) reply(nativeCreate(dbPath))
        handle = reply(nativeOpen(dbPath)).optLong("value")
        return load()
    }

    fun load(): String = reply(nativeLoad(handle)).optString("value")

    fun save(expected: String, content: String) {
        reply(nativeSave(handle, expected, content))
    }

    fun clear(expected: String, current: String) {
        reply(nativeClear(handle, expected, current))
    }

    fun lastCleared(): String? = reply(nativeLastCleared(handle)).stringOrNull()

    fun undoClear(backup: String) {
        reply(nativeUndoClear(handle, backup))
    }

    fun backup(path: String) {
        reply(nativeBackup(handle, path))
    }

    fun configure(url: String, anonKey: String) {
        if (url.isBlank() || anonKey.isBlank()) return
        reply(nativeCloudConfigure(url, anonKey))
    }

    fun restore(): Boolean = reply(nativeCloudRestore()).optBoolean("value")

    fun signedIn(): Boolean = nativeCloudIsSignedIn()

    fun account(): String? = reply(nativeCloudAccount()).stringOrNull()

    fun baseContent(): String? = reply(nativeCloudBaseContent()).stringOrNull()

    fun signIn(email: String, password: String) {
        reply(nativeCloudSignIn(email, password))
    }

    /** Returns true when Supabase wants the address confirmed first. */
    fun signUp(email: String, password: String): Boolean =
        reply(nativeCloudSignUp(email, password))
            .optJSONObject("value")?.optBoolean("needsConfirmation") ?: false

    fun signOut() {
        reply(nativeCloudSignOut())
    }

    fun download(): CloudPad? {
        val value = reply(nativeCloudDownload()).valueOrNull() ?: return null
        return CloudPad(
            content = value.optString("content"),
            updatedAt = value.optString("updatedAt"),
            version = value.optLong("version"),
        )
    }

    fun upload(content: String, version: Long): Long =
        reply(nativeCloudUpload(content, version)).optLong("value")

    fun sync(localContent: String): CloudPad {
        val value = reply(nativeCloudSync(localContent)).valueOrNull()
            ?: throw JotException("Jot core returned no sync result")
        return CloudPad(
            content = value.optString("merged"),
            updatedAt = "",
            version = value.optLong("version"),
        )
    }

    fun setBaseline(local: String, remote: String, version: Long) {
        reply(nativeCloudSetBaseline(local, remote, version))
    }

    override fun close() {
        if (handle != 0L) {
            nativeClose(handle)
            handle = 0L
        }
    }
}
