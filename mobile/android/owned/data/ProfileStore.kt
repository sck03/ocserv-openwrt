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
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import java.util.UUID
import java.io.File

private val Context.profileDataStore: DataStore<Preferences> by preferencesDataStore(name = "lansyra_profiles_v4")

/** Each mutation reads the latest metadata and preserves all unchanged ciphertext. */
class ProfileStore private constructor(context: Context) {
    private val store = context.applicationContext.profileDataStore
    private val key = stringPreferencesKey("profiles.json")
    private val selectedKey = stringPreferencesKey("profiles.selected")
    private val codec = ProfileCodec(SecretBox::encrypt, SecretBox::decrypt)
    private val operations = Mutex()
    private val certificates = CertificateFiles(File(context.filesDir, "certificates")) { source ->
        checkNotNull(context.applicationContext.contentResolver.openInputStream(Uri.parse(source))) {
            "Certificate provider is unavailable"
        }
    }
    private val observations = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    // One upstream subscription and one decode per changed JSON document.
    // Selection changes reuse metadata and never unlock any credential.
    private val metadata = flow {
        val projection = ProfileProjection { raw -> codec.decode(raw).map { it.profile } }
        store.data.collect { prefs ->
            emit(projection.snapshot(prefs[key].orEmpty(), prefs[selectedKey].orEmpty()))
        }
    }.catch { error ->
        if (error is CancellationException) throw error
        emit(ProfileMetadata(error = "Profile storage is unavailable; connection is blocked."))
    }.stateIn(observations, SharingStarted.WhileSubscribed(5000), ProfileMetadata())
    val storageError = metadata.map { it.error }.distinctUntilChanged()
    val profiles: Flow<List<VpnProfile>> = metadata.map { it.profiles }.distinctUntilChanged()
    val activeProfile: Flow<VpnProfile> = metadata.map { it.selected }.distinctUntilChanged()

    suspend fun select(id: String) = operation {
        store.edit { prefs ->
            val profiles = codec.decode(prefs[key].orEmpty()).map { it.profile }
            require(profiles.any { it.id == id }) { "Selected profile is unavailable" }
            prefs[selectedKey] = id
        }
    }

    suspend fun connectionProfile(): VpnProfile {
        val prefs = store.data.first()
        val current = codec.decode(prefs[key].orEmpty())
        val id = ProfilePolicy.selected(current.map { it.profile }, prefs[selectedKey].orEmpty())?.id
        val selected = current.firstOrNull { it.profile.id == id }
        return codec.unlock(checkNotNull(selected) { "Selected profile is unavailable" })
    }

    suspend fun openConnection(): Pair<VpnProfile, CertificateFiles.Lease> {
        var retained: CertificateFiles.Lease? = null
        try {
            return withContext(Dispatchers.IO) {
                operations.withLock {
                    val prefs = store.data.first()
                    val current = codec.decode(prefs[key].orEmpty())
                    val id = ProfilePolicy.selected(current.map { it.profile }, prefs[selectedKey].orEmpty())?.id
                    val selected = current.firstOrNull { it.profile.id == id }
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

    suspend fun save(edit: ProfileEdit): VpnProfile = operation {
        val profile = edit.profile
        val candidate = if (profile.id.isBlank()) profile.copy(id = UUID.randomUUID().toString()) else profile
        var saved = candidate
        var staged: CertificateFiles.Lease? = null
        try {
            mutateLocked { current ->
                val prepared = certificates.stage(candidate).also { staged = it }.profile
                val index = current.indexOfFirst { it.profile.id == prepared.id }
                val updated = codec.update(prepared, current.getOrNull(index), edit.fields)
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

    suspend fun exportConnection(): String = operation {
        val prefs = store.data.first()
        val profiles = codec.decode(prefs[key].orEmpty()).map { it.profile }
        val profile = checkNotNull(ProfilePolicy.selected(profiles, prefs[selectedKey].orEmpty())) { "Select a profile" }
        certificates.reconcile(profiles)
        certificates.retain(profile).use {
            val ca = if (profile.caCertPath.isEmpty()) null else File(profile.caCertPath).inputStream().use { input -> ConnectionFile.read(input, 65536) }
            ConnectionFile(profile, ca).encode()
        }
    }

    suspend fun pinCertificate(id: String, server: String, fingerprint: String) = mutate { current ->
        val updated = ProfilePolicy.pin(current.map { it.profile }, id, server, fingerprint)
        current.zip(updated) { stored, profile -> stored.copy(profile = profile) }
    }

    suspend fun reserveHotp(id: String, server: String, expected: String, next: String) = mutate { current ->
        require(next == dev.opentunnel.vpn.core.HotpCounter.next(expected)) { "Invalid HOTP reservation" }
        val index = current.indexOfFirst { it.profile.id == id }
        val stored = checkNotNull(current.getOrNull(index)) { "Profile was removed" }
        require(stored.profile.softwareTokenMode == 3 && ProfilePolicy.origin(stored.profile.server) == ProfilePolicy.origin(server)) {
            "HOTP profile changed"
        }
        require(codec.unlock(stored).tokenString == expected) { "HOTP token changed" }
        current.toMutableList().apply { set(index, codec.update(stored.profile.copy(tokenString = next), stored)) }
    }

    suspend fun importConnection(raw: String): VpnProfile = operation {
        val imported = ConnectionFile.parse(raw)
        val candidate = imported.profile.copy(id = UUID.randomUUID().toString())
        var staged: CertificateFiles.Lease? = null
        var saved = candidate
        try {
            mutateLocked(candidate.id) { current ->
                val prepared = certificates.stage(candidate, imported.ca).also { staged = it }.profile
                val entry = codec.update(prepared, null)
                saved = entry.profile
                current + entry
            }
        } finally { staged?.close() }
        saved
    }

    suspend fun delete(id: String) = mutate { it.filter { stored -> stored.profile.id != id } }

    private suspend fun <T> operation(action: suspend () -> T): T = withContext(Dispatchers.IO) {
        operations.withLock {
            // Once a commit starts, finish the file/reference transaction even if the editor closes.
            withContext(NonCancellable) { action() }
        }
    }

    private suspend fun mutate(transform: (List<StoredProfile>) -> List<StoredProfile>) =
        operation { mutateLocked(transform = transform) }

    private suspend fun mutateLocked(select: String? = null, transform: (List<StoredProfile>) -> List<StoredProfile>) {
        var committed = emptyList<VpnProfile>()
        store.edit { prefs ->
            val current = codec.decode(prefs[key].orEmpty())
            certificates.reconcile(current.map { it.profile })
            val updated = transform(current)
            prefs[key] = codec.encode(updated)
            committed = updated.map { it.profile }
            if (select != null) {
                require(committed.any { it.id == select }) { "Selected profile is unavailable" }
                prefs[selectedKey] = select
            }
            if (committed.none { it.id == prefs[selectedKey] })
                prefs[selectedKey] = committed.firstOrNull()?.id.orEmpty()
        }
        certificates.reconcile(committed)
    }

    companion object {
        @Volatile private var instance: ProfileStore? = null
        fun get(context: Context): ProfileStore = instance ?: synchronized(this) {
            instance ?: ProfileStore(context.applicationContext).also { instance = it }
        }
    }
}
