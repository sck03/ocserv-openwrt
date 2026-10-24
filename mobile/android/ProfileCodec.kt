package dev.opentunnel.vpn.data

import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

/** Storage format and crypto failure handling are independent of Android/DataStore. */
internal class ProfileCodec(private val encrypt: (String) -> String, private val decrypt: (String) -> String) {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    fun encode(profiles: List<VpnProfile>): String {
        ProfilePolicy.validate(profiles)
        fun protected(value: String): String = if (value.isEmpty()) "" else encrypt(value).also {
            check(it.isNotEmpty()) { "Credential encryption failed; original data was preserved" }
        }
        val encrypted = profiles.map { it.copy(
            password = if (it.savePassword) protected(it.password) else "",
            tokenString = protected(it.tokenString)
        ) }
        return json.encodeToString(encrypted).also {
            require(it.toByteArray(Charsets.UTF_8).size <= ProfilePolicy.MAX_BYTES) { "Profiles exceed the storage limit; original data was preserved" }
        }
    }

    fun decode(raw: String): List<VpnProfile> {
        if (raw.isBlank()) return emptyList()
        require(raw.toByteArray(Charsets.UTF_8).size <= ProfilePolicy.MAX_BYTES) { "Profiles exceed the storage limit" }
        fun unprotected(value: String): String = if (value.isEmpty()) "" else decrypt(value).also {
            check(it.isNotEmpty()) { "Credential decryption failed; original data was preserved" }
        }
        return try {
            json.decodeFromString<List<VpnProfile>>(raw).map {
                it.copy(password = unprotected(it.password), tokenString = unprotected(it.tokenString))
            }.also { ProfilePolicy.validate(it) }
        } catch (error: Exception) {
            throw IllegalStateException("Profiles could not be decoded; original data was preserved", error)
        }
    }
}
