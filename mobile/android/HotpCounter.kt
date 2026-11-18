package dev.opentunnel.vpn.core

/** Reserve the next counter durably before the native core emits an HOTP code. */
internal object HotpCounter {
    fun next(token: String): String {
        fun increment(value: String): String {
            require(value.isNotEmpty() && value.all { it in '0'..'9' }) { "HOTP needs an explicit counter" }
            val counter = value.toLongOrNull()
            require(counter != null && counter < Int.MAX_VALUE) { "HOTP counter is out of range" }
            return (counter + 1).toString()
        }
        if (token.startsWith("otpauth://hotp/", ignoreCase = true)) {
            val marker = Regex("([?&]counter=)([0-9]+)(?=&|$)")
            val matches = marker.findAll(token).toList()
            require(matches.size == 1) { "HOTP URI needs one counter" }
            val match = matches.single()
            return token.replaceRange(match.groups[2]!!.range, increment(match.groupValues[2]))
        }
        require(token.startsWith("base32:", ignoreCase = true) || token.startsWith("0x")) {
            "Use an otpauth HOTP URI or a base32/hex secret with a counter"
        }
        val separator = token.lastIndexOf(',')
        require(separator > 0) { "Append the HOTP counter after a comma" }
        return token.substring(0, separator + 1) + increment(token.substring(separator + 1))
    }
}
