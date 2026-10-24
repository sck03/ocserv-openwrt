package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

class PromptWaitTest {
    @Test fun cancellationBeforePromptPublicationDoesNotWaitForItsTimeout() {
        assertNull(PromptWait.await(ArrayBlockingQueue<String>(1), { true }, 180_000))
    }
    @Test fun cancellationWhileWaitingEndsAnUnansweredPrompt() {
        val cancelled = AtomicBoolean(false)
        val thread = Thread {
            assertNull(PromptWait.await(ArrayBlockingQueue<String>(1), cancelled::get, 180_000))
        }
        thread.start()
        cancelled.set(true)
        thread.join(2000)
        assertFalse(thread.isAlive)
    }
    @Test fun answersArePreservedAndTimeoutIsBounded() {
        val queue = ArrayBlockingQueue<String>(1)
        queue.add("answer")
        assertEquals("answer", PromptWait.await(queue, { false }, 1000))
        assertNull(PromptWait.await(queue, { false }, 1))
    }
}
