package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class SessionControlTest {
    @Test fun cancelledStartupCannotPublishALateCore() {
        val control = SessionControl<Any>()
        control.cancel { fail("There is no published core") }
        assertFalse(control.publish(Any()))
        assertNull(control.current())
    }
    @Test fun cancellationAfterPublicationReachesTheOwnedCore() {
        val control = SessionControl<Any>()
        val core = Any()
        assertTrue(control.publish(core))
        var cancelled = 0
        repeat(2) { control.cancel { assertSame(core, it); cancelled++ } }
        assertEquals(2, cancelled)
        control.close { assertSame(core, it) }
        control.use { fail("Freed core was reused") }
    }
    @Test fun closeWaitsForAnInFlightAsyncCall() {
        val control = SessionControl<Any>()
        control.publish(Any())
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val closed = CountDownLatch(1)
        val caller = Thread { control.use { entered.countDown(); assertTrue(release.await(2, TimeUnit.SECONDS)) } }
        caller.start()
        assertTrue(entered.await(2, TimeUnit.SECONDS))
        val closer = Thread { control.close { closed.countDown() } }
        closer.start()
        assertFalse(closed.await(50, TimeUnit.MILLISECONDS))
        release.countDown()
        caller.join(2000); closer.join(2000)
        assertEquals(0L, closed.count)
    }
}
