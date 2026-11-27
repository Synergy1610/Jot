package app.jot.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.jot.DateTag
import app.jot.JotStore
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

    Box(Modifier.fillMaxSize().background(JotColors.ink)) {
        Column(Modifier.fillMaxSize().statusBarsPadding().imePadding()) {
            Header(store) { text ->
                tip = text
                tipToken++
            }
            Box(Modifier.fillMaxWidth().height(1.dp).background(JotColors.dim))
            EditorField(store, Modifier.weight(1f))
        }

        tip?.let { text ->
            Box(
                Modifier
                    .align(Alignment.TopEnd)
                    .statusBarsPadding()
                    .padding(top = 44.dp, end = 8.dp)
                    .background(JotColors.ink)
                    .padding(horizontal = 9.dp, vertical = 5.dp)
            ) {
                Text(text, style = JotBody, color = JotColors.text)
            }
        }
    }
}

@Composable
private fun Header(store: JotStore, onTip: (String) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .height(40.dp)
            .padding(start = 16.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(store.dateLabel, style = JotHeader)
        Spacer(Modifier.weight(1f))
        CloudBadge(store.cloudSignedIn) { onTip(store.cloudTooltip) }
        Spacer(Modifier.width(8.dp))
        DeviceBadge(store) { onTip(store.localTooltip) }
        Spacer(Modifier.weight(0.18f))
        GearButton { store.settingsOpen = true }
    }
}

@Composable
private fun CloudBadge(signedIn: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .width(21.dp)
            .fillMaxHeight()
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = onClick,
            ),
        contentAlignment = Alignment.CenterStart,
    ) {
        Canvas(Modifier.padding(start = 6.dp).size(15.dp)) {
            val unit = size.width / 15f
            scale(unit, unit) {
                val path = Path().apply {
                    moveTo(4f, 12f)
                    cubicTo(0f, 12f, 0f, 7f, 3.5f, 7f)
                    cubicTo(4f, 2.5f, 10f, 2f, 11.5f, 6f)
                    cubicTo(15.5f, 6f, 15.5f, 12f, 12f, 12f)
                    close()
                }
                drawPath(
                    path = path,
                    color = if (signedIn) JotColors.accent else JotColors.dim,
                    style = Stroke(width = 1.2f, cap = StrokeCap.Round),
                )
            }
        }
    }
}

@Composable
private fun DeviceBadge(store: JotStore, onClick: () -> Unit) {
    Box(
        Modifier
            .width(40.dp)
            .fillMaxHeight()
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = onClick,
            ),
        contentAlignment = Alignment.CenterStart,
    ) {
        Row(Modifier.padding(start = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            MonitorIcon(store)
            Spacer(Modifier.width(5.dp))
            when {
                store.saving -> Spinner()
                store.localError -> Text(
                    "!",
                    style = JotBody.copy(fontSize = 11.sp, color = JotColors.error),
                )
                else -> Text(
                    "✓",
                    style = JotBody.copy(fontSize = 11.sp, color = JotColors.accent),
                )
            }
        }
    }
}

@Composable
private fun MonitorIcon(store: JotStore) {
    val color = when {
        store.localError -> JotColors.error
        store.ready -> JotColors.accent
        else -> JotColors.dim
    }
    Canvas(Modifier.size(15.dp)) {
        val unit = size.width / 15f
        scale(unit, unit) {
            drawRect(
                color = color,
                topLeft = Offset(1.5f, 2.5f),
                size = Size(12f, 8f),
                style = Stroke(width = 1.2f),
            )
            drawLine(color, Offset(7.5f, 10.5f), Offset(7.5f, 13f), strokeWidth = 1.2f)
            drawLine(color, Offset(4.5f, 13f), Offset(10.5f, 13f), strokeWidth = 1.2f)
        }
    }
}

