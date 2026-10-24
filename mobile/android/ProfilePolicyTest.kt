package dev.opentunnel.vpn.data

import org.junit.Assert.*
import org.junit.Test
import java.util.Base64

class ProfilePolicyTest {
    private val a = VpnProfile(id = "00000000-0000-4000-8000-000000000001", server = "a.example.com")
    private val b = VpnProfile(id = "00000000-0000-4000-8000-000000000002", server = "b.example.com")
    private val pin = "pin-sha256:" + Base64.getEncoder().encodeToString(ByteArray(32))

    @Test fun latePinBelongsToItsOriginalProfileAndOrigin() {
        val result = ProfilePolicy.pin(listOf(b, a), a.id, "https://A.example.com:443/group", pin)
        assertEquals("", result[0].trustedCertificate)
        assertEquals(pin, result[1].trustedCertificate)
        assertEquals(listOf(b), ProfilePolicy.pin(listOf(b), a.id, a.server, pin))
        val edited = a.copy(server = "other.example.com")
        assertEquals(listOf(edited), ProfilePolicy.pin(listOf(edited), a.id, a.server, pin))
    }

    @Test fun invalidImportsAreRejectedBeforePersisting() {
        ProfilePolicy.validate(listOf(a, b))
        for (invalid in listOf(listOf(a, a), listOf(a.copy(id = "../bad")), listOf(a.copy(server = "http://insecure.test")), listOf(a.copy(username = "x".repeat(8193))))) {
            assertTrue(runCatching { ProfilePolicy.validate(invalid) }.isFailure)
        }
    }

    @Test fun changingTheServerClearsBoundCredentialsAndTrust() {
        val stored = a.copy(username = "fixture", password = "secret", tokenString = "token", trustedCertificate = pin)
        val changed = ProfilePolicy.updated(stored, stored.copy(server = b.server))
        assertEquals("", changed.password)
        assertEquals("", changed.username)
        assertEquals("", changed.tokenString)
        assertEquals("", changed.trustedCertificate)
        assertEquals(stored, ProfilePolicy.updated(stored.copy(server = a.server + "/group"), stored))
    }
}
