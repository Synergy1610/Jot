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
import androidx.compose.foundation.layout.navigationBarsPadding
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
import app.jot.JotStore

@Composable
fun SettingsScreen(store: JotStore) {
    var email by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var confirmClear by remember { mutableStateOf(false) }

    Column(
        Modifier
            .fillMaxSize()
            .background(JotColors.ink)
            .statusBarsPadding()
            .navigationBarsPadding()
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
                )
                Spacer(Modifier.height(6.dp))
                Section("Clear pad")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    JotButton("Clear notes", enabled = store.ready && store.content.isNotEmpty()) {
                        confirmClear = true
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

    if (confirmClear) {
        AlertDialog(
            onDismissRequest = { confirmClear = false },
            containerColor = JotColors.panel,
            title = { Text("Clear all notes?", style = JotTitle) },
            text = {
                val cloud = if (store.cloudSignedIn) {
                    " and, if you tap Back up now, the cloud copy too."
                } else {
                    "."
                }
                Text(
                    "This empties the pad on this phone$cloud " +
                        "One restore copy is kept here, so you can undo this once.",
                    style = JotBody,
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmClear = false
                    store.clearPad()
                }) { Text("Clear notes", style = JotLabel) }
            },
            dismissButton = {
                TextButton(onClick = { confirmClear = false }) {
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
) {
    Section("Account")
    if (store.cloudSignedIn) {
        Text(
            "Signed in as ${store.cloudAccount ?: ""}. Nothing syncs on its own. " +
                "Get latest replaces the notes here; Back up now replaces the cloud copy.",
            style = JotBody,
            color = JotColors.text,
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            JotButton("Get latest", enabled = !store.cloudBusy) { store.getLatest() }
            JotButton("Back up now", enabled = !store.cloudBusy) { store.backUpNow() }
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
        JotButton("Sign-in help") { store.helpOpen = true }
    }
    Text(
        store.cloudStatus,
        style = JotBody,
        color = if (store.cloudError) JotColors.error else JotColors.dim,
    )
}

@Composable
private fun HelpContent() {
    Section("Writing")
    Text("Type as you would in any notes app. Enter ends the line with today's date tag.", style = JotBody)
    Section("Saving")
    Text(
        "Notes save on this device as you type, whether or not you are signed in. " +
            "Cloud backup never happens by itself on the phone — you decide when.",
        style = JotBody,
    )
    Section("Cloud")
    Text(
        "Get latest downloads the cloud copy and replaces the notes on this phone. " +
            "Back up now uploads the notes on this phone and replaces the cloud copy. " +
            "If the other copy has changes you have not saved elsewhere, Jot asks " +
            "before replacing it. Your laptop keeps working the way it always has.",
        style = JotBody,
    )
    Section("Clearing")
    Text(
        "Clear notes empties the pad but keeps one restore copy. Restore cleared brings " +
            "that text back. If you are signed in, tap Back up now afterwards to empty " +
            "the cloud copy too.",
        style = JotBody,
    )
    Section("Signing in")
    Text(
        "Use the same email and password as your Jot account. If you just created " +
            "an account, check your email for a confirmation link before signing in. " +
            "Your password is sent securely to Supabase and is not stored by Jot.",
        style = JotBody,
    )
    Text(
        "If sign-in fails, check the email address and password, confirm the account " +
            "if prompted, and try again with an internet connection. Your notes remain " +
            "saved on this phone if cloud sign-in or backup is unavailable.",
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

