package dev.opentunnel.vpn.util

import java.math.BigInteger
import java.net.InetAddress

/** Express exclusions as included prefixes on every supported Android version. */
internal object RoutePolicy {
    private data class Block(val address: BigInteger, val prefix: Int, val bits: Int) {
        fun contains(other: Block) = bits == other.bits && prefix <= other.prefix &&
            address.shiftRight(bits - prefix) == other.address.shiftRight(bits - prefix)

        fun subtract(other: Block, result: MutableList<Block>) {
            if (other.contains(this)) return
            if (!contains(other)) { result.add(this); return }
            val child = prefix + 1
            Block(address, child, bits).subtract(other, result)
            Block(address.setBit(bits - child), child, bits).subtract(other, result)
        }

        fun cidr(): Cidr {
            val bytes = address.toByteArray()
            val fixed = ByteArray(bits / 8)
            val count = minOf(bytes.size, fixed.size)
            bytes.copyInto(fixed, fixed.size - count, bytes.size - count)
            return Cidr(InetAddress.getByAddress(fixed).hostAddress!!, prefix, bits == 128)
        }
    }

    private fun block(cidr: Cidr): Block {
        val bits = if (cidr.isIpv6) 128 else 32
        require(cidr.prefixLength in 0..bits && Net.isValidIp(cidr.address)) { "Invalid route" }
        val bytes = InetAddress.getByName(cidr.address).address
        require(bytes.size * 8 == bits) { "Route address family mismatch" }
        val hostBits = bits - cidr.prefixLength
        val network = BigInteger(1, bytes).shiftRight(hostBits).shiftLeft(hostBits)
        return Block(network, cidr.prefixLength, bits)
    }

    private fun blocks(routes: List<Cidr>): List<Block> {
        val sorted = routes.map(::block).sortedWith(compareBy({ it.bits }, { it.address }, { it.prefix }))
        return buildList {
            // Aligned CIDRs are either disjoint or nested. A retained parent is
            // still the last block when its duplicate or child is encountered.
            for (candidate in sorted) {
                if (lastOrNull()?.contains(candidate) != true) add(candidate)
            }
        }
    }

    fun routes(included: List<Cidr>, excluded: List<Cidr>): List<Cidr> {
        var remaining = blocks(included)
        for (excludedBlock in blocks(excluded)) {
            remaining = buildList { for (route in remaining) route.subtract(excludedBlock, this) }
        }
        return remaining.map { it.cidr() }
    }
}
