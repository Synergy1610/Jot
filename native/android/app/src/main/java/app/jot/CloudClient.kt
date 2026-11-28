package app.jot

import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.time.Instant
import java.util.concurrent.TimeUnit

data class CloudSession(
    val accessToken: String,
    val refreshToken: String,
    val expiresAt: Long,
    val userId: String,
    val email: String,
)

data class CloudPad(val content: String, val updatedAt: String, val version: Long)

data class CloudBase(val content: String, val version: Long, val userId: String)

class CloudException(message: String) : Exception(message)

/**
 * Direct Supabase client. Mirrors the desktop Rust core's cloud behavior closely
 * enough that the same pad row and session files keep working across devices.
 */
class CloudClient(private val configDir: File) {
    private val http = OkHttpClient.Builder()
        .callTimeout(15, TimeUnit.SECONDS)
        .build()
    private val jsonMedia = "application/json".toMediaType()

    var url: String = ""
        private set
    var anonKey: String = ""
        private set
    var session: CloudSession? = null
        private set

    private val sessionFile get() = File(configDir, "cloud-session.json")
    private val accountFile get() = File(configDir, "cloud-account")
    private val stateFile get() = File(configDir, "cloud-state.json")

    fun configure(url: String, anonKey: String) {
        val trimmed = url.trim().trimEnd('/')
        require(trimmed.startsWith("https://")) { "Supabase URL is missing or invalid in this build" }
        require(anonKey.isNotBlank()) { "Supabase anon key is missing in this build" }
        this.url = trimmed
        this.anonKey = anonKey.trim()
    }

    fun isSignedIn(): Boolean = session != null
    fun account(): String? = session?.email

    fun restore(): Boolean {
        val account = accountFile.takeIf { it.exists() }?.readText()?.trim().orEmpty()
        if (account.isEmpty()) return false
        val saved = readSession() ?: return false
        val active = if (saved.expiresAt <= nowUnix()) refresh(saved) else saved
        session = active
        return true
    }

    fun signIn(email: String, password: String) {
        val value = postJson(
            endpoint("auth/v1/token?grant_type=password"),
            token = null,
            body = JSONObject().put("email", email).put("password", password),
        )
        val next = sessionFrom(value, email)
        persist(next)
        session = next
    }

    /** Returns true when Supabase wants the address confirmed first. */
    fun signUp(email: String, password: String): Boolean {
        val value = postJson(
            endpoint("auth/v1/signup"),
            token = null,
            body = JSONObject().put("email", email).put("password", password),
        )
        if (value.has("access_token") && !value.isNull("access_token")) {
            val next = sessionFrom(value, email)
            persist(next)
            session = next
            return false
        }
        return true
    }

    fun signOut() {
        val current = session
        if (current != null) {
            runCatching {
                postJson(endpoint("auth/v1/logout"), token = current.accessToken, body = null)
            }
        }
        session = null
        sessionFile.delete()
        accountFile.delete()
        // Keep cloud-state.json: it is harmless and helps if the same account signs back in.
    }

    fun loadBase(): CloudBase? {
        val userId = session?.userId ?: return null
        val obj = runCatching { JSONObject(stateFile.readText()) }.getOrNull() ?: return null
        val stateUser = obj.optString("user_id")
        if (stateUser.isNotEmpty() && stateUser != userId) return null
        return CloudBase(
            content = obj.optString("content"),
            version = obj.optLong("version"),
            userId = stateUser.ifEmpty { userId },
        )
    }

    fun saveBase(content: String, version: Long) {
        val userId = session?.userId ?: return
        configDir.mkdirs()
        val obj = JSONObject()
            .put("content", content)
            .put("version", version)
            .put("user_id", userId)
        atomicWrite(stateFile, obj.toString())
    }

    fun download(): CloudPad? {
        val token = bearer()
        val userId = session!!.userId
        val request = Request.Builder()
            .url(endpoint("rest/v1/pads?select=content,updated_at,version&user_id=eq.$userId"))
            .header("apikey", anonKey)
            .header("Authorization", "Bearer $token")
            .header("Accept", "application/json")
            .get()
            .build()
        val body = execute(request)
        val rows = JSONArray(body)
        if (rows.length() == 0) return null
        val row = rows.getJSONObject(0)
        return CloudPad(
            content = row.optString("content"),
            updatedAt = row.optString("updated_at"),
            version = row.optLong("version"),
        )
    }

