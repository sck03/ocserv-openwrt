package dev.opentunnel.vpn.data

import org.junit.Assert.*
import org.junit.Test

class ConnectionFileTest {
    private val pin = "pin-sha256:" + "A".repeat(43) + "="
    @Test fun publicFilesRoundTripWithoutPrivateFields() {
        val encoded = ConnectionFile(VpnProfile(name = "Office", server = "https://vpn.example.com", trustedCertificate = pin,
            username = "private", password = "secret", tokenString = "token")).encode()
        val decoded = ConnectionFile.parse(encoded).profile
        assertEquals(pin, decoded.trustedCertificate)
        assertEquals("", decoded.username)
        assertFalse(encoded.contains("secret"))
    }
    @Test fun rejectsOldAmbiguousOrExecutableProfiles() {
        val good = "[VPN]\nVersion=1\nServer=https://vpn.example.com\nTrust=prompt\n"
        for (raw in listOf(good.replace("Version=1\n", ""), good + "Server=https://other.example.com\n",
            good + "Script=payload\n", good + "ServerPin=$pin\n", good.replace("Version=1", "Version=2")))
            assertThrows(IllegalArgumentException::class.java) { ConnectionFile.parse(raw) }
    }
}
