package dev.opentunnel.vpn.util

import org.junit.Assert.*
import org.junit.Test

class NetworkRoutesTest {
    private fun routes(entries: List<String>, include: Boolean = true, ipv6: Boolean = false) =
        NetworkRoutes.routes(emptyList(), emptyList(), NetworkRoutes.Rules(true, include, entries), false, ipv6)

    @Test fun domainAndWildcardRulesCannotBecomeADefaultRoute() {
        for (entry in listOf("*.example.com", ".example.com", "example.com"))
            assertThrows(IllegalArgumentException::class.java) { routes(listOf(entry)) }
        assertThrows(IllegalArgumentException::class.java) { routes(emptyList()) }
    }
    @Test fun numericSelectionPreservesItsExactScope() {
        assertEquals(listOf(Cidr("192.0.2.0", 24, false)), routes(listOf("192.0.2.0/24")))
        assertThrows(IllegalArgumentException::class.java) { routes(listOf("fd77::/64")) }
        val dualStack = routes(listOf("fd77::/64"), ipv6 = true)
        assertEquals(1, dualStack.size)
        assertTrue(dualStack[0].isIpv6)
        assertEquals(64, dualStack[0].prefixLength)
    }
    @Test fun invalidServerNetworksAreRejectedInsteadOfSilentlyBroadened() {
        assertThrows(IllegalArgumentException::class.java) {
            NetworkRoutes.routes(listOf("invalid"), emptyList(), NetworkRoutes.Rules(false, false, emptyList()), false, false)
        }
    }

    @Test fun explicitPrivateNetworksSurviveDefaultLocalBypass() {
        val rules = NetworkRoutes.Rules(false, false, emptyList())
        assertEquals(listOf(Cidr("192.168.19.0", 24, false)),
            NetworkRoutes.routes(listOf("192.168.19.0/24"), emptyList(), rules, true, false))
        assertThrows(IllegalArgumentException::class.java) {
            NetworkRoutes.routes(listOf("192.168.19.0/24"), listOf("192.168.19.0/24"), rules, true, false)
        }
        val routes = NetworkRoutes.routes(emptyList(), emptyList(), rules, true, false, listOf("10.77.0.1"))
        assertTrue(routes.contains(Cidr("10.77.0.1", 32, false)))
        assertFalse(routes.any { it.address == "10.0.0.0" && it.prefixLength == 8 })
    }
}
