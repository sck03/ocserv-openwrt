package dev.opentunnel.vpn.core

import java.util.concurrent.BlockingQueue
import java.util.concurrent.TimeUnit

/** The owning worker interrupts on cancellation, including before publication. */
internal object PromptWait {
    fun <T> await(queue: BlockingQueue<T>, cancelled: () -> Boolean, timeoutMs: Long): T? {
        if (cancelled()) return null
        return try {
            queue.poll(timeoutMs, TimeUnit.MILLISECONDS).takeUnless { cancelled() }
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            null
        }
    }
}
