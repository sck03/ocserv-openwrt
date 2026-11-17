package dev.opentunnel.vpn.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.core.stringSetPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import dev.opentunnel.vpn.ui.theme.ThemeMode
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "opentunnel")

/**
 * Single access point for app settings and multi-profile management.
 * Profile data is delegated to [ProfileStore]; settings live in a separate DataStore.
 */
class Repository(context: Context) {

    private val store = context.applicationContext.dataStore
    private val profileStore = ProfileStore.get(context)

    private object Keys {
        val themeMode = stringPreferencesKey("settings.themeMode")
        val appLanguage = stringPreferencesKey("settings.appLanguage")
        val dynamicColor = booleanPreferencesKey("settings.dynamicColor")
        val splitEnabled = booleanPreferencesKey("settings.split.enabled")
        val splitMode = stringPreferencesKey("settings.split.mode")
        val splitPackages = stringSetPreferencesKey("settings.split.packages")
        val splitNetworksEnabled = booleanPreferencesKey("settings.splitNetworks.enabled")
        val splitNetworksMode = stringPreferencesKey("settings.splitNetworks.mode")
        val splitNetworks = stringSetPreferencesKey("settings.splitNetworks.networks")
        val bypassLocal = booleanPreferencesKey("settings.bypassLocalNetworks")
        val connectOnBoot = booleanPreferencesKey("settings.connectOnBoot")
        val reconnectOnNetwork = booleanPreferencesKey("settings.reconnectOnNetworkChange")
        val statsInNotification = booleanPreferencesKey("settings.statsInNotification")
        val verboseLogging = booleanPreferencesKey("settings.verboseLogging")
        val customDns = stringPreferencesKey("settings.customDns")
        val enableGeoIpLookup = booleanPreferencesKey("settings.enableGeoIpLookup")
        val hapticFeedbackEnabled = booleanPreferencesKey("settings.hapticFeedbackEnabled")
        val activeProfileId = stringPreferencesKey("settings.activeProfileId")
    }

    private val settingsError = MutableStateFlow<String?>(null)
    // Display-only fallback. Connection APIs below always read the strict DataStore.
    private val prefs: Flow<Preferences> = store.data.map { it.also { settingsError.value = null } }.catch { error ->
        if (error is CancellationException) throw error
        settingsError.value = "Settings storage is unavailable; connection is blocked."
        emit(emptyPreferences())
    }

    // ── multi-profile ───────────────────────────────────────────────────────

    val storageError = combine(profileStore.storageError, settingsError) { profile, settings -> profile ?: settings }
    val profiles: Flow<List<VpnProfile>> = profileStore.profiles

    val activeProfile: Flow<VpnProfile> = combine(profileStore.profiles, prefs) { list, p ->
        val id = p[Keys.activeProfileId].orEmpty()
        list.firstOrNull { it.id == id } ?: list.firstOrNull() ?: VpnProfile()
    }

    suspend fun currentProfile(): VpnProfile =
        profileStore.connectionProfile(store.data.first()[Keys.activeProfileId].orEmpty())

    suspend fun connectionSnapshot(): Pair<VpnProfile, AppSettings> {
        val settings = decodeSettings(store.data.first())
        return profileStore.connectionProfile(settings.activeProfileId) to settings
    }

    suspend fun getProfileById(id: String): VpnProfile? =
        profileStore.allProfiles().firstOrNull { it.id == id }

    /** Upsert and return the saved copy (which has an auto-assigned id if new). */
    suspend fun saveProfile(profile: VpnProfile): VpnProfile = profileStore.save(profile)

    suspend fun reorderProfiles(ids: List<String>) = profileStore.reorder(ids)

    suspend fun deleteProfile(profileId: String) = profileStore.delete(profileId)

    suspend fun setActiveProfile(profileId: String) {
        store.edit { it[Keys.activeProfileId] = profileId }
    }

    suspend fun exportProfilesJson(): String = profileStore.exportJson()

    suspend fun importProfilesJson(jsonStr: String): Int = profileStore.importJson(jsonStr)

    suspend fun pinCertificate(profileID: String, server: String, fingerprint: String) =
        profileStore.pinCertificate(profileID, server, fingerprint)

    suspend fun pinCertificate(fingerprint: String) {
        val id = store.data.first()[Keys.activeProfileId].orEmpty()
        val profiles = profileStore.allProfiles()
        val active = (if (id.isEmpty()) profiles.firstOrNull() else profiles.firstOrNull { it.id == id }) ?: return
        if (active.id.isNotBlank()) {
            profileStore.pinCertificate(active.id, active.server, fingerprint)
        }
    }

    // ── settings ─────────────────────────────────────────────────────────────

    val settings: Flow<AppSettings> = prefs.map(::decodeSettings).catch { error ->
        if (error is CancellationException) throw error
        settingsError.value = "Settings are invalid; connection is blocked."
        emit(AppSettings())
    }