@Composable
private fun Spinner() {
    val transition = rememberInfiniteTransition(label = "save")
    val angle by transition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(tween(900, easing = LinearEasing), RepeatMode.Restart),
        label = "angle",
    )
    Canvas(Modifier.size(9.dp)) {
        rotate(angle) {
            drawArc(
                color = JotColors.accent,
                startAngle = 0f,
                sweepAngle = 270f,
                useCenter = false,
                style = Stroke(width = 1.2.dp.toPx(), cap = StrokeCap.Round),
            )
        }
    }
}

@Composable
private fun GearButton(onClick: () -> Unit) {
    Box(
        Modifier
            .size(width = 28.dp, height = 40.dp)
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = onClick,
            ),
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(17.dp)) {
            val unit = size.width / 24f
            scale(unit, unit) {
                val stroke = Stroke(width = 1.6f, cap = StrokeCap.Round)
                drawCircle(
                    color = JotColors.accent,
                    radius = 6f,
                    center = Offset(12f, 12f),
                    style = stroke,
                )
                val spokes = listOf(
                    Pair(Offset(12f, 2.5f), Offset(12f, 4.8f)),
                    Pair(Offset(12f, 19.2f), Offset(12f, 21.5f)),
                    Pair(Offset(2.5f, 12f), Offset(4.8f, 12f)),
                    Pair(Offset(19.2f, 12f), Offset(21.5f, 12f)),
                    Pair(Offset(5.3f, 5.3f), Offset(6.9f, 6.9f)),
                    Pair(Offset(17.1f, 17.1f), Offset(18.7f, 18.7f)),
                    Pair(Offset(18.7f, 5.3f), Offset(17.1f, 6.9f)),
                    Pair(Offset(6.9f, 17.1f), Offset(5.3f, 18.7f)),
                )
                for ((from, to) in spokes) drawLine(JotColors.accent, from, to, 1.6f)
            }
        }
    }
}

@Composable
private fun EditorField(store: JotStore, modifier: Modifier) {
    var field by remember { mutableStateOf(TextFieldValue(store.content)) }

    LaunchedEffect(store.content) {
        val text = store.content
        if (text == field.text) return@LaunchedEffect
        val caret = if (text.endsWith(field.text)) {
            field.selection.start + (text.length - field.text.length)
        } else {
            field.selection.start
        }
        field = TextFieldValue(text, TextRange(caret.coerceIn(0, text.length)))
    }

    BasicTextField(
        value = field,
        onValueChange = { next ->
            val entered = DateTag.enterIndex(
                field.text,
                field.selection.start,
                field.selection.end,
                next.text,
                next.selection.start,
            )
            if (entered != null) {
                val tagged = DateTag.apply(field.text, field.selection.start, field.selection.end)
                if (tagged != null) {
                    field = TextFieldValue(tagged.text, TextRange(tagged.caret))
                    store.edit(tagged.text)
                    return@BasicTextField
                }
            }
            field = next
            store.edit(next.text)
        },
        modifier = modifier
            .fillMaxSize()
            .padding(start = 16.dp, end = 16.dp, top = 10.dp, bottom = 12.dp),
        enabled = store.ready,
        textStyle = JotBody,
        cursorBrush = SolidColor(JotColors.accent),
        visualTransformation = DateTagHighlight,
        decorationBox = { inner ->
            Box(Modifier.fillMaxSize()) {
                if (field.text.isEmpty()) {
                    Text("Start typing…", style = JotBody.copy(color = JotColors.dim))
                }
                inner()
            }
        },
    )
}

private object DateTagHighlight : VisualTransformation {
    private val pattern = Regex("""[|·]\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}""")

    override fun filter(text: AnnotatedString): TransformedText {
        val spans = pattern.findAll(text.text).toList()
        if (spans.isEmpty()) return TransformedText(text, OffsetMapping.Identity)
        val styled = buildAnnotatedString {
            append(text.text)
            for (match in spans) {
                addStyle(SpanStyle(color = JotColors.tag), match.range.first, match.range.last + 1)
            }
        }
        return TransformedText(styled, OffsetMapping.Identity)
    }
}
