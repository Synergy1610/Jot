package app.jot.ui

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import app.jot.JotStore

/**
 * Conflict dialogs live above both screens so a sync started from the editor's
 * bottom bar still asks before replacing either copy.
 */
@Composable
fun SyncDialogs(store: JotStore) {
    if (store.downloadPrompt) {
        AlertDialog(
            onDismissRequest = { store.dismissDownload() },
            containerColor = JotColors.panel,
            title = { Text("Replace notes here?", style = JotTitle) },
            text = {
                Text(
                    "The cloud copy will replace everything on this phone. Notes written " +
                        "here that were not backed up will be lost. Back up first if you " +
                        "want to keep them.",
                    style = JotBody,
                )
            },
            confirmButton = {
                TextButton(onClick = { store.confirmDownload() }) {
                    Text("Get latest", style = JotLabel)
                }
            },
            dismissButton = {
                TextButton(onClick = { store.dismissDownload() }) {
                    Text("Cancel", style = JotLabel)
                }
            },
        )
    }

    if (store.backupPrompt) {
        AlertDialog(
            onDismissRequest = { store.dismissBackup() },
            containerColor = JotColors.panel,
            title = { Text("Replace cloud notes?", style = JotTitle) },
            text = {
                Text(
                    "The cloud copy has changed since this phone last synced. Backing up " +
                        "now replaces it with the notes on this phone. Get latest first if " +
                        "you want to keep the cloud copy.",
                    style = JotBody,
                )
            },
            confirmButton = {
                TextButton(onClick = { store.confirmBackup() }) {
                    Text("Back up now", style = JotLabel)
                }
            },
            dismissButton = {
                TextButton(onClick = { store.dismissBackup() }) {
                    Text("Cancel", style = JotLabel)
                }
            },
        )
    }
}