    private fun decodeSettings(p: Preferences): AppSettings {
        val defaults = AppSettings()
        return AppSettings(
            themeMode = p[Keys.themeMode]?.let { runCatching { ThemeMode.valueOf(it) }.getOrNull() }
                ?: defaults.themeMode,
            appLanguage = p[Keys.appLanguage]?.let { runCatching { AppLanguage.valueOf(it) }.getOrNull() }
                ?: defaults.appLanguage,
            dynamicColor = p[Keys.dynamicColor] ?: defaults.dynamicColor,
            splitTunnelEnabled = p[Keys.splitEnabled] ?: defaults.splitTunnelEnabled,
            splitTunnelMode = p[Keys.splitMode]
                ?.let { SplitTunnelMode.valueOf(it) }
                ?: defaults.splitTunnelMode,
            selectedPackages = p[Keys.splitPackages] ?: defaults.selectedPackages,
            splitTunnelNetworksEnabled = p[Keys.splitNetworksEnabled] ?: defaults.splitTunnelNetworksEnabled,
            splitTunnelNetworksMode = p[Keys.splitNetworksMode]
                ?.let { SplitTunnelMode.valueOf(it) }
                ?: defaults.splitTunnelNetworksMode,
            splitTunnelNetworks = p[Keys.splitNetworks] ?: defaults.splitTunnelNetworks,
            bypassLocalNetworks = p[Keys.bypassLocal] ?: defaults.bypassLocalNetworks,
            connectOnBoot = p[Keys.connectOnBoot] ?: defaults.connectOnBoot,
            reconnectOnNetworkChange = p[Keys.reconnectOnNetwork] ?: defaults.reconnectOnNetworkChange,
            showStatsInNotification = p[Keys.statsInNotification] ?: defaults.showStatsInNotification,
            verboseLogging = p[Keys.verboseLogging] ?: defaults.verboseLogging,
            customDns = p[Keys.customDns] ?: defaults.customDns,
            enableGeoIpLookup = p[Keys.enableGeoIpLookup] ?: defaults.enableGeoIpLookup,
            hapticFeedbackEnabled = p[Keys.hapticFeedbackEnabled] ?: defaults.hapticFeedbackEnabled,
            activeProfileId = p[Keys.activeProfileId].orEmpty(),
        )
    }

    suspend fun currentSettings(): AppSettings = decodeSettings(store.data.first())

    suspend fun setThemeMode(mode: ThemeMode) = store.edit { it[Keys.themeMode] = mode.name }
    suspend fun setAppLanguage(language: AppLanguage) = store.edit { it[Keys.appLanguage] = language.name }
    suspend fun setDynamicColor(enabled: Boolean) = store.edit { it[Keys.dynamicColor] = enabled }
    suspend fun setSplitTunnelEnabled(enabled: Boolean) = store.edit { it[Keys.splitEnabled] = enabled }
    suspend fun setSplitTunnelMode(mode: SplitTunnelMode) = store.edit { it[Keys.splitMode] = mode.name }
    suspend fun setSplitTunnelNetworksEnabled(enabled: Boolean) = store.edit { it[Keys.splitNetworksEnabled] = enabled }
    suspend fun setSplitTunnelNetworksMode(mode: SplitTunnelMode) = store.edit { it[Keys.splitNetworksMode] = mode.name }
    suspend fun setBypassLocalNetworks(enabled: Boolean) = store.edit { it[Keys.bypassLocal] = enabled }
    suspend fun setConnectOnBoot(enabled: Boolean) = store.edit { it[Keys.connectOnBoot] = enabled }
    suspend fun setReconnectOnNetworkChange(enabled: Boolean) =
        store.edit { it[Keys.reconnectOnNetwork] = enabled }
    suspend fun setShowStatsInNotification(enabled: Boolean) =
        store.edit { it[Keys.statsInNotification] = enabled }
    suspend fun setVerboseLogging(enabled: Boolean) = store.edit { it[Keys.verboseLogging] = enabled }
    suspend fun setCustomDns(dns: String) = store.edit { it[Keys.customDns] = dns }
    suspend fun setEnableGeoIpLookup(enabled: Boolean) = store.edit { it[Keys.enableGeoIpLookup] = enabled }
    suspend fun setHapticFeedbackEnabled(enabled: Boolean) =
        store.edit { it[Keys.hapticFeedbackEnabled] = enabled }

    suspend fun setPackageSelected(packageName: String, selected: Boolean) {
        store.edit { p ->
            val current = p[Keys.splitPackages] ?: emptySet()
            p[Keys.splitPackages] =
                if (selected) current + packageName else current - packageName
        }
    }

    suspend fun setSelectedPackages(packages: Set<String>) {
        store.edit { it[Keys.splitPackages] = packages }
    }

    suspend fun addSplitTunnelNetwork(network: String) {
        val trimmed = network.trim()
        if (trimmed.isBlank()) return
        store.edit { p ->
            val current = p[Keys.splitNetworks] ?: emptySet()
            p[Keys.splitNetworks] = current + trimmed
        }
    }

    suspend fun removeSplitTunnelNetwork(network: String) {
        store.edit { p ->
            val current = p[Keys.splitNetworks] ?: emptySet()
            p[Keys.splitNetworks] = current - network
        }
    }

    suspend fun clearSplitTunnelNetworks() {
        store.edit { it[Keys.splitNetworks] = emptySet() }
    }

    companion object {
        @Volatile
        private var instance: Repository? = null

        fun get(context: Context): Repository =
            instance ?: synchronized(this) {
                instance ?: Repository(context.applicationContext).also { instance = it }
            }
    }
}
