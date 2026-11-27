package app.jot

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.util.Locale
import java.util.concurrent.Executors

/** Set by the activity so the background worker never opens a second writer. */
object JotVisibility {
    @Volatile var visible = false
}

class JotStore(context: Context) {
    private val files = File(context.filesDir, "jot").apply { mkdirs() }
    private val dbFile = File(files, "pad.db")
    private val backupFile = File(files, "pad.db.backup")
    private val core = JotCore(dbFile.path, File(files, "config").path)
    private val coreDispatcher = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "jot-core").apply { isDaemon = true }
    }.asCoroutineDispatcher()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    var content by mutableStateOf("")
        private set
    var ready by mutableStateOf(false)
        private set
    var saving by mutableStateOf(false)
        private set
    var savedTick by mutableStateOf(0)
        private set
    var localError by mutableStateOf(false)
        private set
    var status by mutableStateOf("Opening notes…")
        private set

    var cloudSignedIn by mutableStateOf(false)
        private set
    var cloudAccount by mutableStateOf<String?>(null)
        private set
    var cloudBusy by mutableStateOf(false)
        private set
    var cloudStatus by mutableStateOf("Cloud backup is off")
        private set
    var cloudError by mutableStateOf(false)
        private set
    var conflict by mutableStateOf(false)
        private set

    var canUndo by mutableStateOf(false)
        private set
    var settingsOpen by mutableStateOf(false)
    var helpOpen by mutableStateOf(false)

    var dateLabel by mutableStateOf(formatDate(LocalDate.now()))
        private set

    private var savedContent = ""
    private var saveJob: Job? = null
    private var pulledOnLaunch = false

    val versionName: String = BuildConfig.VERSION_NAME

    val cloudTooltip: String
        get() = if (cloudSignedIn) {
            "Cloud backup is on — syncs when you leave Jot"
        } else {
            "Cloud backup is off — sign in to enable"
        }

    val localTooltip: String
        get() = when {
            !ready -> "Local save unavailable"
            saving -> "Saving on this device…"
            localError -> "Local save failed"
            else -> "Saved on this device"
        }

    init {
        scope.launch {
            val result = withContext(coreDispatcher) {
                runCatching {
                    val text = core.open()
                    text to (core.lastCleared() != null)
                }
            }
            result.onSuccess { (text, hasCleared) ->
                content = text
                savedContent = text
                canUndo = hasCleared
                ready = true
                status = "Saved on this device"
                configureCloud()
            }.onFailure {
                localError = true
                status = it.message ?: "Could not open the notes"
            }
        }
    }

    fun refreshDate() {
        dateLabel = formatDate(LocalDate.now())
    }

    fun edit(text: String) {
        if (!ready || text == content) return
        content = text
        localError = false
        saving = true
        status = "Saving on this device…"
        saveJob?.cancel()
        saveJob = scope.launch {
            delay(400)
            persist()
        }
    }

    suspend fun flush() {
        saveJob?.cancel()
        persist()
    }

    private suspend fun persist() {
        if (!ready) return
        val expected = savedContent
        val text = content
        if (text == expected) {
            saving = false
            status = "Saved on this device"
            return
        }
        val result = withContext(coreDispatcher) { runCatching { core.save(expected, text) } }
        result.onSuccess {
            savedContent = text
            savedTick++
            if (content == text) {
                saving = false
                status = "Saved on this device"
            }
        }.onFailure {
            localError = true
            saving = false
            status = it.message ?: "Local save failed"
        }
    }

    // --- cloud ---------------------------------------------------------

    private suspend fun configureCloud() {
        val result = withContext(coreDispatcher) {
            runCatching {
                core.configure(BuildConfig.SUPABASE_URL, BuildConfig.SUPABASE_ANON_KEY)
                core.restore()
                Triple(core.signedIn(), core.account(), core.baseContent())
            }
        }
        result.onSuccess { (signedIn, account, _) ->
            cloudSignedIn = signedIn
            cloudAccount = account
            cloudStatus = if (signedIn) "Cloud backup is on" else "Cloud backup is off"
            if (signedIn && !pulledOnLaunch) {
                pulledOnLaunch = true
                launchPull()
            }
        }
    }

    /**
     * On launch the app only pulls, and only when this device has nothing
     * unsynced. That way opening Jot can show notes written on the desktop
     * without ever replacing text typed here.
     */
    private fun launchPull() {
        if (!cloudSignedIn) return
        scope.launch {
            val request = content
            val result = withContext(coreDispatcher) {
                runCatching {
                    val base = core.baseContent()
                    if (request.isNotEmpty() && base != request) return@runCatching null
                    core.sync(request)
                }
            }
            result.onSuccess { pad ->
                if (pad == null) {
                    cloudStatus = "Local notes waiting to back up"
                    return@onSuccess
                }
                if (content != request) {
                    cloudStatus = "Cloud notes arrived while you were typing"
                    return@onSuccess
                }
                if (pad.content != content) applyCloud(pad.content)
                cloudStatus = "Cloud notes are up to date"
            }.onFailure {
                cloudStatus = it.message ?: "Could not reach the cloud"
            }
        }
    }

    fun syncNow() {
        if (!ready || !cloudSignedIn || cloudBusy) return
        scope.launch { sync() }
    }

    private suspend fun sync() {
        flush()
        val request = content
        cloudBusy = true
        cloudError = false
        cloudStatus = "Backing up…"
        val result = withContext(coreDispatcher) { runCatching { core.sync(request) } }
        cloudBusy = false
        result.onSuccess { pad ->
            if (content != request) {
                cloudStatus = "Cloud updated while you were typing — back up again"
                return@onSuccess
            }
            if (pad.content != content) applyCloud(pad.content)
            cloudStatus = "Backed up to the cloud"
        }.onFailure {
            cloudError = true
            conflict = it.message?.contains("both changed") == true
            cloudStatus = it.message ?: "Could not back up"
        }
    }

    fun downloadNow() {
        if (!cloudSignedIn || cloudBusy) return
        scope.launch {
            flush()
            cloudBusy = true
            cloudError = false
            cloudStatus = "Downloading…"
            val result = withContext(coreDispatcher) { runCatching { core.download() } }
            cloudBusy = false
            result.onSuccess { pad ->
                if (pad == null) {
                    cloudStatus = "No cloud notes yet"
                    return@onSuccess
                }
                if (pad.content == content) {
                    cloudStatus = "Already up to date"
                    return@onSuccess
                }
                if (applyCloud(pad.content)) {
                    withContext(coreDispatcher) {
                        runCatching { core.setBaseline(pad.content, pad.content, pad.version) }
                    }
                    cloudStatus = "Downloaded from the cloud"
                }
            }.onFailure {
                cloudError = true
                cloudStatus = it.message ?: "Could not download"
            }
        }
    }

    private suspend fun applyCloud(text: String): Boolean {
        val expected = savedContent
        val result = withContext(coreDispatcher) { runCatching { core.save(expected, text) } }
        return result.onSuccess {
            savedContent = text
            content = text
            saveJob?.cancel()
            saving = false
            localError = false
            status = "Saved on this device"
            savedTick++
        }.onFailure {
            localError = true
            status = it.message ?: "Could not apply cloud notes"
        }.isSuccess
    }

    fun signIn(email: String, password: String) {
        if (cloudBusy) return
        if (BuildConfig.SUPABASE_URL.isBlank() || BuildConfig.SUPABASE_ANON_KEY.isBlank()) {
            cloudError = true
            cloudStatus = "This build has no Supabase configuration"
            return
        }
        scope.launch {
            cloudBusy = true
            cloudError = false
            cloudStatus = "Signing in…"
            val result = withContext(coreDispatcher) {
                runCatching {
                    core.signIn(email.trim(), password)
                    Triple(core.signedIn(), core.account(), core.baseContent())
                }
            }
            cloudBusy = false
            result.onSuccess { (signedIn, account, _) ->
                cloudSignedIn = signedIn
                cloudAccount = account
                cloudStatus = if (signedIn) "Signed in" else "Sign-in failed"
                if (signedIn && !pulledOnLaunch) {
                    pulledOnLaunch = true
                    launchPull()
                }
            }.onFailure {
                cloudError = true
                cloudStatus = it.message ?: "Could not sign in"
            }
        }
    }

    fun signUp(email: String, password: String) {
        if (cloudBusy) return
        if (BuildConfig.SUPABASE_URL.isBlank() || BuildConfig.SUPABASE_ANON_KEY.isBlank()) {
            cloudError = true
            cloudStatus = "This build has no Supabase configuration"
            return
        }
        scope.launch {
            cloudBusy = true
            cloudError = false
            cloudStatus = "Creating account…"
            val result = withContext(coreDispatcher) {
                runCatching {
                    val needsConfirmation = core.signUp(email.trim(), password)
                    Triple(needsConfirmation, core.signedIn(), core.account())
                }
            }
            cloudBusy = false
            result.onSuccess { (needsConfirmation, signedIn, account) ->
                cloudSignedIn = signedIn
                cloudAccount = account
                cloudStatus = when {
                    needsConfirmation -> "Check your email to confirm, then sign in"
                    signedIn -> "Signed in"
                    else -> "Account created"
                }
            }.onFailure {
                cloudError = true
                cloudStatus = it.message ?: "Could not create the account"
            }
        }
    }

    fun signOut() {
        if (cloudBusy) return
        scope.launch {
            cloudBusy = true
            val result = withContext(coreDispatcher) { runCatching { core.signOut() } }
            cloudBusy = false
            result.onSuccess {
                cloudSignedIn = false
                cloudAccount = null
                conflict = false
                cloudError = false
                cloudStatus = "Signed out"
                pulledOnLaunch = false
            }.onFailure {
                cloudError = true
                cloudStatus = it.message ?: "Could not sign out"
            }
        }
    }

    // --- pad maintenance ------------------------------------------------

    fun clearPad() {
        if (!ready) return
        scope.launch {
            flush()
            val expected = savedContent
            val result = withContext(coreDispatcher) { runCatching { core.clear(expected, content) } }
            result.onSuccess {
                content = ""
                savedContent = ""
                saving = false
                canUndo = true
                localError = false
                status = "Cleared"
                savedTick++
            }.onFailure {
                localError = true
                status = it.message ?: "Could not clear the notes"
            }
        }
    }

    fun undoClear() {
        if (!ready) return
        scope.launch {
            val result = withContext(coreDispatcher) {
                runCatching {
                    val backup = core.lastCleared() ?: throw JotException("No cleared notes to restore")
                    core.undoClear(backup)
                    core.load()
                }
            }
            result.onSuccess { text ->
                content = text
                savedContent = text
                canUndo = false
                localError = false
                status = "Restored"
                savedTick++
            }.onFailure {
                localError = true
                status = it.message ?: "Could not restore the notes"
            }
        }
    }

    fun backUpFile() {
        if (!ready) return
        scope.launch {
            val result = withContext(coreDispatcher) { runCatching { core.backup(backupFile.path) } }
            result.onSuccess { status = "Backup written" }
                .onFailure { localError = true; status = it.message ?: "Could not back up" }
        }
    }

    companion object {
        private val headerDate = DateTimeFormatter.ofPattern("EEE dd MMM yyyy", Locale.US)

        fun formatDate(date: LocalDate): String = headerDate.format(date)
    }
}
