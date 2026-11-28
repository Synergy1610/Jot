package app.jot

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SyncTextTest {
    @Test
    fun pendingIsBasedOnTheLastBackedUpCopy() {
        assertTrue(SyncText.isPending("phone note", null))
        assertFalse(SyncText.isPending("", null))
        assertFalse(SyncText.isPending("same", "same"))
        assertTrue(SyncText.isPending("same\nnew", "same"))
        assertFalse(SyncText.isPending("same\n", "same"))
    }

    @Test
    fun cloudChangedRequiresANewCloudCopy() {
        assertTrue(SyncText.cloudChanged("laptop note", "old base", "phone note"))
        assertTrue(SyncText.cloudChanged("laptop note", null, ""))
        assertFalse(SyncText.cloudChanged("", "old base", "phone note"))
        assertFalse(SyncText.cloudChanged("laptop note", "laptop note", "phone note"))
        assertFalse(SyncText.cloudChanged("same", "old base", "same"))
    }
}
