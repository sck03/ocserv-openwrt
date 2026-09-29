package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class CertificatePolicyTest {
    private val pin = "pin-sha256:" + java.util.Base64.getEncoder().encodeToString(ByteArray(32))

    @Test fun fullPinsOnly() {
        assertTrue(CertificatePolicy.isFullPin(pin))
        assertFalse(CertificatePolicy.isFullPin(pin.dropLast(1)))
        assertFalse(CertificatePolicy.isFullPin("pin-sha256:AAAA"))
        assertFalse(CertificatePolicy.isFullPin("sha1:1234567890"))
        assertFalse(CertificatePolicy.isFullPin(pin + "\n"))
    }

    @Test fun neverAcceptsPrefixOrChangedKey() {
        assertTrue(CertificatePolicy.matches(pin, pin))
        assertFalse(CertificatePolicy.matches(pin.take(20), pin))
        val other = "pin-sha256:" + java.util.Base64.getEncoder().encodeToString(ByteArray(32) { 1 })
        assertFalse(CertificatePolicy.matches(pin, other))
        assertFalse(CertificatePolicy.matches("", pin))
    }
}
