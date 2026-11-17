package dev.opentunnel.vpn.core

import dev.opentunnel.vpn.data.AppSettings
import dev.opentunnel.vpn.data.AppTrafficEntry
import dev.opentunnel.vpn.data.AppTrafficSummary
import dev.opentunnel.vpn.data.SplitTunnelMode
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

data class UidTrafficBytes(val rxBytes: Long, val txBytes: Long)
interface TrafficStatsProvider {
    fun getUidRxBytes(uid: Int): Long
    fun getUidTxBytes(uid: Int): Long
    fun getAllUidStats(): Map<Int, UidTrafficBytes>? = null
    fun getElapsedRealtime(): Long
    fun hasPermission(): Boolean = true
}
data class AppMetadata(val packageName: String, val uid: Int, val label: String, val isSystem: Boolean)

/** Demand controls the timer itself. Counter queries, including reset/load, never run on the caller's UI thread. */
class AppTrafficCollector(
    private val statsProvider: TrafficStatsProvider,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val sampleIntervalMs: Long = 1000L,
) {
    private data class Demand(val visible: Boolean = false, val paused: Boolean = false)
    private class Counter(var baseline: Long, var previous: Long = baseline, var rate: Long = 0, var zeroTicks: Int = 0) {
        fun sample(value: Long, elapsed: Long, reset: Boolean, rebaseRate: Boolean): Long {
            if (reset || value < baseline) baseline = value
            if (reset || rebaseRate || value < previous) { previous = value; rate = 0; zeroTicks = 0 }
            val delta = (value - previous).coerceAtLeast(0)
            val raw = (delta.toDouble() * 1000 / elapsed).toLong()
            rate = if (delta == 0L) {
                if (++zeroTicks >= 2) 0 else (rate * 0.35).toLong()
            } else {
                zeroTicks = 0
                if (rate == 0L) raw else (raw * 0.65 + rate * 0.35).toLong()
            }
            previous = value
            return (value - baseline).coerceAtLeast(0)
        }
    }
    private class Tracker(var app: AppMetadata, bytes: UidTrafficBytes, val firstSeen: Long) {
        val rx = Counter(bytes.rxBytes)
        val tx = Counter(bytes.txBytes)
        var sampledAt = firstSeen
        var lastActive = 0L
    }

    private val scope = CoroutineScope(SupervisorJob() + dispatcher)
    private val mutex = Mutex()
    private val demand = MutableStateFlow(Demand())
    val paused = demand.map { it.paused }.distinctUntilChanged()
    private val _entries = MutableStateFlow<List<AppTrafficEntry>>(emptyList())
    val entries = _entries.asStateFlow()
    private val _summary = MutableStateFlow(AppTrafficSummary())
    val summary = _summary.asStateFlow()
    private val trackers = linkedMapOf<String, Tracker>()
    private var sessionStart = statsProvider.getElapsedRealtime()
    private var hadPermission = statsProvider.hasPermission()
    private var rebaseRates = false
    @Volatile private var settings = AppSettings()

    init {
        require(sampleIntervalMs > 0)
        scope.launch {
            demand.map { it.visible && !it.paused }.distinctUntilChanged().collectLatest { active ->
                if (active) {
                    mutex.withLock { rebaseRates = true }
                    while (isActive) { tick(); delay(sampleIntervalMs) }
                }
            }
        }
    }

    fun updateSettings(settings: AppSettings) { this.settings = settings }
    fun start() { demand.update { it.copy(visible = true) } }
    fun stop() { demand.update { it.copy(visible = false) } }
    fun pause() { demand.update { it.copy(paused = true) } }
    fun resume() { demand.update { it.copy(paused = false) } }
    fun destroy() { scope.cancel() }

    private fun bytes(uid: Int, bulk: Map<Int, UidTrafficBytes>?) =
        if (bulk != null) bulk[uid] ?: UidTrafficBytes(0, 0)
        else UidTrafficBytes(statsProvider.getUidRxBytes(uid).coerceAtLeast(0), statsProvider.getUidTxBytes(uid).coerceAtLeast(0))

    suspend fun loadApps(packages: List<AppMetadata>) = withContext(dispatcher) {
        mutex.withLock {
            val apps = packages.filter { !it.isSystem && it.uid > 0 }.associateBy { it.packageName }
            trackers.keys.retainAll(apps.keys)
            val missing = apps.values.filter { trackers[it.packageName]?.app?.uid != it.uid }
            if (missing.isNotEmpty()) {
                val bulk = statsProvider.getAllUidStats()
                currentCoroutineContext().ensureActive()
                val now = statsProvider.getElapsedRealtime()
                missing.forEach { trackers[it.packageName] = Tracker(it, bytes(it.uid, bulk), now) }
            }
            apps.forEach { (name, app) -> trackers[name]?.app = app }
        }
    }

    suspend fun reset() = withContext(dispatcher) {
        mutex.withLock {
            val bulk = statsProvider.getAllUidStats()
            currentCoroutineContext().ensureActive()
            sessionStart = statsProvider.getElapsedRealtime()
            trackers.replaceAll { _, old -> Tracker(old.app, bytes(old.app.uid, bulk), sessionStart) }
            hadPermission = statsProvider.hasPermission()
            _entries.value = emptyList()
            _summary.value = AppTrafficSummary(sessionStartTime = sessionStart)
        }
    }

    suspend fun tick() = withContext(dispatcher) {
        mutex.withLock {
            if (trackers.isEmpty()) {
                _entries.value = emptyList()
                _summary.value = AppTrafficSummary(sessionStartTime = sessionStart)
                return@withLock
            }
            val permission = statsProvider.hasPermission()
            val reset = permission != hadPermission
            val bulk = statsProvider.getAllUidStats()
            currentCoroutineContext().ensureActive()
            val now = statsProvider.getElapsedRealtime()
            val currentSettings = settings
            var rxTotal = 0L
            var txTotal = 0L
            var rxRate = 0L
            var txRate = 0L
            var activeApps = 0
            val seen = mutableSetOf<Int>()
            val snapshot = trackers.values.map { tracker ->
                val app = tracker.app
                val current = bytes(app.uid, bulk)
                val elapsed = (now - tracker.sampledAt).coerceAtLeast(1)
                val rx = tracker.rx.sample(current.rxBytes, elapsed, reset, rebaseRates)
                val tx = tracker.tx.sample(current.txBytes, elapsed, reset, rebaseRates)
                tracker.sampledAt = now
                val active = tracker.rx.rate > 0 || tracker.tx.rate > 0
                if (active) { tracker.lastActive = now; activeApps++ }
                if (seen.add(app.uid)) {
                    rxTotal += rx; txTotal += tx
                    rxRate += tracker.rx.rate; txRate += tracker.tx.rate
                }
                val routed = !currentSettings.splitTunnelEnabled ||
                    ((app.packageName in currentSettings.selectedPackages) == (currentSettings.splitTunnelMode == SplitTunnelMode.INCLUDE_SELECTED))
                AppTrafficEntry(packageName = app.packageName, uid = app.uid, label = app.label, isSystem = app.isSystem,
                    isVpnRouted = routed, rxBytes = rx, txBytes = tx, totalBytes = rx + tx,
                    rxRate = tracker.rx.rate, txRate = tracker.tx.rate, totalRate = tracker.rx.rate + tracker.tx.rate,
                    trafficSharePercent = 0f, firstSeenTime = tracker.firstSeen, lastActiveTime = tracker.lastActive, isActive = active)
            }
            val total = rxTotal + txTotal
            _entries.value = if (total == 0L) snapshot else snapshot.map {
                it.copy(trafficSharePercent = (it.totalBytes.toDouble() * 100 / total).toFloat().coerceIn(0f, 100f))
            }
            _summary.value = AppTrafficSummary(totalRxBytes = rxTotal, totalTxBytes = txTotal, totalBytes = total,
                currentRxRate = rxRate, currentTxRate = txRate, activeAppsCount = activeApps,
                totalAppsCount = trackers.size, sessionStartTime = sessionStart)
            hadPermission = permission
            rebaseRates = false
        }
    }
}
