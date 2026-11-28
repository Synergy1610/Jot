package app.jot

/** Pure rules behind the phone's two explicit cloud buttons. */
object SyncText {
    /** Local text that has not been backed up yet. */
    fun isPending(local: String, base: String?): Boolean =
        if (base == null) local.isNotEmpty()
        else local.trimEnd('\n', '\r') != base.trimEnd('\n', '\r')

    /** True when the cloud copy changed after this phone's last sync. */
    fun cloudChanged(remote: String, base: String?, local: String): Boolean =
        remote.isNotEmpty() && remote != local && (base == null || remote != base)
}
