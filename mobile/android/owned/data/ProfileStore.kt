// Adapted from the checksum-pinned OpenTunnel source; see THIRD-PARTY-NOTICES.md.
package dev.opentunnel.vpn.data

import android.content.Context
import android.net.Uri
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import dev.opentunnel.vpn.util.SecretBox
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.flowOn
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.util.UUID
import java.io.File

private val Context.profileDataStore: DataStore<Preferences> by preferencesDataStore(name = "opentunnel_profiles")

/** Each mutation reads the latest metadata and preserves all unchanged ciphertext. */
class ProfileStore private constructor(context: Context) {
    private val store = context.applicationContext.profileDataStore
    private val key = stringPreferencesKey("profiles.json")
    private val codec = ProfileCodec(SecretBox::encrypt, SecretBox::decrypt)
    private val operations = Mutex()
    private val certificates = CertificateFiles(File(context.filesDir, "certificates")) { source ->
        checkNotNull(context.applicationContext.contentResolver.openInputStream(Uri.parse(source))) {
            "Certificate provider is unavailable"
        }
    }
    private val json = Json { encodeDefaults = true }
    private val _storageError = kotlinx.coroutines.flow.MutableStateFlow<String?>(null)
    val storageError: kotlinx.coroutines.flow.StateFlow<String?> = _storageError

    val profiles: Flow<List<VpnProfile>> = store.data.map { prefs ->
        codec.decode(prefs[key].orEmpty()).map { it.profile }.also { _storageError.value = null }
    }.catch { error ->
        if (error is CancellationException) throw error
        _storageError.value = "Profile storage is unavailable or invalid; connection is blocked."
        emit(emptyList())
    }.flowOn(Dispatchers.IO)

    suspend fun allProfiles(): List<VpnProfile> = codec.decode(store.data.first()[key].orEmpty()).map { it.profile }

    suspend fun connectionProfile(id: String): VpnProfile {
        val current = codec.decode(store.data.first()[key].orEmpty())
        val selected = if (id.isEmpty()) current.firstOrNull() else current.firstOrNull { it.profile.id == id }
        return codec.unlock(checkNotNull(selected) { "Selected profile is unavailable" })
    }

    suspend fun openConnection(id: String): Pair<VpnProfile, CertificateFiles.Lease> {
        var retained: CertificateFiles.Lease? = null
        try {
            return withContext(Dispatchers.IO) {
                operations.withLock {
                    val current = codec.decode(store.data.first()[key].orEmpty())
                    val selected = if (id.isEmpty()) current.firstOrNull() else current.firstOrNull { it.profile.id == id }
                    val profile = codec.unlock(checkNotNull(selected) { "Selected profile is unavailable" })
                    certificates.reconcile(current.map { it.profile })
                    profile to certificates.retain(profile.copy(password = "", tokenString = "")).also { retained = it }
                }
            }
        } catch (error: Throwable) {
            // withContext may be cancelled while transferring a newly acquired lease.
            retained?.close()
            throw error
        }
    }

    suspend fun save(profile: VpnProfile): VpnProfile = operation {
        val candidate = if (profile.id.isBlank()) profile.copy(id = UUID.randomUUID().toString()) else profile
        var saved = candidate
        var staged: CertificateFiles.Lease? = null
        try {
            mutateLocked { current ->
                val prepared = certificates.stage(candidate).also { staged = it }.profile
                val index = current.indexOfFirst { it.profile.id == prepared.id }
                val updated = codec.update(prepared, current.getOrNull(index))
                saved = updated.profile
                current.toMutableList().apply { if (index < 0) add(updated) else set(index, updated) }
            }
        } finally {
            staged?.close()
        }
        saved
    }

    suspend fun reorder(ids: List<String>) = mutate { current ->
        ProfilePolicy.reorder(current, ids) { it.profile.id }
    }

    suspend fun exportJson(): String = json.encodeToString(allProfiles().map(ProfilePolicy::portable))

    suspend fun pinCertificate(id: String, server: String, fingerprint: String) = mutate { current ->
        val updated = ProfilePolicy.pin(current.map { it.profile }, id, server, fingerprint)
        current.zip(updated) { stored, profile -> stored.copy(profile = profile) }
    }

    suspend fun importJson(raw: String): Int {
        require(raw.toByteArray(Charsets.UTF_8).size <= ProfilePolicy.MAX_BYTES) { "Profile import is too large" }
        val imported = json.decodeFromString<List<VpnProfile>>(raw).map {
            ProfilePolicy.portable(if (it.id.isBlank()) it.copy(id = UUID.randomUUID().toString()) else it)
        }
        ProfilePolicy.validate(imported)
        mutate { current ->
            val merged = current.associateBy { it.profile.id }.toMutableMap()
            imported.forEach { merged[it.id] = codec.update(it, merged[it.id]) }
            merged.values.toList()
        }
        return imported.size
    }

    suspend fun delete(id: String) = mutate { it.filter { stored -> stored.profile.id != id } }

    private suspend fun <T> operation(action: suspend () -> T): T = withContext(Dispatchers.IO) {
        operations.withLock {
            // Once a commit starts, finish the file/reference transaction even if the editor closes.
            withContext(NonCancellable) { action() }
        }
    }

    private suspend fun mutate(transform: (List<StoredProfile>) -> List<StoredProfile>) =
        operation { mutateLocked(transform) }

    private suspend fun mutateLocked(transform: (List<StoredProfile>) -> List<StoredProfile>) {
        var committed = emptyList<VpnProfile>()
        store.edit { prefs ->
            val current = codec.decode(prefs[key].orEmpty())
            certificates.reconcile(current.map { it.profile })
            val updated = transform(current)
            prefs[key] = codec.encode(updated)
            committed = updated.map { it.profile }
        }
        certificates.reconcile(committed)
        _storageError.value = null
    }

    companion object {
        @Volatile private var instance: ProfileStore? = null
        fun get(context: Context): ProfileStore = instance ?: synchronized(this) {
            instance ?: ProfileStore(context.applicationContext).also { instance = it }
        }
    }
}
