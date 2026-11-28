package app.jot.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalMinimumInteractiveComponentEnforcement
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jot.DateTag
import app.jot.JotStore
import app.jot.R
import kotlinx.coroutines.delay

@Composable
fun EditorScreen(store: JotStore) {
    var tip by remember { mutableStateOf<String?>(null) }
    var tipToken by remember { mutableStateOf(0) }

    LaunchedEffect(tipToken) {
        if (tip != null) {
            delay(4000)
            tip = null
        }
    }

    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    Box(Modifier.fillMaxSize().background(JotColors.ink)) {
        Column(
            Modifier
                .fillMaxSize()
                .navigationBarsPadding()
                .imePadding()
        ) {
            JotTopBar(store) { text ->
                tip = text
                tipToken++
            }
            HorizontalDivider(thickness = 1.dp, color = JotColors.dim)
            EditorField(store, Modifier.weight(1f))
            AnimatedVisibility(
                visible = !imeVisible,
                enter = fadeIn() + expandVertically(),
                exit = fadeOut() + shrinkVertically(),
            ) {
                SyncBar(store)
            }
        }

        tip?.let { text ->
            Box(
                Modifier
                    .align(Alignment.TopEnd)
                    .statusBarsPadding()
                    .padding(top = 66.dp, end = 8.dp)
                    .background(JotColors.panel, RoundedCornerShape(6.dp))
                    .border(1.dp, JotColors.dim, RoundedCornerShape(6.dp))
                    .padding(horizontal = 9.dp, vertical = 5.dp)
            ) {
                Text(
                    text,
                    style = JotBody.copy(fontSize = 11.sp, lineHeight = 15.sp),
                    color = JotColors.text,
                    maxLines = 1,
                    softWrap = false,
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun JotTopBar(store: JotStore, onTip: (String) -> Unit) {
    val compact = LocalConfiguration.current.screenWidthDp < 340
    TopAppBar(
        title = {
            Text(
                store.dateLabel,
                style = if (compact) {
                    JotHeader.copy(fontSize = 12.sp, lineHeight = 16.sp)
                } else {
                    JotHeader
                },
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
        actions = {
            // Tighter action row: 40dp targets instead of Material's forced 48dp,
            // so the three icons sit closer together.
            CompositionLocalProvider(LocalMinimumInteractiveComponentEnforcement provides false) {
                IconButton(onClick = { onTip(store.cloudTooltip) }, modifier = Modifier.size(40.dp)) {
                    Icon(
                        painter = painterResource(
                            if (cloudSynced(store)) R.drawable.ic_cloud_check_24 else R.drawable.ic_cloud_24
                        ),
                        contentDescription = "Cloud sync",
                        modifier = Modifier.size(28.dp),
                        tint = cloudTint(store),
                    )
                }
                IconButton(onClick = { onTip(store.localTooltip) }, modifier = Modifier.size(40.dp)) {
                    Icon(
                        painter = painterResource(R.drawable.ic_device_24),
                        contentDescription = "Local notes",
                        modifier = Modifier.size(28.dp),
                        tint = deviceTint(store),
                    )
                }
                IconButton(onClick = { store.settingsOpen = true }, modifier = Modifier.size(40.dp)) {
                    Icon(
                        painter = painterResource(R.drawable.ic_gear_24),
                        contentDescription = "Settings",
                        modifier = Modifier.size(28.dp),
                        tint = JotColors.accent,
                    )
                }
            }
        },
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = JotColors.ink,
            titleContentColor = JotColors.accent,
            actionIconContentColor = JotColors.accent,
        ),
    )
}

private fun cloudSynced(store: JotStore): Boolean =
    store.cloudSignedIn && !store.pending && !store.cloudError && !store.cloudBusy

private fun cloudTint(store: JotStore) = when {
    !store.cloudSignedIn -> JotColors.dim
    store.cloudError -> JotColors.error
    store.pending || store.cloudBusy -> JotColors.tag
    else -> JotColors.accent
}

private fun deviceTint(store: JotStore) = when {
    store.localError != null -> JotColors.error
    store.saving -> JotColors.tag
    store.ready -> JotColors.accent
    else -> JotColors.dim
}

@Composable
private fun SyncBar(store: JotStore) {
    Column(Modifier.fillMaxWidth()) {
        HorizontalDivider(thickness = 1.dp, color = JotColors.dim)
        Row(
            Modifier
                .fillMaxWidth()
                .height(44.dp)
                .background(JotColors.ink)
                .padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                syncStatus(store),
                style = JotBody.copy(fontSize = 11.sp, lineHeight = 14.sp),
                color = if (store.localError != null || store.cloudError) {
                    JotColors.error
                } else {
                    JotColors.dim
                },
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            if (store.cloudSignedIn) {
                val getting = store.cloudBusy && store.cloudStatus.startsWith("Getting")
                JotButton(
                    label = if (getting) "Getting…" else "Get latest",
                    enabled = !store.cloudBusy,
                    minHeight = 32.dp,
                    horizontalPadding = 10.dp,
                    verticalPadding = 3.dp,
                    fontSize = 12.sp,
                    lineHeight = 16.sp,
                ) { store.getLatest() }
                Spacer(Modifier.width(8.dp))
                JotButton(
                    label = if (store.cloudBusy && !getting) "Backing up…" else "Back up now",
                    enabled = !store.cloudBusy,
                    highlight = store.pending,
                    minHeight = 32.dp,
                    horizontalPadding = 10.dp,
                    verticalPadding = 3.dp,
                    fontSize = 12.sp,
                    lineHeight = 16.sp,
                ) { store.backUpNow() }
            } else {
                JotButton(
                    label = "Sign in to sync",
                    minHeight = 32.dp,
                    horizontalPadding = 10.dp,
                    verticalPadding = 3.dp,
                    fontSize = 12.sp,
                    lineHeight = 16.sp,
                ) { store.settingsOpen = true }
            }
        }
    }
}

private fun syncStatus(store: JotStore): String {
    store.localError?.let { return it }
    if (!store.cloudSignedIn) return "Notes stay on this phone"
    // The bar is one line next to two buttons; the full wording stays in Settings
    // and in the cloud badge tooltip.
    val status = store.cloudStatus
    return when {
        status.startsWith("Already backed up") -> "Up to date"
        else -> when (status) {
            "Cloud backup is on — nothing to do" -> "Up to date"
            "Not backed up — tap Back up now" -> "Not backed up"
            "Cloud backup is off — sign in to enable" -> "Notes stay on this phone"
            "No cloud notes yet — back up this phone first" -> "No cloud copy yet"
            "Cloud copy ready — confirm to replace notes here" -> "Confirm replace"
            "Cloud has newer notes — confirm to replace them" -> "Cloud has newer notes"
            "Signed in — tap Get latest or Back up now" -> "Signed in"
            "Sign-in failed — check your email and password" -> "Sign-in failed"
            "Cloud sign-in expired — sign in again" -> "Sign in again"
            else -> status
        }
    }
}

@Composable
private fun EditorField(store: JotStore, modifier: Modifier = Modifier) {
    val focus = remember { androidx.compose.ui.focus.FocusRequester() }
    var field by remember {
        mutableStateOf(TextFieldValue(store.content, TextRange(store.content.length)))
    }
    LaunchedEffect(store.content) {
        if (store.content != field.text) {
            val caret = minOf(store.content.length, field.selection.end)
            field = TextFieldValue(store.content, TextRange(caret))
        }
    }

    Box(modifier.fillMaxSize().padding(horizontal = 16.dp, vertical = 14.dp)) {
        BasicTextField(
            value = field,
            onValueChange = { incoming ->
                if (!store.ready) return@BasicTextField
                val next = DateTag.enterIndex(
                    field.text,
                    field.selection.start,
                    field.selection.end,
                    incoming.text,
                    incoming.selection.start,
                )?.let { idx ->
                    val tagged = DateTag.apply(incoming.text, idx, idx)
                        ?: return@let incoming.copy(text = incoming.text, selection = TextRange(idx + 1))
                    TextFieldValue(tagged.text, TextRange(tagged.caret))
                } ?: incoming
                field = next
                store.edit(next.text)
            },
            modifier = Modifier
                .fillMaxSize()
                .focusRequester(focus),
            textStyle = JotBody,
            cursorBrush = SolidColor(JotColors.accent),
            visualTransformation = DateTagVisualTransformation(),
            decorationBox = { inner ->
                if (field.text.isEmpty()) {
                    Text(
                        "Start writing…",
                        style = JotBody,
                        color = JotColors.dim,
                    )
                }
                inner()
            },
        )
    }
}

private class DateTagVisualTransformation : VisualTransformation {
    private val pattern = Regex("""\s*[|·]\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}\s*$""")

    override fun filter(text: AnnotatedString): TransformedText {
        val raw = text.text
        val out = buildAnnotatedString {
            var start = 0
            while (start <= raw.length) {
                val nl = raw.indexOf('\n', start)
                val end = if (nl == -1) raw.length else nl
                val line = raw.substring(start, end)
                val match = pattern.find(line)
                if (match != null) {
                    append(line.substring(0, match.range.first))
                    pushStyle(SpanStyle(color = JotColors.tag))
                    append(line.substring(match.range.first))
                    pop()
                } else {
                    append(line)
                }
                if (nl == -1) break
                append('\n')
                start = nl + 1
            }
        }
        return TransformedText(out, OffsetMapping.Identity)
    }
}
