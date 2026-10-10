package dev.opentunnel.vpn.util

import java.math.BigInteger
import java.net.InetAddress
import org.junit.Assert.*
import org.junit.Test

class RoutePolicyTest {
    private fun route(value: String) = Net.parseCidr(value)!!
    private fun covered(routes: List<Cidr>, value: String): Boolean {
        val bytes = InetAddress.getByName(value).address
        val address = BigInteger(1, bytes)
        return routes.any {
            val network = InetAddress.getByName(it.address).address
            network.size == bytes.size && address.shiftRight(bytes.size * 8 - it.prefixLength) ==
                BigInteger(1, network).shiftRight(bytes.size * 8 - it.prefixLength)
        }
    }

    @Test fun excludesBothFamiliesWithoutLosingNat64OrPublicRoutes() {
        val routes = RoutePolicy.routes(listOf(route("0.0.0.0/0"), route("::/0")),
            listOf(route("192.168.0.0/16"), route("fc00::/7"), route("fe80::/10")))
        for (ip in listOf("192.168.1.1", "fd00::1", "fe80::1")) assertFalse(ip, covered(routes, ip))
        for (ip in listOf("8.8.8.8", "2001:4860:4860::8888", "64:ff9b::808:808"))
            assertTrue(ip, covered(routes, ip))
    }

    @Test fun includesAndOverlappingExclusionsRespectExactBoundaries() {
        val routes = RoutePolicy.routes(listOf(route("2001:db8::/124"), route("2001:db8::/126")),
            listOf(route("2001:db8::4/127"), route("2001:db8::5/128")))
        for (host in 0..16) {
            assertEquals("host $host", host < 16 && host !in 4..5,
                covered(routes, "2001:db8::${host.toString(16)}"))
        }
        assertEquals(routes.size, routes.distinct().size)
    }

    @Test fun hostAndDefaultExclusionsHaveBoundedComplements() {
        val routes = RoutePolicy.routes(listOf(route("::/0")), listOf(route("::1/128")))
        assertEquals(128, routes.size)
        assertTrue(covered(routes, "::"))
        assertFalse(covered(routes, "::1"))
        assertTrue(covered(routes, "::2"))
        assertTrue(RoutePolicy.routes(listOf(route("::/0")), listOf(route("::/0"))).isEmpty())
        val ipv4 = RoutePolicy.routes(listOf(route("192.0.2.0/29")), listOf(route("192.0.2.7/32")))
        for (host in 0..8) assertEquals(host < 7, covered(ipv4, "192.0.2.$host"))
    }

    @Test fun domainsAndInvalidMasksAreNotNumericNetworks() {
        for (value in listOf("example.com", "*.example.com", "10.0.0.0/255.0.255.0", "fd00::/129"))
            assertNull(value, Net.parseCidr(value))
        assertNull(Net.parseCidr("10.0.0.2", "invalid"))
        assertEquals("10.0.0.2", Net.parseCidr("10.0.0.2", "255.255.255.0")!!.address)
    }
}
