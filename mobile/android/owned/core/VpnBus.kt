// Adapted from the checksum-pinned OpenTunnel source; see THIRD-PARTY-NOTICES.md.
package dev.opentunnel.vpn.core

import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * Single source of truth for tunnel state, shared between the VpnService (which
 * writes) and the UI (which reads). Deliberately a process-wide singleton: the
 * service and the activity live in the same process, and this saves the UI from
 * having to bind to the service just to render a status dot.
 */
object VpnBus {

    private const val TAG = "OpenTunnel"
    private const val MAX_LOG_LINES = 800
    private const val MAX_LOG_CHARACTERS = 256 * 1024
    private var logCharacters = 0
    private var lastLogPublish = 0L
    private var publishPending = false
    private val publisher by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }
    private val diagnostic = DiagnosticLog()
    fun remember(value: String?) = diagnostic.remember(value)

    private val _status = MutableStateFlow(TunnelStatus())
    val status: StateFlow<TunnelStatus> = _status.asStateFlow()

    private val _stats = MutableStateFlow(TrafficStats())
    private val statisticsLock = Any()
    val stats: StateFlow<TrafficStats> = _stats.asStateFlow()
    private val viewers = mutableSetOf<Any>()
    private val _statisticsVisible = MutableStateFlow(false)
    val statisticsVisible = _statisticsVisible.asStateFlow()
    @Synchronized fun setStatisticsVisible(owner: Any, visible: Boolean) {
        if (visible) viewers.add(owner) else viewers.remove(owner)
        _statisticsVisible.value = viewers.isNotEmpty()
    }
    private val _widgetStatisticsPresent = MutableStateFlow(false)
    val widgetStatisticsPresent = _widgetStatisticsPresent.asStateFlow()
    fun setWidgetStatisticsPresent(present: Boolean) { _widgetStatisticsPresent.value = present }

    private val logDeque = ArrayDeque<LogLine>(MAX_LOG_LINES)
    private val _logs = MutableStateFlow<List<LogLine>>(emptyList())
    val logs: StateFlow<List<LogLine>> = _logs.asStateFlow()


    fun setStage(stage: ConnectionStage, detail: String? = null) {
        _status.update { current ->
            current.copy(
                stage = stage,
                detail = detail ?: defaultDetail(stage),
                error = if (stage == ConnectionStage.ERROR) current.error else null,
            )
        }
    }

    fun setConnected(connectedAtElapsed: Long, info: TunnelInfo) {
        _status.update {
            it.copy(
                stage = ConnectionStage.CONNECTED,
                detail = null,
                error = null,
                connectedAtElapsed = connectedAtElapsed,
                info = info,
            )
        }
    }

    fun updateInfo(transform: (TunnelInfo) -> TunnelInfo) {
        _status.update { it.copy(info = transform(it.info)) }
    }

    fun setError(message: String) {
        val safeMessage = diagnostic.clean(message)
        _status.update {
            it.copy(
                stage = ConnectionStage.ERROR,
                detail = null,
                error = safeMessage,
                connectedAtElapsed = 0L,
            )
        }
    }

    fun reset() {
        diagnostic.clear()
        _status.value = TunnelStatus()
        synchronized(statisticsLock) { lastSampleAt = 0L; _stats.value = TrafficStats() }
    }

    private fun defaultDetail(stage: ConnectionStage): String? = when (stage) {
        ConnectionStage.PREPARING -> "Preparing tunnel"
        ConnectionStage.AUTHENTICATING -> "Authenticating"
        ConnectionStage.CONNECTING -> "Negotiating connection"
        ConnectionStage.RECONNECTING -> "Reconnecting"
        ConnectionStage.DISCONNECTING -> "Disconnecting"
        else -> null
    }


    private var lastSampleAt = 0L

    fun updateStats(rxBytes: Long, txBytes: Long, rxPackets: Long, txPackets: Long, nowElapsed: Long) {
        synchronized(statisticsLock) {
            val prev = _stats.value
            val dtMs = if (lastSampleAt == 0L) 0L else (nowElapsed - lastSampleAt)
            val rxRate = if (dtMs <= 0) 0 else if (dtMs > 250) (rxBytes - prev.rxBytes) * 1000 / dtMs else prev.rxRate
            val txRate = if (dtMs <= 0) 0 else if (dtMs > 250) (txBytes - prev.txBytes) * 1000 / dtMs else prev.txRate
            lastSampleAt = nowElapsed
            _stats.value = TrafficStats(
                rxBytes = rxBytes,
                txBytes = txBytes,
                rxPackets = rxPackets,
                txPackets = txPackets,
                rxRate = rxRate.coerceAtLeast(0),
                txRate = txRate.coerceAtLeast(0),
                sampleTimestamp = nowElapsed,
            )
        }
    }

    fun resetStatsWindow() {
        synchronized(statisticsLock) { lastSampleAt = 0L }
    }


    fun log(level: LogLevel, message: String) {
        if (message.isBlank()) return
        val safe = diagnostic.clean(message).trimEnd()
        val line = LogLine(System.currentTimeMillis(), level, safe)
        synchronized(logDeque) {
            while (logDeque.isNotEmpty() && (logDeque.size >= MAX_LOG_LINES || logCharacters + safe.length > MAX_LOG_CHARACTERS))
                logCharacters -= logDeque.removeFirst().text.length
            logDeque.addLast(line)
            logCharacters += safe.length
            val now = android.os.SystemClock.elapsedRealtime()
            if (now - lastLogPublish >= 250L) {
                _logs.value = logDeque.toList()
                lastLogPublish = now
            } else if (!publishPending) {
                publishPending = true
                publisher.postDelayed({
                    synchronized(logDeque) {
                        _logs.value = logDeque.toList()
                        lastLogPublish = android.os.SystemClock.elapsedRealtime()
                        publishPending = false
                    }
                }, 250L)
            }
        }
        // Core and authentication content is sanitized before either destination.
        if (level == LogLevel.ERROR) Log.e(TAG, safe) else Log.d(TAG, safe)
    }

    fun info(message: String) = log(LogLevel.APP, message)
    fun debug(message: String) = log(LogLevel.DEBUG, message)
    fun error(message: String) = log(LogLevel.ERROR, message)

    fun clearLogs() {
        synchronized(logDeque) {
            logDeque.clear()
            logCharacters = 0
            publisher.removeCallbacksAndMessages(null)
            publishPending = false
            _logs.value = emptyList()
        }
    }
}
