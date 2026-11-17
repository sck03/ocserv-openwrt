package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class AppRoutingPolicyTest {
    @Test fun emptyAndMissingAllowlistRejectConnection() {
        for (packages in listOf(emptySet(), setOf("uninstalled"))) {
            assertThrows(IllegalStateException::class.java) { AppRoutingPolicy.apply(true, true, packages) { false } }
        }
    }
    @Test fun disabledAndEmptyExclusionDoNotAddApplications() {
        assertEquals(0, AppRoutingPolicy.apply(false, true, setOf("app")) { error("unexpected call") })
        assertEquals(0, AppRoutingPolicy.apply(true, false, emptySet()) { error("unexpected call") })
    }
    @Test fun survivingAllowlistIsUsedAndUnexpectedErrorsAbort() {
        assertEquals(1, AppRoutingPolicy.apply(true, true, setOf("installed", "removed")) { it == "installed" })
        assertThrows(SecurityException::class.java) { AppRoutingPolicy.apply(true, true, setOf("app")) { throw SecurityException() } }
    }
}
