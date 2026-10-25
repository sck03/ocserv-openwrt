package dev.opentunnel.vpn.util

import java.math.BigInteger
import java.net.InetAddress
import java.util.Random
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

    @Test fun manyHostExclusionsPreserveEveryOtherAddressInBothFamilies() {
        for ((network, format) in listOf(
            "192.0.2.0/24" to { host: Int -> "192.0.2.$host" },
            "2001:db8::/120" to { host: Int -> "2001:db8::${host.toString(16)}" }
        )) {
            val prefix = if (network.contains(':')) 128 else 32
            val excluded = (0..255 step 3).map { route("${format(it)}/$prefix") }
            val included = listOf(route(network))
            val routes = RoutePolicy.routes(included, excluded)
            val reversed = RoutePolicy.routes(included, excluded.reversed())
            for (host in 0..255) {
                assertEquals(format(host), host % 3 != 0, covered(routes, format(host)))
                assertEquals(format(host), host % 3 != 0, covered(reversed, format(host)))
            }
            assertEquals(routes.size, routes.distinct().size)
        }
    }

    @Test fun nestedAndDuplicateNetworksProduceTheSameRoutesInAnyOrder() {
        val included = listOf("192.0.2.0/24", "192.0.2.128/25", "192.0.2.0/26",
            "2001:db8::/120", "2001:db8::80/121").map(::route)
        val excluded = listOf("192.0.2.64/26", "192.0.2.64/27", "192.0.2.65/32",
            "2001:db8::40/122", "2001:db8::41/128").map(::route)
        val expected = RoutePolicy.routes(listOf(included[0], included[3]), listOf(excluded[0], excluded[3]))
        assertEquals(expected, RoutePolicy.routes(included + included, excluded + excluded))
        assertEquals(expected, RoutePolicy.routes(included.reversed(), excluded.reversed()))
        for (host in 0..255) {
            val retained = host !in 64..127
            assertEquals(retained, covered(expected, "192.0.2.$host"))
            assertEquals(retained, covered(expected, "2001:db8::${host.toString(16)}"))
        }
    }

    @Test(timeout = 5000) fun maximumAdjacentRulesCollapseBeforeSubtracting() {
        for (ipv6 in listOf(false, true)) {
            fun address(host: Int) = if (ipv6) "2001:db8::${host.toString(16)}"
                else "10.0.${host / 256}.${host % 256}"
            val bits = if (ipv6) 128 else 32
            val hosts = (0 until 4096).map { route("${address(it)}/$bits") }
            val merged = route("${address(0)}/${bits - 12}")
            assertEquals(RoutePolicy.routes(listOf(merged), emptyList()), RoutePolicy.routes(hosts.reversed(), emptyList()))
            val defaults = listOf(route(if (ipv6) "::/0" else "0.0.0.0/0"))
            assertEquals(RoutePolicy.routes(defaults, listOf(merged)), RoutePolicy.routes(defaults, hosts))
        }
    }

    @Test fun exclusionAcrossSeparateIncludesRetainsItsCursorAndAddressFamily() {
        val includes = listOf("10.0.0.0/24", "10.0.2.0/24", "2001:db8::/120").map(::route)
        val excludes = listOf("10.0.0.0/23", "10.0.2.0/25", "::/1").map(::route)
        val routes = RoutePolicy.routes(includes, excludes)
        assertEquals(RoutePolicy.routes(listOf(route("10.0.2.128/25")), emptyList()), routes)
        val spanning = listOf("10.0.0.0/32", "10.0.2.0/32").map(::route)
        assertTrue(RoutePolicy.routes(spanning, listOf(route("10.0.0.0/16"))).isEmpty())
    }

    @Test(timeout = 5000) fun oversizedFinalComplementIsRejectedEarly() {
        val excluded = (0 until 4096).map { route("2001:db8:${it.toString(16)}::1/128") }
        try {
            RoutePolicy.routes(listOf(route("::/0")), excluded)
            fail("An oversized complement was accepted")
        } catch (_: IllegalArgumentException) { }
        try {
            RoutePolicy.routes(List(4097) { route("0.0.0.0/0") }, emptyList())
            fail("Too many input routes were accepted")
        } catch (_: IllegalArgumentException) { }
    }

    @Test fun mixedRuleSetsMatchEveryAddressInSmallNetworks() {
        val random = Random(7263)
        fun address(host: Int, ipv6: Boolean) = if (ipv6) "2001:db8::${host.toString(16)}" else "192.0.2.$host"
        repeat(24) {
            fun rules() = buildList {
                for (ipv6 in listOf(false, true)) repeat(8) {
                    add(route("${address(random.nextInt(256), ipv6)}/${(if (ipv6) 128 else 32) - random.nextInt(9)}"))
                }
            }
            val included = rules()
            val excluded = rules()
            val actual = RoutePolicy.routes(included, excluded)
            for (ipv6 in listOf(false, true)) for (host in 0..255) {
                val ip = address(host, ipv6)
                assertEquals(ip, covered(included, ip) && !covered(excluded, ip), covered(actual, ip))
            }
        }
    }

    @Test fun maximumUnsignedAddressesStayInsideTheirFamily() {
        val defaults = listOf(route("0.0.0.0/0"), route("::/0"))
        val maximum = listOf("255.255.255.255", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
        val routes = RoutePolicy.routes(defaults, maximum.map { route(it) })
        for (ip in maximum) assertFalse(ip, covered(routes, ip))
        for (ip in listOf("0.0.0.0", "255.255.255.254", "::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe"))
            assertTrue(ip, covered(routes, ip))
    }
}
