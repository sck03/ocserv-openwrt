package dev.opentunnel.vpn.util

import java.math.BigInteger
import java.net.InetAddress

/** Express exclusions as included prefixes on every supported Android version. */
internal object RoutePolicy {
    private const val MAX_ROUTES = 2048
    private const val MAX_INPUT_ROUTES = 4096
    private val ONE = BigInteger.ONE

    private data class Range(val first: BigInteger, val last: BigInteger, val bits: Int) {
        fun appendCidrs(result: MutableList<Cidr>) {
            var address = first
            while (address <= last) {
                // Take the largest aligned prefix wholly inside this final
                // range. Later exclusions cannot invalidate an emitted route.
                require(result.size < MAX_ROUTES) { "The route exclusions produce too many tunnel routes" }
                val alignment = if (address.signum() == 0) bits else address.lowestSetBit
                val available = last.subtract(address).add(ONE).bitLength() - 1
                val hostBits = minOf(alignment, available)
                result.add(cidr(address, bits - hostBits))
                address = address.add(ONE.shiftLeft(hostBits))
            }
        }

        private fun cidr(address: BigInteger, prefix: Int): Cidr {
            val bytes = address.toByteArray()
            val fixed = ByteArray(bits / 8)
            val count = minOf(bytes.size, fixed.size)
            bytes.copyInto(fixed, fixed.size - count, bytes.size - count)
            return Cidr(InetAddress.getByAddress(fixed).hostAddress!!, prefix, bits == 128)
        }
    }

    private fun range(cidr: Cidr): Range {
        val bits = if (cidr.isIpv6) 128 else 32
        require(cidr.prefixLength in 0..bits && Net.isValidIp(cidr.address)) { "Invalid route" }
        val bytes = InetAddress.getByName(cidr.address).address
        require(bytes.size * 8 == bits) { "Route address family mismatch" }
        val hostBits = bits - cidr.prefixLength
        val network = BigInteger(1, bytes).shiftRight(hostBits).shiftLeft(hostBits)
        return Range(network, network.add(ONE.shiftLeft(hostBits)).subtract(ONE), bits)
    }

    private fun ranges(routes: List<Cidr>): List<Range> {
        val sorted = routes.map(::range).sortedWith(compareBy({ it.bits }, { it.first }, { it.last }))
        return buildList {
            for (candidate in sorted) {
                val previous = lastOrNull()
                if (previous != null && previous.bits == candidate.bits &&
                    candidate.first <= previous.last.add(ONE)) {
                    if (candidate.last > previous.last)
                        this[lastIndex] = Range(previous.first, candidate.last, candidate.bits)
                } else add(candidate)
            }
        }
    }

    fun routes(included: List<Cidr>, excluded: List<Cidr>): List<Cidr> {
        require(included.size <= MAX_INPUT_ROUTES && excluded.size <= MAX_INPUT_ROUTES) { "Too many tunnel routes" }
        val includes = ranges(included)
        val excludes = ranges(excluded)
        val result = mutableListOf<Cidr>()
        var exclusion = 0
        // Both lists are disjoint and ordered. Sweep once instead of repeatedly
        // splitting every retained CIDR for each exclusion (quadratic work).
        for (network in includes) {
            var address = network.first
            while (exclusion < excludes.size) {
                val omit = excludes[exclusion]
                if (omit.bits < network.bits || (omit.bits == network.bits && omit.last < address)) {
                    exclusion++
                    continue
                }
                if (omit.bits != network.bits || omit.first > network.last) break
                if (omit.first > address)
                    Range(address, omit.first.subtract(ONE), network.bits).appendCidrs(result)
                address = maxOf(address, omit.last.add(ONE))
                if (address > network.last) break
                exclusion++
            }
            if (address <= network.last) Range(address, network.last, network.bits).appendCidrs(result)
        }
        return result
    }
}
