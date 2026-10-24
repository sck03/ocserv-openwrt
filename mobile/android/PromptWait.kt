package dev.opentunnel.vpn.core

import java.util.concurrent.BlockingQueue
import java.util.concurrent.TimeUnit

/** A cancel delivered before the prompt queue is published must still end it. */
internal object PromptWait {
    fun <T> await(queue: BlockingQueue<T>, cancelled: () -> Boolean, timeoutMs: Long): T? {
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs)
        while (!cancelled()) {
            val remaining = deadline - System.nanoTime()
            if (remaining <= 0) return null
            queue.poll(minOf(remaining, TimeUnit.MILLISECONDS.toNanos(100)), TimeUnit.NANOSECONDS)?.let { return it }
        }
        return null
    }
}
