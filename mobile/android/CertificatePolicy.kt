package dev.opentunnel.vpn.core

import java.security.MessageDigest
import java.util.Base64

/** Only complete, canonical SHA-256 public-key pins are accepted. */
internal object CertificatePolicy {
    fun isFullPin(pin: String): Boolean {
        if (!pin.startsWith("pin-sha256:")) return false
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
