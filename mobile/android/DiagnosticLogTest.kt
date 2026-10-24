package dev.opentunnel.vpn.core

import org.junit.Assert.*
import org.junit.Test

class DiagnosticLogTest {
    @Test fun credentialsAndEncodedFieldsNeverReachLogs() {
        val log = DiagnosticLog()
        log.remember("Fixture secret/+")
        for (line in listOf("echo Fixture secret/+", "echo Fixture+secret%2F%2B", "Set-Cookie: opaque", "pa%73sword%3Dopaque", "<input value='opaque'>")) {
            assertEquals("[authentication data redacted]", log.clean(line))
        }
        assertEquals("TLS handshake failed", log.clean("TLS handshake failed"))
        assertFalse(log.clean("GET https://vpn.test/group?private=value").contains("private"))
    }

    @Test fun oversizedAndExcessiveFieldsFailClosed() {
        val log = DiagnosticLog()
        assertTrue(log.clean("x".repeat(8193)).length < 100)
        repeat(1100) { log.remember("distinct-secret-$it") }
        assertEquals("[authentication data redacted]", log.clean("new unknown value"))
        log.clear()
        assertEquals("connected", log.clean("connected"))
    }
}
