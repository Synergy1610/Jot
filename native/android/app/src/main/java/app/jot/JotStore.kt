package app.jot

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.time.LocalDate
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.Locale

/** One store per app process. */
object JotStores {
    @Volatile private var shared: JotStore? = null

    @Synchronized
    fun get(context: Context): JotStore =
        shared ?: JotStore(context.applicationContext).also { shared = it }
}

class JotStore(context: Context) {
    private val appContext = context.applicationContext
    private val dir = File(appContext.filesDir, "jot").apply { mkdirs() }
    private val configDir = File(dir, "config").apply { mkdirs() }
    private val local = LocalPad(dir)
    private val cloud = CloudClient(configDir)
    private val io = Dispatchers.IO
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    var content by mutableStateOf("")
        private set
    var ready by mutableStateOf(false)
        private set
    var saving by mutableStateOf(false)
        private set
    var savedTick by mutableStateOf(0)
        private set
    var localError by mutableStateOf<String?>(null)
        private set
    var status by mutableStateOf("Opening notes…")
        private set

    var cloudSignedIn by mutableStateOf(false)
        private set
    var cloudAccount by mutableStateOf<String?>(null)
        private set
    var cloudBusy by mutableStateOf(false)
        private set
    var cloudStatus by mutableStateOf("Cloud backup is off — sign in to enable")
        private set
    var cloudError by mutableStateOf(false)
        private set
    var pending by mutableStateOf(false)
        private set

    var downloadPrompt by mutableStateOf(false)
        private set
    var backupPrompt by mutableStateOf(false)
        private set

    var canUndo by mutableStateOf(false)
        private set
    var settingsOpen by mutableStateOf(false)
    var helpOpen by mutableStateOf(false)

    var dateLabel by mutableStateOf(formatDate(LocalDate.now()))
        private set

    private var savedContent = ""
    private var dirty = false
    private var saveJob: Job? = null
    private var stagedPad: CloudPad? = null
    private var stagedVersion = 0L

    val versionName: String = BuildConfig.VERSION_NAME
    val localErrorFlag: Boolean get() = localError != null

    val cloudTooltip: String
        get() = if (cloudSignedIn) cloudStatus else "Cloud backup is off — sign in to enable"

    val localTooltip: String
        get() = when {
            !ready -> "Local save unavailable"
            saving -> "Saving on this device…"
            localError != null -> localError ?: "Local save failed"
            else -> "Saved on this device"
        }

    init {
        scope.launch {
            val result = withContext(io) {
                runCatching {
                    val loaded = local.load()
                    Pair(loaded.content, loaded.lastCleared != null)
                }
            }
            result.onSuccess { (text, hasCleared) ->
                content = text
                savedContent = text
                dirty = false
                canUndo = hasCleared
                ready = true
                saving = false
                localError = null
                status = "Saved on this device"
                configureCloud()
            }.onFailure { e ->
                localError = e.message ?: "Could not open the notes"
                status = localError ?: "Could not open the notes"
            }
        }
    }

    fun refreshDate() {
        dateLabel = formatDate(LocalDate.now())
    }

    // --- local save ------------------------------------------------------

    fun edit(text: String) {
        if (!ready || text == content) return
        content = text
        dirty = content != savedContent
        if (dirty) {
            saving = true
            localError = null
            status = "Saving on this device…"
        }
        if (cloudSignedIn) refreshPendingState()
        saveJob?.cancel()
        saveJob = scope.launch {
            delay(350)
            persistCurrent()
        }
    }

    /** Flush pending edits to disk. Returns true if local state is safe to use. */
    suspend fun flush(): Boolean {
        saveJob?.cancel()
        return persistCurrent()
    }

    /** Fire-and-forget flush for lifecycle callbacks. */
    fun flushNow() {
        if (!ready) return
        saveJob?.cancel()
        scope.launch { persistCurrent() }
    }

