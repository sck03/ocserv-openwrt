package dev.opentunnel.vpn.core

/** Process-wide lease: a stopped service cannot orphan a still-running native worker. */
internal class SessionOwner<T : Any> {
    private var generation = 0L
    private var starting = false
    private var worker: T? = null

    @Synchronized fun begin(): Long? {
        if (starting || worker != null) return null
        starting = true
        return ++generation
    }

    @Synchronized fun attach(ticket: Long, value: T): Boolean {
        if (!starting || ticket != generation || worker != null) return false
        starting = false
        worker = value
        return true
    }

    @Synchronized fun cancelStart(ticket: Long? = null): Boolean {
        if (!starting || (ticket != null && ticket != generation)) return false
        starting = false
        ++generation
        return true
    }

    @Synchronized fun current(): T? = worker
    @Synchronized fun busy() = starting || worker != null
    @Synchronized fun owns(value: T) = worker === value

    @Synchronized fun finish(value: T): Long? {
        if (worker !== value) return null
        worker = null
        return ++generation
    }

    @Synchronized fun idle(ticket: Long) = !starting && worker == null && generation == ticket
}
