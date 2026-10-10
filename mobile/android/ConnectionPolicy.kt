package dev.opentunnel.vpn.core

import dev.opentunnel.vpn.util.Cidr
import dev.opentunnel.vpn.util.Net
import java.net.IDN
import java.net.URI
import java.net.URISyntaxException
import java.util.Locale

/** Address parsing never resolves DNS: OpenConnect retains all A/AAAA candidates and SNI. */
internal object ConnectionPolicy {
    fun serverUrl(raw: String): String {
        val value = raw.trim().let { if ("://" in it) it else "https://$it" }
        require(value.none { it <= ' ' || it == '\u007f' || it == '\\' }) { "Invalid server address" }
        val uri = try { URI(value) } catch (error: URISyntaxException) {
            throw IllegalArgumentException("Invalid server address", error)
        }
        require(uri.scheme.equals("https", ignoreCase = true) && uri.rawFragment == null) { "Use an HTTPS server address" }
        val authority = requireNotNull(uri.rawAuthority) { "Missing server hostname" }
        require('@' !in authority) { "Keep credentials out of the server address" }
        val host: String
        val suffix: String
        if (authority.startsWith('[')) {
            val end = authority.indexOf(']')
            require(end > 1 && '%' !in authority) { "Invalid IPv6 server address" }
            host = authority.substring(0, end + 1).lowercase(Locale.ROOT)
            suffix = authority.substring(end + 1)
        } else {
            require(authority.count { it == ':' } <= 1) { "Enclose IPv6 addresses in brackets" }
            val name = authority.substringBefore(':')
            host = IDN.toASCII(name, IDN.USE_STD3_ASCII_RULES).lowercase(Locale.ROOT)
            require(host.isNotEmpty() && host.length <= 253) { "Invalid server hostname" }
            if (host.all { it.isDigit() || it == '.' }) {
                val parts = host.split('.')
                require(parts.size == 4 && parts.all {
                    it.isNotEmpty() && (it.length == 1 || it[0] != '0') && (it.toIntOrNull() ?: -1) in 0..255
                }) { "Invalid IPv4 server address" }
            }
            suffix = authority.substring(name.length)
        }
        val port = if (suffix.isEmpty()) 443 else {
            require(suffix.startsWith(':') && suffix.length in 2..6 && suffix.drop(1).all { it in '0'..'9' }) { "Invalid server port" }
            suffix.drop(1).toInt().also { require(it in 1..65535) { "Invalid server port" } }
        }
        val result = "https://$host" + (if (port == 443) "" else ":$port") + uri.rawPath.orEmpty() +
            (uri.rawQuery?.let { "?$it" } ?: "")
        require(runCatching { URI(result).parseServerAuthority() }.isSuccess) { "Invalid server address" }
        return result
    }

    fun ipv6Address(enabled: Boolean, address: String?, netmask: String?): Cidr? {
        if (!enabled) return null
        val value = netmask?.takeIf { it.isNotBlank() } ?: address.orEmpty()
        return Net.parseCidr(value)?.takeIf { it.isIpv6 }
    }

    fun tunnelMtu(negotiated: Int, requested: Int, ipv6: Boolean): Int {
        require(negotiated in 576..65535 && (requested == 0 || requested in 576..65535)) { "Invalid tunnel MTU" }
        val mtu = if (requested > 0) minOf(negotiated, requested) else negotiated
        require(!ipv6 || mtu >= 1280) { "IPv6 requires a negotiated MTU of at least 1280" }
        return mtu
    }
}
