package app.jot.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * The app's one button style: field fill, dim border, mono accent label.
 * [minHeight] and [horizontalPadding] let the sync bar grow the touch target
 * without changing how the button looks elsewhere.
 */
@Composable
internal fun JotButton(
    label: String,
    enabled: Boolean = true,
    highlight: Boolean = false,
    minHeight: Dp = 0.dp,
    horizontalPadding: Dp = 12.dp,
    verticalPadding: Dp = 7.dp,
    fontSize: TextUnit = 13.sp,
    lineHeight: TextUnit = 22.sp,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .heightIn(min = minHeight)
            .background(JotColors.field)
            .border(
                1.dp,
                when {
                    !enabled -> JotColors.field
                    highlight -> JotColors.accent
                    else -> JotColors.dim
                },
            )
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                enabled = enabled,
                onClick = onClick,
            )
            .padding(horizontal = horizontalPadding, vertical = verticalPadding),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            style = JotBody.copy(fontSize = fontSize, lineHeight = lineHeight),
            color = if (enabled) JotColors.accent else JotColors.dim,
            maxLines = 1,
        )
    }
}
