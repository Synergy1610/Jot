package app.jot

import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.util.Locale

/**
 * Enter ends the current line with a date tag and starts the next one, exactly
 * like the desktop editor. There are no bullets.
 */
object DateTag {
    private val existing = Regex("""\s*[|·]\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}\s*$""")
    private val stamp = DateTimeFormatter.ofPattern("d MMM yyyy", Locale.US)

    data class Result(val text: String, val caret: Int)

    /**
     * Returns the index at which a single newline was inserted at the caret,
     * or null when the change was something else. Soft keyboards deliver Enter
     * as a plain "\n" insertion, which is how the editor recognises it.
     */
    fun enterIndex(
        oldText: String,
        oldStart: Int,
        oldEnd: Int,
        newText: String,
        newStart: Int,
    ): Int? {
        if (oldStart != oldEnd) return null
        if (newText.length != oldText.length + 1) return null
        val index = newStart - 1
        if (index < 0 || index >= newText.length) return null
        if (newText[index] != '\n') return null
        if (newText.removeRange(index, index + 1) != oldText) return null
        return index
    }

    fun apply(
        text: String,
        selectionStart: Int,
        selectionEnd: Int,
        today: LocalDate = LocalDate.now(),
    ): Result? {
        if (selectionStart < 0 || selectionEnd < selectionStart || selectionEnd > text.length) {
            return null
        }
        val lineStart = if (selectionStart == 0) 0 else text.lastIndexOf('\n', selectionStart - 1) + 1
        var lineEnd = text.indexOf('\n', selectionEnd)
        if (lineEnd < 0) lineEnd = text.length

        val line = text.substring(lineStart, lineEnd)
        if (existing.containsMatchIn(line)) {
            val newText = text.substring(0, lineEnd) + "\n" + text.substring(lineEnd)
            return Result(newText, lineEnd + 1)
        }

        var trimmedEnd = lineEnd
        while (trimmedEnd > lineStart) {
            val previous = text[trimmedEnd - 1]
            if (!previous.isWhitespace() && previous != '\uFEFF') break
            trimmedEnd--
        }
        val insertion = "  | ${stamp.format(today)}\n"
        val newText = text.substring(0, trimmedEnd) + insertion + text.substring(lineEnd)
        return Result(newText, trimmedEnd + insertion.length)
    }
}