    fun upload(content: String, expectedVersion: Long): CloudPad {
        // Mirror cloud.rs: always check remote first.
        val remote = download()
        if (expectedVersion <= 0 && remote != null) {
            throw CloudException("A cloud pad already exists; download it before uploading.")
        }
        if (expectedVersion > 0 && remote?.version != expectedVersion) {
            throw CloudException("The cloud pad changed since it was loaded; sync was cancelled.")
        }
        val token = bearer()
        val userId = session!!.userId
        val updatedAt = Instant.now().toString()
        val nextVersion = if (remote == null) 1L else expectedVersion + 1
        val payload = JSONObject()
            .put("content", content)
            .put("updated_at", updatedAt)
            .put("version", nextVersion)
        val request = if (remote == null) {
            payload.put("user_id", userId)
            Request.Builder()
                .url(endpoint("rest/v1/pads"))
                .header("apikey", anonKey)
                .header("Authorization", "Bearer $token")
                .header("Content-Type", "application/json")
                .header("Prefer", "return=representation,resolution=merge-duplicates")
                .post(payload.toString().toRequestBody(jsonMedia))
                .build()
        } else {
            Request.Builder()
                .url(endpoint("rest/v1/pads?user_id=eq.$userId&version=eq.$expectedVersion"))
                .header("apikey", anonKey)
                .header("Authorization", "Bearer $token")
                .header("Content-Type", "application/json")
                .header("Prefer", "return=representation")
                .patch(payload.toString().toRequestBody(jsonMedia))
                .build()
        }
        val body = execute(request)
        val rows = JSONArray(body)
        if (rows.length() == 0) {
            throw CloudException("The cloud pad changed since it was loaded; sync was cancelled.")
        }
        val row = rows.getJSONObject(0)
        return CloudPad(
            content = row.optString("content", content),
            updatedAt = row.optString("updated_at", updatedAt),
            version = row.optLong("version", nextVersion),
        )
    }

    private fun bearer(): String {
        val current = session ?: throw CloudException("Not signed in")
        if (current.expiresAt > nowUnix()) return current.accessToken
        val refreshed = refresh(current)
        session = refreshed
        return refreshed.accessToken
    }

    private fun refresh(current: CloudSession): CloudSession {
        val value = postJson(
            endpoint("auth/v1/token?grant_type=refresh_token"),
            token = null,
            body = JSONObject().put("refresh_token", current.refreshToken),
        )
        val next = sessionFrom(value, current.email)
        persist(next)
        return next
    }

    private fun persist(session: CloudSession) {
        configDir.mkdirs()
        val obj = JSONObject()
            .put("access_token", session.accessToken)
            .put("refresh_token", session.refreshToken)
            .put("expires_at", session.expiresAt)
            .put("user_id", session.userId)
            .put("email", session.email)
        atomicWrite(sessionFile, obj.toString())
        atomicWrite(accountFile, session.email)
    }

    private fun readSession(): CloudSession? {
        val obj = runCatching { JSONObject(sessionFile.readText()) }.getOrNull() ?: return null
        val access = obj.optString("access_token")
        val user = obj.optString("user_id")
        val email = obj.optString("email")
        if (access.isBlank() || user.isBlank()) return null
        return CloudSession(
            accessToken = access,
            refreshToken = obj.optString("refresh_token"),
            expiresAt = obj.optLong("expires_at"),
            userId = user,
            email = email,
        )
    }

    private fun sessionFrom(value: JSONObject, email: String): CloudSession {
        val access = value.optString("access_token")
        if (access.isBlank()) throw CloudException("Supabase did not return an access token")
        val refresh = value.optString("refresh_token")
        val expiresIn = value.optLong("expires_in", 3600)
        val user = value.optJSONObject("user")
        val userId = user?.optString("id").orEmpty()
        if (userId.isBlank()) throw CloudException("Supabase did not return a user id")
        return CloudSession(
            accessToken = access,
            refreshToken = refresh,
            expiresAt = nowUnix() + expiresIn - 30,
            userId = userId,
            email = email,
        )
    }

    private fun endpoint(path: String): String = "$url/${path.trimStart('/')}"

    private fun postJson(url: String, token: String?, body: JSONObject?): JSONObject {
        val builder = Request.Builder()
            .url(url)
            .header("apikey", anonKey)
            .header("Content-Type", "application/json")
        if (token != null) builder.header("Authorization", "Bearer $token")
        val requestBody = (body?.toString() ?: "").toRequestBody(jsonMedia)
        builder.post(requestBody)
        val text = execute(builder.build())
        if (text.isBlank()) return JSONObject()
        return runCatching { JSONObject(text) }.getOrElse {
            throw CloudException("Could not parse Supabase response")
        }
    }

    private fun execute(request: Request): String {
        try {
            http.newCall(request).execute().use { response ->
                val body = response.body?.string().orEmpty()
                if (!response.isSuccessful) throw CloudException(httpMessage(response.code, body))
                return body
            }
        } catch (e: CloudException) {
            throw e
        } catch (e: Exception) {
            throw CloudException("Could not reach the cloud")
        }
    }

    private fun httpMessage(code: Int, body: String): String {
        val detail = runCatching { JSONObject(body) }.getOrNull()
        val message = detail?.optString("msg")?.takeIf { it.isNotBlank() }
            ?: detail?.optString("message")?.takeIf { it.isNotBlank() }
            ?: detail?.optString("error_description")?.takeIf { it.isNotBlank() }
            ?: body
        return "Supabase returned $code: ${message.take(200)}"
    }

    private fun nowUnix(): Long = System.currentTimeMillis() / 1000L

    private fun atomicWrite(file: File, text: String) {
        file.parentFile?.mkdirs()
        val tmp = File(file.parentFile, file.name + ".tmp")
        tmp.writeText(text)
        // Best-effort durability; rename is atomic on the same filesystem.
        runCatching { tmp.setReadable(true, true) }
        if (!tmp.renameTo(file)) {
            file.writeText(text)
            tmp.delete()
        }
    }
}