    private suspend fun persistCurrent(): Boolean {
        if (!ready) return false
        while (true) {
            val text = content
            if (!dirty && text == savedContent) {
                if (saving) saving = false
                if (localError == null) status = "Saved on this device"
                return localError == null
            }
            dirty = text != savedContent
            if (!dirty) {
                if (saving) saving = false
                if (localError == null) status = "Saved on this device"
                return localError == null
            }
            saving = true
            val result = withContext(io) { runCatching { local.save(text) } }
            val failure = result.exceptionOrNull()
            if (failure != null) {
                localError = failure.message ?: "Local save failed"
                saving = false
                status = localError ?: "Local save failed"
                return false
            }
            savedContent = text
            savedTick++
            if (content == text) {
                dirty = false
                saving = false
                localError = null
                status = "Saved on this device"
                if (cloudSignedIn) refreshPendingState()
                return true
            }
            // User kept typing while we were writing; loop and save the latest.
            dirty = true
        }
    }

    private fun refreshPendingState() {
        val base = cloud.loadBase()?.content
        pending = SyncText.isPending(content, base)
        if (!cloudBusy && !cloudError) {
            cloudStatus = if (pending) "Not backed up — tap Back up now" else "Cloud backup is on — nothing to do"
        }
    }

    private suspend fun replaceLocal(text: String): Boolean {
        val result = withContext(io) { runCatching { local.save(text) } }
        return result.fold(
            onSuccess = {
                saveJob?.cancel()
                savedContent = text
                content = text
                dirty = false
                saving = false
                localError = null
                status = "Saved on this device"
                savedTick++
                true
            },
            onFailure = { e ->
                localError = e.message ?: "Could not save notes on this phone"
                saving = false
                status = localError ?: "Could not save notes on this phone"
                false
            },
        )
    }

    // --- cloud -----------------------------------------------------------

    private suspend fun configureCloud() {
        val result = withContext(io) {
            runCatching {
                cloud.configure(BuildConfig.SUPABASE_URL, BuildConfig.SUPABASE_ANON_KEY)
                val restored = cloud.restore()
                Triple(restored, cloud.isSignedIn(), cloud.account())
            }
        }
        result.onSuccess { (_, signedIn, account) ->
            cloudSignedIn = signedIn
            cloudAccount = account
            cloudError = false
            if (signedIn) {
                refreshPendingState()
            } else {
                pending = false
                cloudStatus = "Cloud backup is off — sign in to enable"
            }
        }.onFailure { e ->
            cloudError = true
            cloudStatus = friendlyMessage(e)
        }
    }

    fun getLatest() {
        if (!ready || !cloudSignedIn || cloudBusy) return
        scope.launch {
            cloudBusy = true
            cloudError = false
            cloudStatus = "Getting latest…"
            val saved = flush()
            if (!saved) {
                finishError(localError ?: "Local save failed — try again")
                return@launch
            }
            val localText = content
            val result = withContext(io) { runCatching { cloud.download() } }
            if (content != localText) {
                finishError("Notes changed while downloading — try again")
                return@launch
            }
            val pad = result.getOrElse { e ->
                finishError(friendlyMessage(e))
                return@launch
            }
            if (pad == null) {
                cloudBusy = false
                cloudStatus = "No cloud notes yet — back up this phone first"
                return@launch
            }
            val base = withContext(io) { cloud.loadBase()?.content }
            if (SyncText.isPending(localText, base)) {
                stagedPad = pad
                downloadPrompt = true
                cloudBusy = false
                cloudStatus = "Cloud copy ready — confirm to replace notes here"
            } else {
                applyDownload(pad)
            }
        }
    }

    private suspend fun applyDownload(pad: CloudPad) {
        cloudBusy = true
        cloudStatus = "Getting latest…"
        if (!replaceLocal(pad.content)) {
            finishError(localError ?: "Could not save cloud notes on this phone")
            return
        }
        withContext(io) { runCatching { cloud.saveBase(pad.content, pad.version) } }
        cloudBusy = false
        cloudError = false
        pending = false
        markSynced("Got latest")
    }

    fun confirmDownload() {
        val pad = stagedPad ?: return
        stagedPad = null
        downloadPrompt = false
        scope.launch { applyDownload(pad) }
    }

