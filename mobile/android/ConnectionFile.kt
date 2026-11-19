package dev.opentunnel.vpn.data

import dev.opentunnel.vpn.core.CertificatePolicy
import dev.opentunnel.vpn.core.ConnectionPolicy
import java.util.Base64

/** Version 1 public .vpn contract shared with the server and desktop clients. */
internal data class ConnectionFile(val profile: VpnProfile, val ca: ByteArray? = null) {
    companion object {
        const val MAX_BYTES = 128 * 1024
        private val keys = setOf("Version", "Name", "Server", "Protocol", "Trust", "ServerPin", "CABase64")
        fun read(input: java.io.InputStream, limit: Int = MAX_BYTES): ByteArray {
            val output = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                require(count > 0 && output.size() + count <= limit) { "Public profile is too large" }
                output.write(buffer, 0, count)
            }
            return output.toByteArray()
        }
        fun text(bytes: ByteArray): String = Charsets.UTF_8.newDecoder()
            .onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
            .decode(java.nio.ByteBuffer.wrap(bytes)).toString()
        fun parse(raw: String): ConnectionFile {
            require(raw.toByteArray(Charsets.UTF_8).size <= MAX_BYTES && '\u0000' !in raw) { "Invalid public profile size" }
            val fields = linkedMapOf<String, String>()
            var section = false
            for (line in raw.lineSequence()) {
                val text = line.removeSuffix("\r")
                if (text.isEmpty() || text.startsWith('#') || text.startsWith(';')) continue
                if (text == "[VPN]" && !section) { section = true; continue }
                val split = text.indexOf('=')
                require(section && split > 0) { "Invalid public profile" }
                val key = text.substring(0, split)
                require(key in keys && key !in fields) { "Unknown or duplicate public profile field" }
                fields[key] = text.substring(split + 1)
            }
            require(fields["Version"] == "1") { "Unsupported public profile version" }
            val trust = fields["Trust"]
            require(trust in setOf("pin", "ca", "prompt")) { "Unsupported certificate policy" }
            val pin = fields["ServerPin"].orEmpty()
            val encoded = fields["CABase64"].orEmpty()
            require((trust == "pin") == pin.isNotEmpty() && (trust == "ca") == encoded.isNotEmpty()) { "Ambiguous certificate policy" }
            require(pin.isEmpty() || CertificatePolicy.isFullPin(pin)) { "Invalid server pin" }
            val server = ConnectionPolicy.serverUrl(fields["Server"].orEmpty())
            val name = fields["Name"].orEmpty().ifBlank { java.net.URI(server).host }
            val protocol = fields["Protocol"] ?: "anyconnect"
            require(name.length <= 128 && name.toByteArray(Charsets.UTF_8).size <= 512 && server.toByteArray(Charsets.UTF_8).size <= 4096 &&
                protocol.matches(Regex("[a-z0-9-]{1,32}"))) { "Invalid public profile field" }
            val ca = if (encoded.isEmpty()) null else Base64.getDecoder().decode(encoded).also { bytes ->
                require(Base64.getEncoder().encodeToString(bytes) == encoded) { "Invalid public CA encoding" }
                PublicCa.validate(bytes)
            }
            return ConnectionFile(VpnProfile(name = name, server = server, protocol = protocol, trustedCertificate = pin), ca)
        }
    }
    fun encode(): String {
        require(profile.name.none { it == '\r' || it == '\n' || it == '\u0000' })
        val trust = if (profile.trustedCertificate.isNotEmpty()) "pin" else if (ca != null) "ca" else "prompt"
        val raw = "[VPN]\nVersion=1\nName=${profile.name}\nServer=${profile.server}\nProtocol=${profile.protocol}\nTrust=$trust\n" +
            (if (trust == "pin") "ServerPin=${profile.trustedCertificate}\n" else "") +
            (if (trust == "ca") "CABase64=${Base64.getEncoder().encodeToString(ca)}\n" else "")
        parse(raw)
        return raw
    }
}
