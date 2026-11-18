package dev.opentunnel.vpn.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first

/** Process-wide lease: a stopped service cannot orphan a still-running native worker. */
internal class SessionOwner<T : Any> {
    private var generation = 0L
    private var starting = false
    private var worker: T? = null
    private val released = MutableStateFlow(0L)

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

    @Synchronized fun cancelStart(ticket: Long?): Boolean {
        if (!starting || ticket != generation) return false
        starting = false
        released.value = ++generation
        return true
    }

    @Synchronized fun current(): T? = worker
    @Synchronized fun active(ticket: Long?) = ticket == generation && (starting || worker != null)
    suspend fun awaitIdle(): Long = released.first { idle(it) }
    @Synchronized fun owns(value: T) = worker === value

    @Synchronized fun finish(value: T): Long? {
        if (worker !== value) return null
        worker = null
        released.value = ++generation
        return generation
    }

    @Synchronized fun idle(ticket: Long) = !starting && worker == null && generation == ticket
}
