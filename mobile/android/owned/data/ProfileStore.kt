// Adapted from the checksum-pinned OpenTunnel source; see THIRD-PARTY-NOTICES.md.
package dev.opentunnel.vpn.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import dev.opentunnel.vpn.util.SecretBox
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.util.UUID

private val Context.profileDataStore: DataStore<Preferences> by
    preferencesDataStore(name = "opentunnel_profiles")

/**
 * Persists the ordered list of [VpnProfile] objects as a JSON array.
 * Passwords are individually encrypted with [SecretBox] before storage.
 */
class ProfileStore(context: Context) {

    private val store = context.applicationContext.profileDataStore

    private object Keys {
        val profilesJson = stringPreferencesKey("profiles.json")
    }

    private val _storageError = kotlinx.coroutines.flow.MutableStateFlow<String?>(null)
    val storageError: kotlinx.coroutines.flow.StateFlow<String?> = _storageError
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }


    val profiles: Flow<List<VpnProfile>> = store.data
        .map { prefs ->
            try {
                decode(prefs[Keys.profilesJson].orEmpty()).also { _storageError.value = null }
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                _storageError.value = "Profiles could not be read; the original data was preserved."
                emptyList()
            }
        }
        .catch { error ->
            if (error is CancellationException) throw error
            _storageError.value = "Profile storage is unavailable; the original data was preserved."
            emit(emptyList())
        }

    suspend fun allProfiles(): List<VpnProfile> = decode(store.data.first()[Keys.profilesJson].orEmpty())

    /** Upsert a profile. If [profile.id] is blank a new UUID is assigned. */
    suspend fun save(profile: VpnProfile): VpnProfile {
        val toSave = if (profile.id.isBlank()) profile.copy(id = UUID.randomUUID().toString()) else profile
        store.edit { prefs ->
            val current = decode(prefs[Keys.profilesJson].orEmpty()).toMutableList()
            val idx = current.indexOfFirst { it.id == toSave.id }
            if (idx >= 0) current[idx] = ProfilePolicy.updated(current[idx], toSave) else current.add(toSave)
            prefs[Keys.profilesJson] = encode(current)
        }
        return toSave
    }

    suspend fun saveAll(profiles: List<VpnProfile>) {
        store.edit { prefs ->
            prefs[Keys.profilesJson] = encode(profiles)
        }
    }

    suspend fun exportJson(): String {
        return json.encodeToString(allProfiles().map { it.copy(password = "", tokenString = "") })
    }

    suspend fun pinCertificate(id: String, server: String, fingerprint: String) {
        store.edit { prefs ->
            val current = decode(prefs[Keys.profilesJson].orEmpty())
            val updated = ProfilePolicy.pin(current, id, server, fingerprint)
            if (updated != current) prefs[Keys.profilesJson] = encode(updated)
        }
    }

    suspend fun importJson(jsonStr: String): Int {
        require(jsonStr.toByteArray(Charsets.UTF_8).size <= ProfilePolicy.MAX_BYTES) { "Profile import is too large" }
        return run {
            val imported = json.decodeFromString<List<VpnProfile>>(jsonStr)
            if (imported.isEmpty()) return 0
            var count = 0
            store.edit { prefs ->
                val current = decode(prefs[Keys.profilesJson].orEmpty()).toMutableList()
                imported.forEach { item ->
                    val p = if (item.id.isBlank()) item.copy(id = UUID.randomUUID().toString()) else item
                    val idx = current.indexOfFirst { it.id == p.id }
                    if (idx >= 0) current[idx] = ProfilePolicy.updated(current[idx], p) else current.add(p)
                    count++
                }
                prefs[Keys.profilesJson] = encode(current)
            }
            count
        }
    }

    suspend fun delete(profileId: String) {
        store.edit { prefs ->
            val current = decode(prefs[Keys.profilesJson].orEmpty())
            prefs[Keys.profilesJson] = encode(current.filter { it.id != profileId })
        }
    }


    private val codec = ProfileCodec(SecretBox::encrypt, SecretBox::decrypt)
    private fun encode(profiles: List<VpnProfile>) = codec.encode(profiles)
    private fun decode(raw: String) = codec.decode(raw)

    companion object {
        @Volatile private var instance: ProfileStore? = null
        fun get(context: Context): ProfileStore =
            instance ?: synchronized(this) {
                instance ?: ProfileStore(context.applicationContext).also { instance = it }
            }
    }
}