    fun dismissDownload() {
        stagedPad = null
        downloadPrompt = false
        cloudBusy = false
        refreshPendingState()
        if (!pending) cloudStatus = "Cloud backup is on — nothing to do"
    }

    fun backUpNow() {
        if (!ready || !cloudSignedIn || cloudBusy) return
        scope.launch {
            cloudBusy = true
            cloudError = false
            cloudStatus = "Backing up…"
            val saved = flush()
            if (!saved) {
                finishError(localError ?: "Local save failed — try again")
                return@launch
            }
            val localText = content
            val result = withContext(io) {
                runCatching {
                    val remote = cloud.download()
                    val base = cloud.loadBase()
                    Pair(remote, base)
                }
            }
            if (content != localText) {
                finishError("Notes changed while backing up — try again")
                return@launch
            }
            val (remote, base) = result.getOrElse { e ->
                finishError(friendlyMessage(e))
                return@launch
            }
            when {
                remote?.content == localText -> {
                    withContext(io) {
                        runCatching { cloud.saveBase(localText, remote.version) }
                    }
                    cloudBusy = false
                    cloudError = false
                    pending = false
                    markSynced("Already backed up")
                }
                SyncText.cloudChanged(remote?.content.orEmpty(), base?.content, localText) -> {
                    stagedVersion = remote?.version ?: 0L
                    backupPrompt = true
                    cloudBusy = false
                    cloudStatus = "Cloud has newer notes — confirm to replace them"
                }
                else -> uploadAndBaseline(localText, remote?.version ?: 0L)
            }
        }
    }

    private suspend fun uploadAndBaseline(localText: String, expectedVersion: Long) {
        cloudBusy = true
        cloudStatus = "Backing up…"
        val result = withContext(io) {
            runCatching {
                val pad = cloud.upload(localText, expectedVersion)
                cloud.saveBase(localText, pad.version)
                pad
            }
        }
        cloudBusy = false
        result.onSuccess {
            cloudError = false
            pending = false
            markSynced("Backed up")
        }.onFailure { e ->
            finishError(friendlyMessage(e))
        }
    }

    fun confirmBackup() {
        backupPrompt = false
        val expected = stagedVersion
        scope.launch { uploadAndBaseline(content, expected) }
    }

    fun dismissBackup() {
        backupPrompt = false
        cloudBusy = false
        refreshPendingState()
        if (!pending) cloudStatus = "Cloud backup is on — nothing to do"
    }

    private fun finishError(message: String) {
        cloudBusy = false
        cloudError = true
        cloudStatus = message
    }

    private fun markSynced(label: String) {
        val time = DateTimeFormatter.ofPattern("HH:mm").format(LocalTime.now())
        cloudError = false
        cloudStatus = "$label · $time"
    }

    private fun friendlyMessage(e: Throwable): String = friendlyMessage(e.message ?: "Something went wrong")

    private fun friendlyMessage(message: String): String {
        val lower = message.lowercase(Locale.ROOT)
        return when {
            message.contains("Cloud sign-in expired") ||
                lower.contains("401") || lower.contains("jwt") || lower.contains("unauthorized") ->
                "Cloud sign-in expired — sign in again"
            lower.contains("could not reach the cloud") ||
                lower.contains("could not reach supabase") ||
                lower.contains("connection abort") ||
                lower.contains("network error") ||
                lower.contains("timed out") ||
                lower.contains("failed to connect") ||
                lower.contains("unreachable") ||
                lower.contains("unable to resolve host") ->
                "Couldn't reach the cloud"
            lower.contains("supabase returned") && lower.contains("duplicate") ->
                "Cloud update conflict — try again"
            lower.contains("http://") || lower.contains("https://") || lower.contains("supabase.co") ->
                "Couldn't reach the cloud"
            message.length > 120 -> message.take(117) + "…"
            else -> message
        }
    }

    // --- account ---------------------------------------------------------

