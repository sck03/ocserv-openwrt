package dev.opentunnel.vpn.core

/** Publish/cancel/close use one lease; asynchronous JNI calls cannot race free(). */
internal class SessionControl<T : Any> {
    private var cancelled = false
    private var value: T? = null

    @Synchronized fun publish(session: T): Boolean {
        check(value == null) { "A native session is already published" }
        if (cancelled) return false
        value = session
        return true
    }

    @Synchronized fun current(): T? = value
    @Synchronized fun use(action: (T) -> Unit) { value?.let(action) }
    @Synchronized fun cancel(action: (T) -> Unit) {
        cancelled = true
        value?.let(action) // Repeated abort also reaches a newly published core.
    }
    @Synchronized fun close(action: (T) -> Unit) {
        val previous = value
        value = null
        previous?.let(action)
    }
}
