package app.jot

import android.content.Context
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * Backstop for the sync that runs when the app goes to the background. It is
 * cancelled as soon as Jot is visible again, and it refuses to run while the
 * UI owns the database connection.
 */
class SyncWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        if (JotVisibility.visible) return Result.success()

        val files = File(applicationContext.filesDir, "jot")
        val core = JotCore(File(files, "pad.db").path, File(files, "config").path)
        return try {
            val local = core.open()
            core.configure(BuildConfig.SUPABASE_URL, BuildConfig.SUPABASE_ANON_KEY)
            if (!core.restore() || !core.signedIn()) {
                Result.success()
            } else {
                val base = core.baseContent()
                if (local.isNotEmpty() && base != local) {
                    // Unsynced local edits: leave them for the next deliberate sync.
                    Result.success()
                } else {
                    val pad = core.sync(local)
                    if (pad.content != local) core.save(local, pad.content)
                    Result.success()
                }
            }
        } catch (_: Throwable) {
            Result.retry()
        } finally {
            core.close()
        }
    }
}

object SyncScheduler {
    private const val WORK_NAME = "jot-background-sync"

    fun schedule(context: Context) {
        val request = OneTimeWorkRequestBuilder<SyncWorker>()
            .setInitialDelay(10, TimeUnit.MINUTES)
            .setConstraints(
                Constraints.Builder()
                    .setRequiredNetworkType(NetworkType.CONNECTED)
                    .build()
            )
            .build()
        WorkManager.getInstance(context)
            .enqueueUniqueWork(WORK_NAME, ExistingWorkPolicy.REPLACE, request)
    }

    fun cancel(context: Context) {
        WorkManager.getInstance(context).cancelUniqueWork(WORK_NAME)
    }
}
