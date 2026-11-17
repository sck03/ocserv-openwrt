package dev.opentunnel.vpn.util


/** An IP address plus prefix length, in the form VpnService.Builder wants. */
data class Cidr(val address: String, val prefixLength: Int, val isIpv6: Boolean)

object Net {

    /**
     * Accepts the shapes openconnect hands back:
     *   "10.0.0.5" + "255.255.255.0"   (address + dotted netmask)
     *   "10.0.0.0/24"                  (already CIDR)
     *   "2001:db8::1/64"
     *   "192.168.1.7"                  (bare host — treated as /32 or /128)
     */
    fun parseCidr(raw: String, netmask: String? = null): Cidr? {
        val value = raw.trim()
        if (value.isEmpty()) return null

        val slash = value.indexOf('/')
        if (slash >= 0) {
            val host = value.substring(0, slash).trim()
            val suffix = value.substring(slash + 1).trim()
            if (!isValidIp(host)) return null
            val ipv6 = host.contains(':')
            // Some gateways send "10.0.0.0/255.255.255.0".
            val prefix = suffix.toIntOrNull() ?: maskToPrefix(suffix) ?: return null
            if (!validPrefix(prefix, ipv6)) return null
            return Cidr(host, prefix, ipv6)
        }

        if (!isValidIp(value)) return null
        val ipv6 = value.contains(':')
        val prefix = when {
            netmask.isNullOrBlank() -> if (ipv6) 128 else 32
            netmask.toIntOrNull() != null -> netmask.toInt()
            else -> maskToPrefix(netmask) ?: return null
        }
        if (!validPrefix(prefix, ipv6)) return null
        return Cidr(value, prefix, ipv6)
    }

    private fun validPrefix(prefix: Int, ipv6: Boolean) =
        prefix in 0..(if (ipv6) 128 else 32)

    /** "255.255.255.0" -> 24. Returns null when the mask is not contiguous. */
    fun maskToPrefix(mask: String): Int? {
        val parts = mask.trim().split('.')
        if (parts.size != 4) return null
        var bits = 0L
        for (part in parts) {
            val octet = part.toIntOrNull() ?: return null
            if (octet !in 0..255) return null
            bits = (bits shl 8) or octet.toLong()
        }
        // Must be a run of ones followed by a run of zeros.
        val inverted = bits.inv() and 0xFFFFFFFFL
        if ((inverted + 1) and inverted != 0L) return null
        return java.lang.Long.bitCount(bits and 0xFFFFFFFFL)
    }

    private val IPV4_REGEX = Regex("""^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$""")
    private val IPV6_REGEX = Regex("""^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))$""")

    fun isValidIp(value: String): Boolean {
        val trimmed = value.trim()
        if (trimmed.isEmpty()) return false
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
            return android.net.InetAddresses.isNumericAddress(trimmed)
        }
        return IPV4_REGEX.matches(trimmed) || IPV6_REGEX.matches(trimmed)
    }

    /**
     * RFC1918 + link-local + CGNAT + IPv6 ULA/link-local. Excluded from the
     * tunnel when "keep local network traffic off the VPN" is on, so printers,
     * NAS boxes and Chromecasts keep working while connected.
     */
    val LOCAL_NETWORKS: List<Cidr> = listOf(
        Cidr("10.0.0.0", 8, false),
        Cidr("172.16.0.0", 12, false),
        Cidr("192.168.0.0", 16, false),
        Cidr("169.254.0.0", 16, false),
        Cidr("100.64.0.0", 10, false),
        Cidr("fc00::", 7, true),
        Cidr("fe80::", 10, true),
    )


}
