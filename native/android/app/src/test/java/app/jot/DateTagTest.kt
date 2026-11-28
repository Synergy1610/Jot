package app.jot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.time.LocalDate

class DateTagTest {
    private val today = LocalDate.of(2026, 11, 27)

    @Test
    fun enterAddsTagAndNewLine() {
        val result = DateTag.apply("first note", 10, 10, today)!!
        assertEquals("first note  | 27 Nov 2026\n", result.text)
        assertEquals(result.text.length, result.caret)
    }

    @Test
    fun enterDoesNotDuplicateAnExistingTag() {
        val text = "first note  | 27 Nov 2026"
        val result = DateTag.apply(text, text.length, text.length, today)!!
        assertEquals("$text\n", result.text)
        assertEquals(result.text.length, result.caret)
    }

    @Test
    fun enterTrimsTrailingSpacesBeforeTagging() {
        val result = DateTag.apply("note   ", 7, 7, today)!!
        assertEquals("note  | 27 Nov 2026\n", result.text)
    }

    @Test
    fun enterOnlyTagsTheLineBeingFinished() {
        val text = "one  | 26 Nov 2026\ntwo"
        val result = DateTag.apply(text, text.length, text.length, today)!!
        assertEquals("one  | 26 Nov 2026\ntwo  | 27 Nov 2026\n", result.text)
    }

    @Test
    fun enterOnAnEmptyLineStartsWithTheTag() {
        val result = DateTag.apply("", 0, 0, today)!!
        assertEquals("  | 27 Nov 2026\n", result.text)
    }

    @Test
    fun detectsASingleNewlineInsertion() {
        val index = DateTag.enterIndex("abc", 3, 3, "abc\n", 4)
        assertEquals(3, index)
    }

    @Test
    fun ignoresOtherEdits() {
        assertNull(DateTag.enterIndex("abc", 3, 3, "abcd", 4))
        assertNull(DateTag.enterIndex("abc", 1, 2, "a\nc", 2))
        assertNull(DateTag.enterIndex("abc", 0, 0, "xabc", 1))
    }

    @Test
    fun keyboardEnterDoesNotAddAnExtraBlankLine() {
        val old = "abc"
        val incoming = "abc\n"
        val index = DateTag.enterIndex(old, 3, 3, incoming, 4)!!
        val result = DateTag.apply(incoming, index, index, today)!!
        assertEquals("abc  | 27 Nov 2026\n", result.text)
        assertEquals(result.text.length, result.caret)
    }

    @Test
    fun keyboardEnterOnTaggedLineDoesNotAddAnExtraBlankLine() {
        val old = "abc  | 27 Nov 2026"
        val incoming = "$old\n"
        val index = DateTag.enterIndex(old, old.length, old.length, incoming, incoming.length)!!
        val result = DateTag.apply(incoming, index, index, today)!!
        assertEquals(incoming, result.text)
    }
}
