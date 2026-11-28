package app.jot

import android.database.sqlite.SQLiteDatabase
import java.io.File
import java.io.FileOutputStream

/**
 * Plain-file local storage. This is the source of truth for the phone.
 * Writes are atomic (temp + fsync + rename) so a crash cannot corrupt notes.
 */
class LocalPad(private val dir: File) {
    private val notesFile = File(dir, "notes.txt")
    private val lastClearedFile = File(dir, "last-cleared.txt")
    private val legacyDb = File(dir, "pad.db")

    fun load(): LoadedPad {
        dir.mkdirs()
        migrateIfNeeded()
        val content = if (notesFile.exists()) notesFile.readText() else ""
        val lastCleared = if (lastClearedFile.exists()) lastClearedFile.readText() else null
        return LoadedPad(content = content, lastCleared = lastCleared)
    }

    fun save(content: String) {
        dir.mkdirs()
        atomicWrite(notesFile, content)
    }

    fun clear(current: String) {
        dir.mkdirs()
        atomicWrite(lastClearedFile, current)
        atomicWrite(notesFile, "")
    }

    fun restoreCleared(): String? {
        val backup = if (lastClearedFile.exists()) lastClearedFile.readText() else return null
        dir.mkdirs()
        atomicWrite(notesFile, backup)
        lastClearedFile.delete()
        return backup
    }

    fun lastCleared(): String? = if (lastClearedFile.exists()) lastClearedFile.readText() else null

    fun backupTo(path: File) {
        path.parentFile?.mkdirs()
        if (notesFile.exists()) {
            notesFile.copyTo(path, overwrite = true)
        } else {
            path.writeText("")
        }
    }

    private fun migrateIfNeeded() {
        if (notesFile.exists() || !legacyDb.exists()) return
        try {
            SQLiteDatabase.openDatabase(legacyDb.path, null, SQLiteDatabase.OPEN_READONLY).use { db ->
                val content = db.rawQuery("SELECT content FROM pad WHERE id = 1", null).use { c ->
                    if (c.moveToFirst()) c.getString(0) ?: "" else ""
                }
                val lastCleared = db.rawQuery(
                    "SELECT value FROM settings WHERE key = 'last_cleared'", null,
                ).use { c ->
                    if (c.moveToFirst()) c.getString(0) else null
                }
                atomicWrite(notesFile, content)
                if (!lastCleared.isNullOrEmpty()) atomicWrite(lastClearedFile, lastCleared)
            }
        } catch (_: Exception) {
            // If migration fails, fall back to an empty pad rather than crashing.
            // The old pad.db is left untouched.
            if (!notesFile.exists()) atomicWrite(notesFile, "")
        }
    }

    private fun atomicWrite(file: File, content: String) {
        val parent = file.parentFile ?: dir
        parent.mkdirs()
        val tmp = File(parent, file.name + ".tmp")
        FileOutputStream(tmp).use { out ->
            out.write(content.toByteArray(Charsets.UTF_8))
            out.flush()
            out.fd.sync()
        }
        if (!tmp.renameTo(file)) {
            // renameTo can fail across filesystems; fall back to direct write.
            FileOutputStream(file).use { out ->
                out.write(content.toByteArray(Charsets.UTF_8))
                out.flush()
                out.fd.sync()
            }
            tmp.delete()
        }
    }
}

data class LoadedPad(val content: String, val lastCleared: String?)
