package dev.opentunnel.vpn.service

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.PowerManager
import androidx.core.content.ContextCompat
import dev.opentunnel.vpn.core.ConnectionStage
import dev.opentunnel.vpn.core.StatisticsPolicy
import dev.opentunnel.vpn.core.VpnBus
import dev.opentunnel.vpn.widget.TunnelWidget
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.Closeable

/** Demand owns the timer; the native connection and power lease have separate lifetimes. */
internal class StatisticsSampler(
    private val context: Context,
    scope: CoroutineScope,
    notification: StateFlow<Boolean>,
    sample: () -> Unit,
) : Closeable {
    private val screen = MutableStateFlow(context.getSystemService(PowerManager::class.java)?.isInteractive == true)
    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            screen.value = intent.action == Intent.ACTION_SCREEN_ON
        }
    }
    private var closed = false
    init {
        ContextCompat.registerReceiver(context, receiver, IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_SCREEN_OFF)
        }, ContextCompat.RECEIVER_NOT_EXPORTED)
        VpnBus.setWidgetStatisticsPresent(TunnelWidget.hasWidgets(context))
    }
    private val job = scope.launch {
        combine(screen, VpnBus.statisticsVisible, notification, VpnBus.widgetStatisticsPresent, VpnBus.status) {
            interactive, visible, notice, widgets, state ->
            if (state.stage == ConnectionStage.CONNECTED)
                StatisticsPolicy.interval(interactive, visible, notice, widgets) else 0L
        }.distinctUntilChanged().collectLatest { interval ->
            if (interval > 0) {
                VpnBus.resetStatsWindow()
                while (isActive) { sample(); delay(interval) }
            }
        }
    }
    override fun close() {
        if (closed) return
        closed = true
        job.cancel()
        runCatching { context.unregisterReceiver(receiver) }
    }
}
