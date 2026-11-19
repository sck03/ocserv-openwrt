package dev.opentunnel.vpn.data

import java.security.cert.CertificateFactory
import java.util.Base64

/** The public CA contract is PEM only, bounded, with every certificate validated. */
internal object PublicCa {
    private val block = Regex("-----BEGIN CERTIFICATE-----([A-Za-z0-9+/=\\r\\n]+)-----END CERTIFICATE-----")
    fun validate(bytes: ByteArray): ByteArray {
        require(bytes.size in 1..65536) { "Public CA exceeds 64 KiB" }
        val text = ConnectionFile.text(bytes)
        var offset = 0
        var count = 0
        val factory = CertificateFactory.getInstance("X.509")
        for (match in block.findAll(text)) {
            require(text.substring(offset, match.range.first).all { it in " \t\r\n" } && ++count <= 64) { "Invalid public CA" }
            val encoded = match.groupValues[1].replace("\r", "").replace("\n", "")
            val der = Base64.getDecoder().decode(encoded)
            require(Base64.getEncoder().encodeToString(der) == encoded) { "Invalid public CA encoding" }
            val certificate = factory.generateCertificate(der.inputStream())
            require(certificate.encoded.contentEquals(der)) { "Invalid public CA certificate" }
            offset = match.range.last + 1
        }
        require(count > 0 && text.substring(offset).all { it in " \t\r\n" }) { "CA must contain only PEM certificates" }
        return bytes
    }
}
