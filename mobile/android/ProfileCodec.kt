package dev.opentunnel.vpn.data

import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

@Serializable
internal data class StoredProfile(val profile: VpnProfile, val password: String = "", val token: String = "")
@Serializable
private data class ProfileFile(val schema: Int, val profiles: List<StoredProfile>)

/** Metadata is readable without Keystore access. Only a connection unlocks credentials. */
internal class ProfileCodec(
    private val encrypt: (String, String) -> String,
    private val decrypt: (String, String) -> String,
) {
    private val json = Json { encodeDefaults = true }
    private fun context(p: VpnProfile, kind: String) = "Lansyra/3/${p.id}/${ProfilePolicy.origin(p.server)}/$kind"
    private fun checked(value: String): String = value.also {
        check(it.isNotEmpty()) { "Credential operation failed; original data was preserved" }
    }

    fun update(candidate: VpnProfile, previous: StoredProfile?): StoredProfile {
        val p = ProfilePolicy.updated(previous?.profile, candidate)
        ProfilePolicy.validate(listOf(p))
        val sameOrigin = previous != null && ProfilePolicy.origin(previous.profile.server) == ProfilePolicy.origin(p.server)
        fun protect(value: String, old: String, kind: String): String =
            if (value.isNotEmpty()) checked(encrypt(value, context(p, kind))) else if (sameOrigin) old else ""
        return StoredProfile(p.copy(password = "", tokenString = ""),
            if (p.savePassword) protect(p.password, previous?.password.orEmpty(), "password") else "",
            if (p.softwareTokenMode != 0) protect(p.tokenString, previous?.token.orEmpty(), "token") else "")
    }

    fun unlock(stored: StoredProfile): VpnProfile {
        fun unprotect(value: String, kind: String) =
            if (value.isEmpty()) "" else checked(decrypt(value, context(stored.profile, kind)))
        return stored.profile.copy(password = unprotect(stored.password, "password"),
            tokenString = unprotect(stored.token, "token")).also { ProfilePolicy.validate(listOf(it)) }
    }

    fun encode(profiles: List<StoredProfile>): String {
        validate(profiles)
        return json.encodeToString(ProfileFile(3, profiles)).also(::size)
    }

    fun decode(raw: String): List<StoredProfile> {
        if (raw.isEmpty()) return emptyList()
        size(raw)
        val file = json.decodeFromString<ProfileFile>(raw)
        require(file.schema == 3) { "Unsupported profile storage schema" }
        return file.profiles.also(::validate)
    }

    private fun size(raw: String) {
        require(raw.toByteArray(Charsets.UTF_8).size <= ProfilePolicy.MAX_BYTES) { "Profile storage is too large" }
    }

    private fun validate(profiles: List<StoredProfile>) {
        ProfilePolicy.validate(profiles.map { it.profile })
        profiles.forEach {
            require(it.profile.password.isEmpty() && it.profile.tokenString.isEmpty()) { "Plaintext credentials in metadata" }
            require(it.password.length <= 16384 && it.token.length <= 16384) { "Encrypted credential is too large" }
            require(it.profile.savePassword || it.password.isEmpty())
            require(it.profile.softwareTokenMode != 0 || it.token.isEmpty())
        }
    }
}
