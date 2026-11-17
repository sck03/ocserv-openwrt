package dev.opentunnel.vpn.data

import dev.opentunnel.vpn.core.CertificatePolicy
import dev.opentunnel.vpn.core.ConnectionPolicy
import java.net.URI
import java.util.UUID

/** Shared by import, persistence and delayed certificate updates. */
internal object ProfilePolicy {
    const val MAX_BYTES = 1024 * 1024
    fun <T> reorder(current: List<T>, ids: List<String>, id: (T) -> String): List<T> {
        require(ids.size <= 128 && ids.distinct().size == ids.size) { "Invalid profile order" }
        val remaining = current.associateBy(id).toMutableMap()
        return ids.mapNotNull { remaining.remove(it) } + current.filter { id(it) in remaining }
    }
    fun origin(server: String): String = if (server.isBlank()) "" else URI(ConnectionPolicy.serverUrl(server)).let {
        "${it.host.lowercase()}:${if (it.port < 0) 443 else it.port}"
    }

    fun validate(profiles: List<VpnProfile>) {
        require(profiles.size <= 128 && profiles.map { it.id }.toSet().size == profiles.size) { "Invalid profile count or duplicate identifier" }
        profiles.forEach { profile ->
            require(UUID.fromString(profile.id).toString() == profile.id.lowercase()) { "Invalid profile identifier" }
            require(profile.name.length <= 128 && profile.server.length <= 4096) { "Profile is too long" }
            origin(profile.server)
            for (value in listOf(profile.username, profile.password, profile.tokenString, profile.authGroup))
                require(value.toByteArray(Charsets.UTF_8).size <= 8192 && '\u0000' !in value) { "Invalid credential field" }
            require(profile.trustedCertificate.isEmpty() || CertificatePolicy.isFullPin(profile.trustedCertificate)) { "Invalid certificate pin" }
            require(profile.mtu == 0 || profile.mtu in 576..1500) { "Invalid MTU" }
            require(!profile.overrideDpdTimeout || profile.dpdSeconds in 1..86400) { "Invalid DPD interval" }
        }
    }

    fun pin(profiles: List<VpnProfile>, id: String, server: String, pin: String): List<VpnProfile> {
        require(pin.isEmpty() || CertificatePolicy.isFullPin(pin)) { "Invalid certificate pin" }
        return profiles.map { if (it.id == id && origin(it.server) == origin(server)) it.copy(trustedCertificate = pin) else it }
    }

    fun portable(profile: VpnProfile): VpnProfile = profile.copy(username = "", password = "", tokenString = "",
        caCertPath = "", userCertPath = "", privateKeyPath = "")

    fun updated(previous: VpnProfile?, candidate: VpnProfile): VpnProfile {
        val normalized = if (candidate.overrideDpdTimeout) candidate else candidate.copy(dpdSeconds = 0)
        return if (previous != null && origin(previous.server) != origin(candidate.server))
            portable(normalized).copy(authGroup = "", trustedCertificate = "")
        else normalized
    }
}
