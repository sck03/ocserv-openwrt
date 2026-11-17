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

    @Test fun pinnedCertificatesStillRequireValidDatesAndDer() {
        val der = java.util.Base64.getDecoder().decode(
            "MIICqzCCAZOgAwIBAgIBATANBgkqhkiG9w0BAQsFADAZMRcwFQYDVQQDDA5yZXZpZXctZml4dHVyZTAeFw0yNjEwMjMyMDE4NTRaFw0yNjEwMjUyMDE4NTRaMBkxFzAVBgNVBAMMDnJldmlldy1maXh0dXJlMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsynfMcSD4M+BLrydBCflnMTZc1RFFue43GOPcyUncUhcsdV3bNtmxKSH5EkUlLBuRS2HxSE4AjyGp4fk4gA1qw1Vxfk3SsyU7WNFhmWtwS7PAUhaybPYza5C2n3/KmKEt3knZWn/LiuEty5C8szeq7Xas+b/QK7xiLPWst5s81IoedS+QcnqUz6x8LZkaBvpHgxE3y3rR4wPqHTUVN+1xWnS7gyZajW+csWvWlA8bYCvDYNZs+nqW0G9FAMpJPIXnqJUVyUiPMghAalbd9UTQQYPeypX6zr1aGGJ8flslKfp3DuqwIpO/a9wnTmYsLlzcPG4OuUyzrXjiJYUJoYrywIDAQABMA0GCSqGSIb3DQEBCwUAA4IBAQB0MqgPha/OS/lJtWZ1yiZGCZf+phJUgzT7/mPxANhQp+PJM/kHg8/bmHCLm8U3K7krAQkMRrajBOlFBCQ6lcs9XYpCmJUjQds5tlKdMm9rG1f0+2fF1ft7YRSiE0imwVIaVR/e3S4uMiDJmLnNs57kVxvJM6ka1ScZZLfdMZWngDV8QcChl1ydKLR4NH1mKX6DwEceQeMz7tGCI/ZH60SvruUWIFYYur8c6mb5GZ5mxcQyUzgGFcn9yfM6WsccxBltwEZ7gdARvASBcuOSZnywxiwGNkyMTZs32fj+vDo6KZn4+3wumKckrGnUGiY6umafemO6kWGztGo/LyaRlRgl")
        fun date(value: String) = java.util.Date.from(java.time.Instant.parse(value))
        assertTrue(CertificatePolicy.validAt(der, date("2026-10-24T00:00:00Z")))
        assertFalse(CertificatePolicy.validAt(der, date("2026-10-22T00:00:00Z")))
        assertFalse(CertificatePolicy.validAt(der, date("2026-10-26T00:00:00Z")))
        assertFalse(CertificatePolicy.validAt(null))
        assertFalse(CertificatePolicy.validAt(byteArrayOf(1, 2)))
        assertFalse(CertificatePolicy.validAt(der + byteArrayOf(1)))
    }
}
