package dev.opentunnel.vpn.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dev.opentunnel.vpn.core.Interaction
import dev.opentunnel.vpn.core.PromptResult
import dev.opentunnel.vpn.core.VpnBus
import dev.opentunnel.vpn.data.AppSettings
import dev.opentunnel.vpn.data.InstalledApp
import dev.opentunnel.vpn.data.InstalledApps
import dev.opentunnel.vpn.data.Repository
import dev.opentunnel.vpn.data.SplitTunnelMode
import dev.opentunnel.vpn.data.VpnProfile
import dev.opentunnel.vpn.ui.theme.ThemeMode
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

class AppViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = Repository.get(application)

    val status = VpnBus.status
    val stats = VpnBus.stats
    val logs = VpnBus.logs
    fun setStatisticsVisible(owner: Any, visible: Boolean) = VpnBus.setStatisticsVisible(owner, visible)
    val pendingPrompt = Interaction.pending

    val settings: StateFlow<AppSettings> = repository.settings
        .stateIn(viewModelScope, SharingStarted.Eagerly, AppSettings())

    /** The currently active (selected) profile. */
    val profile: StateFlow<VpnProfile> = repository.activeProfile
        .stateIn(viewModelScope, SharingStarted.Eagerly, VpnProfile())

    /** All saved profiles, for the profile picker menu. */
    val profiles: StateFlow<List<VpnProfile>> = repository.profiles
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())

    private val _installedApps = MutableStateFlow<List<InstalledApp>?>(null)
    val installedApps: StateFlow<List<InstalledApp>?> = _installedApps.asStateFlow()
    private var appsJob: Job? = null
    private val trafficVisible = MutableStateFlow(false)

    private val historyBuffer = dev.opentunnel.vpn.core.SpeedHistory()
    private val _speedHistory = MutableStateFlow(dev.opentunnel.vpn.core.SpeedSnapshot())
    val speedHistory = _speedHistory.asStateFlow()

    private var lastStage: dev.opentunnel.vpn.core.ConnectionStage? = null
    private var lastConnectedAt = 0L

    // ── app traffic monitor properties ───────────────────────────────────────

    private val trafficCollector = dev.opentunnel.vpn.core.AppTrafficCollector(
        statsProvider = dev.opentunnel.vpn.core.AndroidTrafficStatsProvider(application),
        dispatcher = Dispatchers.IO,
        sampleIntervalMs = 5000L,
    )

    val appTrafficSummary: StateFlow<dev.opentunnel.vpn.data.AppTrafficSummary> = trafficCollector.summary
    val appTrafficPaused = trafficCollector.paused.stateIn(viewModelScope, SharingStarted.Eagerly, false)

    private val _trafficSortBy = MutableStateFlow(dev.opentunnel.vpn.data.TrafficSortBy.TOTAL_TRAFFIC)
    val trafficSortBy: StateFlow<dev.opentunnel.vpn.data.TrafficSortBy> = _trafficSortBy.asStateFlow()

    private val _trafficSortDirection = MutableStateFlow(dev.opentunnel.vpn.data.SortDirection.DESCENDING)
    val trafficSortDirection: StateFlow<dev.opentunnel.vpn.data.SortDirection> = _trafficSortDirection.asStateFlow()

    private val _trafficFilterMode = MutableStateFlow(dev.opentunnel.vpn.data.TrafficFilterMode.ALL)
    val trafficFilterMode: StateFlow<dev.opentunnel.vpn.data.TrafficFilterMode> = _trafficFilterMode.asStateFlow()

    private val _trafficSearchQuery = MutableStateFlow("")
    val trafficSearchQuery: StateFlow<String> = _trafficSearchQuery.asStateFlow()

    private val _hasUsageAccessPermission = MutableStateFlow(
        dev.opentunnel.vpn.core.hasUsageStatsPermission(application),
    )
    val hasUsageAccessPermission: StateFlow<Boolean> = _hasUsageAccessPermission.asStateFlow()

    val appTrafficEntries: StateFlow<List<dev.opentunnel.vpn.data.AppTrafficEntry>> = kotlinx.coroutines.flow.combine(
        trafficCollector.entries,
        _trafficSortBy,
        _trafficSortDirection,
        _trafficFilterMode,
        _trafficSearchQuery,
    ) { rawEntries, sortBy, sortDir, filterMode, query ->
        val needle = query.trim().lowercase()
        val filtered = rawEntries.filter { entry ->
            !entry.isSystem && (
                (needle.isEmpty() || entry.label.lowercase().contains(needle) || entry.packageName.lowercase().contains(needle)) &&
                (when (filterMode) {
                    dev.opentunnel.vpn.data.TrafficFilterMode.ALL -> true
                    dev.opentunnel.vpn.data.TrafficFilterMode.ACTIVE_ONLY -> entry.isActive || entry.totalBytes > 0L
                })
            )
        }

        val collator = java.text.Collator.getInstance()
        val comparator: Comparator<dev.opentunnel.vpn.data.AppTrafficEntry> = when (sortBy) {
            dev.opentunnel.vpn.data.TrafficSortBy.TOTAL_TRAFFIC ->
                if (sortDir == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
                    compareByDescending<dev.opentunnel.vpn.data.AppTrafficEntry> { it.totalBytes }
                        .thenBy(collator) { it.label }
                } else {
                    compareBy<dev.opentunnel.vpn.data.AppTrafficEntry> { it.totalBytes }
                        .thenBy(collator) { it.label }
                }

            dev.opentunnel.vpn.data.TrafficSortBy.DOWNLOAD ->
                if (sortDir == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
                    compareByDescending<dev.opentunnel.vpn.data.AppTrafficEntry> { it.rxBytes }
                        .thenBy(collator) { it.label }
                } else {
                    compareBy<dev.opentunnel.vpn.data.AppTrafficEntry> { it.rxBytes }
                        .thenBy(collator) { it.label }
                }

            dev.opentunnel.vpn.data.TrafficSortBy.UPLOAD ->
                if (sortDir == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
                    compareByDescending<dev.opentunnel.vpn.data.AppTrafficEntry> { it.txBytes }
                        .thenBy(collator) { it.label }
                } else {
                    compareBy<dev.opentunnel.vpn.data.AppTrafficEntry> { it.txBytes }
                        .thenBy(collator) { it.label }
                }

            dev.opentunnel.vpn.data.TrafficSortBy.DOWNLOAD_SPEED ->
                if (sortDir == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
                    compareByDescending<dev.opentunnel.vpn.data.AppTrafficEntry> { it.rxRate }
                        .thenByDescending { it.totalBytes }
                        .thenBy(collator) { it.label }
                } else {
                    compareBy<dev.opentunnel.vpn.data.AppTrafficEntry> { it.rxRate }
                        .thenBy { it.totalBytes }
                        .thenBy(collator) { it.label }
                }

            dev.opentunnel.vpn.data.TrafficSortBy.UPLOAD_SPEED ->
                if (sortDir == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
                    compareByDescending<dev.opentunnel.vpn.data.AppTrafficEntry> { it.txRate }
                        .thenByDescending { it.totalBytes }
                        .thenBy(collator) { it.label }
                } else {
                    compareBy<dev.opentunnel.vpn.data.AppTrafficEntry> { it.txRate }
                        .thenBy { it.totalBytes }
                        .thenBy(collator) { it.label }
                }

            dev.opentunnel.vpn.data.TrafficSortBy.APP_NAME ->
                if (sortDir == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
                    compareByDescending(collator) { it.label }
                } else {
                    compareBy(collator) { it.label }
                }
        }

        filtered.sortedWith(comparator)
    }.flowOn(Dispatchers.Default).stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    fun clearSpeedHistory() {
        historyBuffer.clear(status.value.connectedAtElapsed)
        _speedHistory.value = dev.opentunnel.vpn.core.SpeedSnapshot()
    }

    init {
        viewModelScope.launch {
            repository.storageError.collect { error -> if (error != null) reportStorageError(error) }
        }

        viewModelScope.launch {
            trafficVisible.collectLatest { visible ->
                if (visible) {
                    loadInstalledApps()
                    try {
                        installedApps.filterNotNull().collectLatest { list ->
                            trafficCollector.loadApps(list.map { app ->
                                dev.opentunnel.vpn.core.AppMetadata(app.packageName, app.uid, app.label, app.isSystem)
                            })
                            trafficCollector.start()
                        }
                    } catch (error: kotlinx.coroutines.CancellationException) { throw error }
                    catch (error: Exception) { VpnBus.error("Application traffic counters are unavailable.") }
                    finally {
                        trafficCollector.stop()
                    }
                }
            }
        }

        viewModelScope.launch {
            status.collect { s ->
                val prevStage = lastStage
                val prevConnectedAt = lastConnectedAt
                lastStage = s.stage
                lastConnectedAt = s.connectedAtElapsed

                val isFreshConnection = (prevStage == null || prevStage == dev.opentunnel.vpn.core.ConnectionStage.IDLE || prevStage == dev.opentunnel.vpn.core.ConnectionStage.ERROR) &&
                    (s.stage == dev.opentunnel.vpn.core.ConnectionStage.PREPARING || s.stage == dev.opentunnel.vpn.core.ConnectionStage.CONNECTING)
                val newConnectionSession = s.connectedAtElapsed != 0L && s.connectedAtElapsed != prevConnectedAt

                if (isFreshConnection || newConnectionSession) {
                    clearSpeedHistory()
                }
            }
        }

        viewModelScope.launch(Dispatchers.Default) {
            stats.collect { st ->
                val active = status.value
                if (active.stage == dev.opentunnel.vpn.core.ConnectionStage.CONNECTED) {
                    historyBuffer.sample(st.sampleTimestamp, st.rxRate, st.txRate, active.connectedAtElapsed)
                    if (_speedHistory.subscriptionCount.value > 0) _speedHistory.value = historyBuffer.snapshot()
                }
            }
        }

        viewModelScope.launch {
            settings.collect { s ->
                trafficCollector.updateSettings(s)
            }
        }

    }

    private val _editingProfileId = MutableStateFlow<String?>(null)
    val editingProfileId: StateFlow<String?> = _editingProfileId.asStateFlow()

    fun setEditingProfileId(id: String?) {
        _editingProfileId.value = id
    }

    fun getProfile(id: String?): VpnProfile {
        if (id == null || id == "new" || id.isBlank()) return VpnProfile()
        return profiles.value.firstOrNull { it.id == id } ?: VpnProfile()
    }

    private fun reportStorageError(message: String) {
        if (status.value.stage in listOf(dev.opentunnel.vpn.core.ConnectionStage.IDLE, dev.opentunnel.vpn.core.ConnectionStage.ERROR))
            VpnBus.setError(message)
        else VpnBus.error(message)
    }

    private fun profileOperation(action: suspend () -> Unit) {
        viewModelScope.launch {
            try { action() }
            catch (error: kotlinx.coroutines.CancellationException) { throw error }
            catch (error: Exception) { reportStorageError("Storage operation failed. Check the input, storage and device key; original data was preserved.") }
        }
    }

    suspend fun saveProfile(edit: dev.opentunnel.vpn.data.ProfileEdit): VpnProfile = repository.saveProfile(edit)

    suspend fun deleteEditingProfile(profileId: String) { repository.deleteProfile(profileId) }

    fun reorderProfiles(profiles: List<VpnProfile>) {
        profileOperation { repository.reorderProfiles(profiles.map { it.id }) }
    }

    fun selectProfile(profileId: String) {
        profileOperation { repository.setActiveProfile(profileId) }
    }

    suspend fun exportConnection(): String = repository.exportConnection()

    suspend fun importConnection(raw: String): VpnProfile = repository.importConnection(raw)

    suspend fun forgetPinnedCertificate(profileId: String, server: String) {
        repository.pinCertificate(profileId, server, "")
    }

    // ── settings ─────────────────────────────────────────────────────────────

    fun setThemeMode(mode: ThemeMode) {
        profileOperation { repository.setThemeMode(mode) }
    }

    fun setAppLanguage(language: dev.opentunnel.vpn.data.AppLanguage) {
        profileOperation { repository.setAppLanguage(language) }
    }

    fun setDynamicColor(enabled: Boolean) {
        profileOperation { repository.setDynamicColor(enabled) }
    }

    fun setSplitTunnelEnabled(enabled: Boolean) {
        profileOperation { repository.setSplitTunnelEnabled(enabled) }
    }

    fun setSplitTunnelMode(mode: SplitTunnelMode) {
        profileOperation { repository.setSplitTunnelMode(mode) }
    }

    fun setBypassLocalNetworks(enabled: Boolean) {
        profileOperation { repository.setBypassLocalNetworks(enabled) }
    }

    fun setConnectOnBoot(enabled: Boolean) {
        profileOperation { repository.setConnectOnBoot(enabled) }
    }

    fun setReconnectOnNetworkChange(enabled: Boolean) {
        profileOperation { repository.setReconnectOnNetworkChange(enabled) }
    }

    fun setShowStatsInNotification(enabled: Boolean) {
        profileOperation { repository.setShowStatsInNotification(enabled) }
    }

    fun setVerboseLogging(enabled: Boolean) {
        profileOperation { repository.setVerboseLogging(enabled) }
    }

    fun setHapticFeedbackEnabled(enabled: Boolean) {
        profileOperation { repository.setHapticFeedbackEnabled(enabled) }
    }

    // ── split tunnelling ──────────────────────────────────────────────────────

    fun loadInstalledApps() = loadApps(false)

    fun refreshInstalledApps() = loadApps(true)

    private fun loadApps(forceRefresh: Boolean) {
        if (appsJob?.isActive == true) return
        appsJob = viewModelScope.launch(Dispatchers.IO) {
            _installedApps.value = InstalledApps.load(getApplication(), forceRefresh)
        }
    }

    fun togglePackage(packageName: String, selected: Boolean) {
        profileOperation { repository.setPackageSelected(packageName, selected) }
    }

    fun clearSelectedPackages() {
        profileOperation { repository.setSelectedPackages(emptySet()) }
    }

    fun setSplitTunnelNetworksEnabled(enabled: Boolean) {
        profileOperation { repository.setSplitTunnelNetworksEnabled(enabled) }
    }

    fun setSplitTunnelNetworksMode(mode: SplitTunnelMode) {
        profileOperation { repository.setSplitTunnelNetworksMode(mode) }
    }

    fun addSplitTunnelNetwork(network: String) {
        profileOperation { repository.addSplitTunnelNetwork(network) }
    }

    fun removeSplitTunnelNetwork(network: String) {
        profileOperation { repository.removeSplitTunnelNetwork(network) }
    }

    fun clearSplitTunnelNetworks() {
        profileOperation { repository.clearSplitTunnelNetworks() }
    }

    // ── prompts ──────────────────────────────────────────────────────────────

    fun submitPrompt(values: Map<String, String>) {
        Interaction.submit(PromptResult.Values(values))
    }

    fun acceptPrompt() {
        Interaction.submit(PromptResult.Accept)
    }

    fun cancelPrompt() {
        Interaction.submit(PromptResult.Cancel)
    }

    // ── logs ─────────────────────────────────────────────────────────────────

    fun clearLogs() {
        VpnBus.clearLogs()
    }

    // ── app traffic monitor ──────────────────────────────────────────────────

    fun setTrafficVisible(visible: Boolean) { trafficVisible.value = visible }

    fun setTrafficSortBy(sortBy: dev.opentunnel.vpn.data.TrafficSortBy) {
        _trafficSortBy.value = sortBy
    }

    fun toggleTrafficSortDirection() {
        _trafficSortDirection.value = if (_trafficSortDirection.value == dev.opentunnel.vpn.data.SortDirection.DESCENDING) {
            dev.opentunnel.vpn.data.SortDirection.ASCENDING
        } else {
            dev.opentunnel.vpn.data.SortDirection.DESCENDING
        }
    }

    fun setTrafficSortDirection(direction: dev.opentunnel.vpn.data.SortDirection) {
        _trafficSortDirection.value = direction
    }

    fun setTrafficFilterMode(mode: dev.opentunnel.vpn.data.TrafficFilterMode) {
        _trafficFilterMode.value = mode
    }

    fun setTrafficSearchQuery(query: String) {
        _trafficSearchQuery.value = query
    }

    fun resetTrafficStats() {
        viewModelScope.launch {
            trafficCollector.reset()
        }
    }

    fun pauseTrafficMonitoring() {
        trafficCollector.pause()
    }

    fun resumeTrafficMonitoring() {
        trafficCollector.resume()
    }

    fun checkUsageAccessPermission() {
        val granted = dev.opentunnel.vpn.core.hasUsageStatsPermission(getApplication())
        if (granted != _hasUsageAccessPermission.value) {
            _hasUsageAccessPermission.value = granted
            if (granted) {
                viewModelScope.launch {
                    trafficCollector.reset()
                }
            }
        }
    }

    fun openUsageAccessSettings() {
        dev.opentunnel.vpn.core.openUsageAccessSettings(getApplication())
    }

    override fun onCleared() {
        trafficCollector.destroy()
        super.onCleared()
    }
}
