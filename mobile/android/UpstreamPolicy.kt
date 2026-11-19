package dev.opentunnel.vpn.core

internal data class UpstreamCandidate(val id: Long, val validated: Boolean, val transportPreference: Int)

/** Used where Android does not expose its best-matching passive callback. */
internal object UpstreamPolicy {
    fun choose(candidates: Collection<UpstreamCandidate>, preferred: Long?, current: Long?): Long? {
        candidates.firstOrNull { it.id == preferred }?.let { return it.id }
        return candidates.maxWithOrNull(compareBy<UpstreamCandidate> { it.validated }
            .thenBy { it.transportPreference }.thenBy { it.id == current }.thenBy { it.id })?.id
    }
}
