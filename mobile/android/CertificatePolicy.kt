package dev.opentunnel.vpn.core

import java.security.MessageDigest
import java.util.Base64
import java.io.ByteArrayInputStream
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.util.Date

/** Only complete, canonical SHA-256 public-key pins are accepted. */
internal object CertificatePolicy {
    fun validAt(der: ByteArray?, now: Date = Date()): Boolean = runCatching {
        require(der != null && der.isNotEmpty() && der.size <= 65536)
        val input = ByteArrayInputStream(der)
        val certificate = CertificateFactory.getInstance("X.509").generateCertificate(input) as X509Certificate
        require(input.available() == 0)
        certificate.checkValidity(now)
        true
    }.getOrDefault(false)

    fun isFullPin(pin: String): Boolean {
        if (pin.length != 55 || !pin.startsWith("pin-sha256:")) return false
        return runCatching {
            val encoded = pin.removePrefix("pin-sha256:")
            val bytes = Base64.getDecoder().decode(encoded)
            bytes.size == 32 && Base64.getEncoder().encodeToString(bytes) == encoded
        }.getOrDefault(false)
    }

    fun matches(expected: String, actual: String): Boolean =
        isFullPin(expected) && isFullPin(actual) &&
            MessageDigest.isEqual(expected.toByteArray(Charsets.US_ASCII), actual.toByteArray(Charsets.US_ASCII))
}
