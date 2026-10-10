package dev.opentunnel.vpn.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class ConnectionPolicyTest {
    @Test fun preservesHostnameIpv6AndAuthenticationPath() {
        assertEquals("https://vpn.example.com:4443/group?q=one%20two", ConnectionPolicy.serverUrl(" VPN.Example.COM:4443/group?q=one%20two "))
        assertEquals("https://[2001:db8::1]:4443", ConnectionPolicy.serverUrl("[2001:DB8::1]:4443"))
        assertEquals("https://[::1]", ConnectionPolicy.serverUrl("https://[::1]:443"))
        assertEquals("https://xn--fsqu00a.xn--0zwm56d", ConnectionPolicy.serverUrl("例子.测试"))
        assertEquals("https://192.168.19.253:4443", ConnectionPolicy.serverUrl("192.168.19.253:4443"))
    }

    @Test fun rejectsMalformedAddressesBeforeAuthentication() {
        for (address in listOf("", "http://example.com", "user:pass@example.com", "vpn.example.com:",
            "vpn.example.com:0", "vpn.example.com:65536", "[::1:]", "2001:db8::1",
            "vpn.example.com/#fragment", "vpn.example.com\nPassword=x", "192.168.1.999", "127.1", "192.168.01.1")) {
            assertThrows(address, IllegalArgumentException::class.java) { ConnectionPolicy.serverUrl(address) }
        }
    }

    @Test fun negotiatedMtuIsAnUpperBound() {
        assertEquals(1280, ConnectionPolicy.tunnelMtu(1280, 1500, true))
        assertEquals(1350, ConnectionPolicy.tunnelMtu(1500, 1350, true))
        assertEquals(1200, ConnectionPolicy.tunnelMtu(1200, 0, false))
        assertEquals(1400, ConnectionPolicy.tunnelMtu(1400, 0, true))
        for (mtu in listOf(0, 575, 65536)) {
            assertThrows(IllegalArgumentException::class.java) { ConnectionPolicy.tunnelMtu(mtu, 0, false) }
        }
        assertThrows(IllegalArgumentException::class.java) { ConnectionPolicy.tunnelMtu(1200, 1500, true) }
    }
}
