package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class UpstreamPolicyTest {
    private val wifi = UpstreamCandidate(1, true, 1)
    private val mobile = UpstreamCandidate(2, true, 0)
    @Test fun callbackOrderDoesNotChooseTheLastAvailableNetwork() {
        assertEquals(1L, UpstreamPolicy.choose(listOf(wifi, mobile), null, null))
        assertEquals(1L, UpstreamPolicy.choose(listOf(mobile, wifi), null, null))
    }
    @Test fun systemChoiceAndValidationOverrideTransportHeuristics() {
        assertEquals(2L, UpstreamPolicy.choose(listOf(wifi, mobile), 2, 1))
        assertEquals(2L, UpstreamPolicy.choose(listOf(wifi.copy(validated = false), mobile), null, 1))
        assertEquals(1L, UpstreamPolicy.choose(listOf(wifi.copy(validated = false)), null, null))
    }
    @Test fun equivalentNetworksStayStableAndLossIsExplicit() {
        assertEquals(1L, UpstreamPolicy.choose(listOf(wifi, wifi.copy(id = 3)), null, 1))
        assertNull(UpstreamPolicy.choose(emptyList(), null, 1))
    }
}