    fun signIn(email: String, password: String) {
        if (cloudBusy) return
        scope.launch {
            cloudBusy = true
            cloudError = false
            cloudStatus = "Signing in…"
            val result = withContext(io) {
                runCatching {
                    cloud.configure(BuildConfig.SUPABASE_URL, BuildConfig.SUPABASE_ANON_KEY)
                    cloud.signIn(email.trim(), password)
                    cloud.isSignedIn() to cloud.account()
                }
            }
            cloudBusy = false
            result.onSuccess { (signedIn, account) ->
                cloudSignedIn = signedIn
                cloudAccount = account
                if (signedIn) {
                    cloudError = false
                    refreshPendingState()
                    cloudStatus = "Signed in — tap Get latest or Back up now"
                } else {
                    cloudError = true
                    cloudStatus = "Sign-in failed — check your email and password"
                }
            }.onFailure { e ->
                cloudError = true
                cloudStatus = friendlyMessage(e)
            }
        }
    }

    fun signUp(email: String, password: String) {
        if (cloudBusy) return
        scope.launch {
            cloudBusy = true
            cloudError = false
            cloudStatus = "Creating account…"
            val result = withContext(io) {
                runCatching {
                    cloud.configure(BuildConfig.SUPABASE_URL, BuildConfig.SUPABASE_ANON_KEY)
                    val needsConfirmation = cloud.signUp(email.trim(), password)
                    Triple(needsConfirmation, cloud.isSignedIn(), cloud.account())
                }
            }
            cloudBusy = false
            result.onSuccess { (needsConfirmation, signedIn, account) ->
                cloudSignedIn = signedIn
                cloudAccount = account
                cloudStatus = when {
                    needsConfirmation -> "Check your email to confirm, then sign in"
                    signedIn -> {
                        cloudError = false
                        refreshPendingState()
                        "Signed in — tap Get latest or Back up now"
                    }
                    else -> "Account created"
                }
            }.onFailure { e ->
                cloudError = true
                cloudStatus = friendlyMessage(e)
            }
        }
    }

    fun signOut() {
        if (cloudBusy) return
        scope.launch {
            cloudBusy = true
            val result = withContext(io) { runCatching { cloud.signOut() } }
            cloudBusy = false
            result.onSuccess {
                cloudSignedIn = false
                cloudAccount = null
                cloudError = false
                pending = false
                cloudStatus = "Cloud backup is off — sign in to enable"
            }.onFailure { e ->
                cloudError = true
                cloudStatus = friendlyMessage(e)
            }
        }
    }

    // --- pad maintenance -------------------------------------------------

    fun clearPad() {
        if (!ready) return
        scope.launch {
            saveJob?.cancel()
            val text = content
            val result = withContext(io) { runCatching { local.clear(text) } }
            result.onSuccess {
                content = ""
                savedContent = ""
                dirty = false
                saving = false
                canUndo = true
                localError = null
                status = "Cleared"
                savedTick++
                if (cloudSignedIn) refreshPendingState()
            }.onFailure { e ->
                localError = e.message ?: "Could not clear the notes"
                status = localError ?: "Could not clear the notes"
            }
        }
    }

    fun undoClear() {
        if (!ready) return
        scope.launch {
            saveJob?.cancel()
            val result = withContext(io) { runCatching { local.restoreCleared() } }
            result.onSuccess { text ->
                if (text == null) {
                    localError = "No cleared notes to restore"
                    status = localError ?: "No cleared notes to restore"
                    return@onSuccess
                }
                content = text
                savedContent = text
                dirty = false
                saving = false
                canUndo = false
                localError = null
                status = "Restored"
                savedTick++
                if (cloudSignedIn) refreshPendingState()
            }.onFailure { e ->
                localError = e.message ?: "Could not restore the notes"
                status = localError ?: "Could not restore the notes"
            }
        }
    }

    fun backUpFile() {
        if (!ready) return
        scope.launch {
            val backup = File(dir, "pad.db.backup")
            val result = withContext(io) { runCatching { local.backupTo(backup) } }
            result.onSuccess { status = "Backup written" }
                .onFailure { e ->
                    localError = e.message ?: "Could not back up"
                    status = localError ?: "Could not back up"
                }
        }
    }

    companion object {
        private val headerDate = DateTimeFormatter.ofPattern("EEE dd MMM yyyy", Locale.US)
        fun formatDate(date: LocalDate): String = headerDate.format(date)
    }
}
