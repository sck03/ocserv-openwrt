package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking

class SessionOwnerTest {
    @Test fun cancellationBeforeAttachRejectsLateWorker() {
        val owner = SessionOwner<Any>()
        val first = owner.begin()!!
        assertNull(owner.begin())
        assertTrue(owner.cancelStart(first))
        val next = owner.begin()!!
        assertFalse(owner.attach(first, Any()))
        assertFalse(owner.cancelStart(first))
        assertTrue(owner.attach(next, Any()))
    }

    @Test fun timeoutDoesNotReleaseAnActiveWorker() {
        val owner = SessionOwner<Any>()
        val worker = Any()
        val ticket = owner.begin()!!
        assertTrue(owner.attach(ticket, worker))
        assertTrue(owner.active(ticket))
        assertFalse(owner.cancelStart(ticket))
        assertSame(worker, owner.current())
        assertNull(owner.begin())
        assertNull(owner.finish(Any()))
        assertNotNull(owner.finish(worker))
        assertNotNull(owner.begin())
    }

    @Test fun lateCompletionCannotResetANewSession() {
        val owner = SessionOwner<Any>()
        val old = Any()
        owner.attach(owner.begin()!!, old)
        val completion = owner.finish(old)!!
        assertTrue(owner.idle(completion))
        val current = Any()
        owner.attach(owner.begin()!!, current)
        assertFalse(owner.idle(completion))
        assertNull(owner.finish(old))
        assertTrue(owner.owns(current))
    }

    @Test fun releaseWakesWaitersAndIgnoresForeignCancellation() = runBlocking {
        val owner = SessionOwner<Any>()
        val first = owner.begin()!!
        val waiter = async(start = CoroutineStart.UNDISPATCHED) { owner.awaitIdle() }
        assertFalse(owner.cancelStart(null))
        assertFalse(waiter.isCompleted)
        assertTrue(owner.cancelStart(first))
        assertTrue(owner.idle(waiter.await()))
        val next = owner.begin()!!
        val worker = Any()
        assertTrue(owner.attach(next, worker))
        val release = async(start = CoroutineStart.UNDISPATCHED) { owner.awaitIdle() }
        assertFalse(owner.active(first))
        assertEquals(owner.finish(worker), release.await())
    }
}
