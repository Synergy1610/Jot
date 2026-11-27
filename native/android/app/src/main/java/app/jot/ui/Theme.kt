package app.jot.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.sp

object JotColors {
    val ink = Color(0xFF090809)
    val panel = Color(0xFF0E0D0E)
    val text = Color(0xFFFAFCFB)
    val accent = Color(0xFFB59790)
    val tag = Color(0xFF87A9B0)
    val dim = Color(0xFF584E51)
    val error = Color(0xFFC38B7B)
    val field = Color(0xFF131112)
}

val JotBody = TextStyle(
    fontFamily = FontFamily.Monospace,
    fontSize = 13.sp,
    lineHeight = 18.sp,
    color = JotColors.text,
)

val JotHeader = JotBody.copy(fontSize = 12.sp, color = JotColors.accent)

val JotLabel = JotBody.copy(color = JotColors.accent)

val JotHeading = TextStyle(
    fontFamily = FontFamily.Monospace,
    fontSize = 15.sp,
    color = JotColors.tag,
)

val JotTitle = TextStyle(
    fontFamily = FontFamily.Monospace,
    fontSize = 17.sp,
    color = JotColors.text,
)

@Composable
fun JotTheme(content: @Composable () -> Unit) {
    val scheme = darkColorScheme(
        primary = JotColors.accent,
        onPrimary = JotColors.ink,
        background = JotColors.ink,
        onBackground = JotColors.text,
        surface = JotColors.ink,
        onSurface = JotColors.text,
        error = JotColors.error,
    )
    MaterialTheme(
        colorScheme = scheme,
        typography = MaterialTheme.typography.copy(
            bodyLarge = JotBody,
            bodyMedium = JotBody,
            labelLarge = JotBody,
        ),
        content = content,
    )
}
