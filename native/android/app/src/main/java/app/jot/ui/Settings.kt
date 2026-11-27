package app.jot.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jot.JotStore

@Composable
fun SettingsScreen(store: JotStore) {
    var email by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var confirmDownload by remember { mutableStateOf(false) }

    Column(
        Modifier
            .fillMaxSize()
            .background(JotColors.ink)
            .statusBarsPadding()
            .imePadding()
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(40.dp)
                .padding(start = 16.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                if (store.helpOpen) "Jot help" else "Jot settings",
                style = JotTitle,
                modifier = Modifier.weight(1f),
            )
            Box(
                Modifier
                    .size(width = 30.dp, height = 40.dp)
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                    ) {
                        if (store.helpOpen) store.helpOpen = false else store.settingsOpen = false
                    },
                contentAlignment = Alignment.Center,
            ) {
                Text("✕", style = JotTitle)
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(JotColors.dim))

        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (store.helpOpen) {
                HelpContent()
            } else {
                AccountSection(
                    store = store,
                    email = email,
                    password = password,
                    onEmail = { email = it },
                    onPassword = { password = it },
                    onClearPassword = { password = "" },
                    onDownload = { confirmDownload = true },
                )
                Spacer(Modifier.height(6.dp))
                Section("Clear pad")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    JotButton("Clear notes", enabled = store.ready && store.content.isNotEmpty()) {
                        store.clearPad()
                    }
                    JotButton("Restore cleared", enabled = store.ready && store.canUndo) {
                        store.undoClear()
                    }
                }
                Spacer(Modifier.height(6.dp))
                Section("About")
                Text("Jot ${store.versionName}", style = JotBody, color = JotColors.dim)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    JotButton("Help") { store.helpOpen = true }
                    JotButton("Back up file") { store.backUpFile() }
                }
                Text(store.status, style = JotBody, color = JotColors.dim)
            }
        }
    }

    if (confirmDownload) {
        AlertDialog(
            onDismissRequest = { confirmDownload = false },
            containerColor = JotColors.panel,
            title = { Text("Download cloud notes?", style = JotTitle) },
            text = {
                Text(
                    "This replaces the notes on this phone with the cloud copy. " +
                        "Use Back up now instead if this phone has newer text.",
                    style = JotBody,
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmDownload = false
                    store.downloadNow()
                }) { Text("Download", style = JotLabel) }
            },
            dismissButton = {
                TextButton(onClick = { confirmDownload = false }) {
                    Text("Cancel", style = JotLabel)
                }
            },
        )
    }
}

@Composable
private fun AccountSection(
    store: JotStore,
    email: String,
    password: String,
    onEmail: (String) -> Unit,
    onPassword: (String) -> Unit,
    onClearPassword: () -> Unit,
    onDownload: () -> Unit,
) {
    Section("Account")
    if (store.cloudSignedIn) {
        Text(
            "Signed in as ${store.cloudAccount ?: ""}. Notes are backed up when you leave Jot.",
            style = JotBody,
            color = JotColors.text,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            JotButton("Back up now", enabled = !store.cloudBusy) { store.syncNow() }
            JotButton("Download", enabled = !store.cloudBusy) { onDownload() }
        }
        JotButton("Sign out", enabled = !store.cloudBusy) { store.signOut() }
    } else {
        Text(
            "Signed out — notes stay on this phone until you sign in.",
            style = JotBody,
            color = JotColors.text,
        )
        JotField("Email", email, onEmail, KeyboardType.Email)
        JotField("Password", password, onPassword, KeyboardType.Password, secret = true)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            JotButton("Sign in", enabled = !store.cloudBusy && email.isNotBlank() && password.isNotEmpty()) {
                store.signIn(email, password)
                onClearPassword()
            }
            JotButton("Sign up", enabled = !store.cloudBusy && email.isNotBlank() && password.isNotEmpty()) {
                store.signUp(email, password)
                onClearPassword()
            }
        }
    }
    Text(
        store.cloudStatus,
        style = JotBody,
        color = if (store.cloudError || store.conflict) JotColors.error else JotColors.dim,
    )
}

@Composable
private fun HelpContent() {
    Section("Writing")
    Text("Type as you would in any notes app. Enter ends the line with today's date tag.", style = JotBody)
    Section("Saving")
    Text(
        "Notes save on this device as you type. Cloud backup runs when you leave Jot, " +
            "and whenever you tap Back up now.",
        style = JotBody,
    )
    Section("Conflicts")
    Text(
        "If this phone and another device both changed the notes, Jot reports it and " +
            "never overwrites either copy. Download deliberately replaces this phone's " +
            "notes with the cloud copy.",
        style = JotBody,
    )
    Section("Clearing")
    Text(
        "Clear notes empties the pad but keeps a copy. Restore cleared brings the last " +
            "cleared text back.",
        style = JotBody,
    )
    Section("Signing in")
    Text(
        "Use the same account as the desktop app. The anon key is built in; your " +
            "password is only sent to Supabase.",
        style = JotBody,
    )
}

@Composable
private fun Section(title: String) {
    Text(title, style = JotHeading)
}

@Composable
private fun JotField(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    keyboardType: KeyboardType,
    secret: Boolean = false,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, style = JotLabel)
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            textStyle = JotBody,
            cursorBrush = SolidColor(JotColors.accent),
            visualTransformation = if (secret) PasswordVisualTransformation() else VisualTransformation.None,
            keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
            modifier = Modifier
                .fillMaxWidth()
                .background(JotColors.field)
                .border(1.dp, JotColors.dim)
                .padding(horizontal = 8.dp, vertical = 7.dp),
        )
    }
}

@Composable
private fun JotButton(label: String, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier
            .background(JotColors.field)
            .border(1.dp, if (enabled) JotColors.dim else JotColors.field)
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                enabled = enabled,
                onClick = onClick,
            )
            .padding(horizontal = 12.dp, vertical = 7.dp),
    ) {
        Text(
            label,
            style = JotBody.copy(fontSize = 13.sp),
            color = if (enabled) JotColors.accent else JotColors.dim,
        )
    }
}
