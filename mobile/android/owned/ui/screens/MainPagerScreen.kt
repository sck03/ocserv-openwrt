package dev.opentunnel.vpn.ui.screens

import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.LifecycleStartEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.opentunnel.vpn.core.LogLine
import dev.opentunnel.vpn.core.TrafficStats
import dev.opentunnel.vpn.core.TunnelStatus
import dev.opentunnel.vpn.core.SpeedSnapshot
import dev.opentunnel.vpn.data.*
import dev.opentunnel.vpn.ui.components.OpenTunnelWorld
import dev.opentunnel.vpn.ui.theme.ThemeMode
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainPagerScreen(
    status: TunnelStatus,
    trafficStats: StateFlow<TrafficStats>,
    logs: List<LogLine>,
    profile: VpnProfile,
    profiles: List<VpnProfile>,
    settings: AppSettings,
    speedHistory: StateFlow<SpeedSnapshot>,
    appTrafficSummary: AppTrafficSummary = AppTrafficSummary(),
    appTrafficEntries: List<AppTrafficEntry> = emptyList(),
    appTrafficPaused: Boolean = false,
    trafficSortBy: TrafficSortBy = TrafficSortBy.TOTAL_TRAFFIC,
    trafficSortDirection: SortDirection = SortDirection.DESCENDING,
    trafficFilterMode: TrafficFilterMode = TrafficFilterMode.ALL,
    trafficSearchQuery: String = "",
    hasUsageAccessPermission: Boolean = true,
    onToggleConnection: () -> Unit,
    onSelectProfile: (String) -> Unit,
    onOpenProfile: () -> Unit,
    onOpenProfileManagement: () -> Unit,
    onOpenSplitTunnel: () -> Unit,
    onOpenSplitNetworks: () -> Unit = {},
    onClearLogs: () -> Unit,
    onThemeMode: (ThemeMode) -> Unit,
    onAppLanguage: (AppLanguage) -> Unit,
    onDynamicColor: (Boolean) -> Unit,
    onBypassLocalNetworks: (Boolean) -> Unit,
    onConnectOnBoot: (Boolean) -> Unit,
    onReconnectOnNetworkChange: (Boolean) -> Unit,
    onShowStatsInNotification: (Boolean) -> Unit,
    onVerboseLogging: (Boolean) -> Unit,
    onHapticFeedbackEnabled: (Boolean) -> Unit,
    onSortByChange: (TrafficSortBy) -> Unit = {},
    onToggleSortDirection: () -> Unit = {},
    onFilterModeChange: (TrafficFilterMode) -> Unit = {},
    onSearchQueryChange: (String) -> Unit = {},
    onResetTrafficStats: () -> Unit = {},
    onPauseTrafficMonitoring: () -> Unit = {},
    onResumeTrafficMonitoring: () -> Unit = {},
    onGrantUsageAccess: () -> Unit = {},
    onRefreshPermission: () -> Unit = {},
    onTrafficVisibilityChanged: (Boolean) -> Unit,
    onStatisticsVisibilityChanged: (Any, Boolean) -> Unit,
) {
    // 4 Pages: 0: Home, 1: Traffic Monitor, 2: Logs, 3: Settings
    val pagerState = rememberPagerState(initialPage = 0, pageCount = { 4 })
    val statisticsPage = pagerState.settledPage in 0..1
    val statisticsOwner = remember { Any() }
    LifecycleStartEffect(statisticsPage) {
        onStatisticsVisibilityChanged(statisticsOwner, statisticsPage)
        onStopOrDispose { onStatisticsVisibilityChanged(statisticsOwner, false) }
    }
    val stats = if (statisticsPage) trafficStats.collectAsStateWithLifecycle().value else TrafficStats()
    val history = if (pagerState.settledPage == 0) speedHistory.collectAsStateWithLifecycle().value else SpeedSnapshot()
    val currentHistory = if (history.session == status.connectedAtElapsed) history else SpeedSnapshot()
    val trafficPage = pagerState.settledPage == 1
    LifecycleStartEffect(trafficPage) {
        onTrafficVisibilityChanged(trafficPage)
        onStopOrDispose { onTrafficVisibilityChanged(false) }
    }
    val homeScrollState = rememberScrollState()
    val scope = rememberCoroutineScope()

    OpenTunnelWorld(
        pagerState = pagerState,
        stage = status.stage,
        lang = settings.appLanguage,
        homeScrollProvider = { homeScrollState.value.toFloat() },
        onNavigateToPage = { index ->
            scope.launch { pagerState.animateScrollToPage(index) }
        },
    ) {
        HorizontalPager(
            state = pagerState,
            modifier = Modifier.fillMaxSize(),
        ) { page ->
            when (page) {
                0 -> HomeScreen(
                    status = status,
                    stats = stats,
                    profile = profile,
                    profiles = profiles,
                    settings = settings,
                    rxHistoryList = currentHistory.rx,
                    txHistoryList = currentHistory.tx,
                    scrollState = homeScrollState,
                    onToggleConnection = onToggleConnection,
                    onSelectProfile = onSelectProfile,
                    onOpenProfile = onOpenProfile,
                    onOpenProfileManagement = onOpenProfileManagement,
                    onOpenSplitTunnel = onOpenSplitTunnel,
                    onOpenSplitNetworks = onOpenSplitNetworks,
                    onOpenTrafficMonitor = {
                        scope.launch { pagerState.animateScrollToPage(1) }
                    },
                    onOpenLogs = {
                        scope.launch { pagerState.animateScrollToPage(2) }
                    },
                    onOpenSettings = {
                        scope.launch { pagerState.animateScrollToPage(3) }
                    },
                )
                1 -> AppTrafficMonitorScreen(
                    vpnStats = stats,
                    summary = appTrafficSummary,
                    entries = appTrafficEntries,
                    isPaused = appTrafficPaused,
                    sortBy = trafficSortBy,
                    sortDirection = trafficSortDirection,
                    filterMode = trafficFilterMode,
                    searchQuery = trafficSearchQuery,
                    appLanguage = settings.appLanguage,
                    hapticEnabled = settings.hapticFeedbackEnabled,
                    hasUsageAccessPermission = hasUsageAccessPermission,
                    onSortByChange = onSortByChange,
                    onToggleSortDirection = onToggleSortDirection,
                    onFilterModeChange = onFilterModeChange,
                    onSearchQueryChange = onSearchQueryChange,
                    onResetStats = onResetTrafficStats,
                    onPauseMonitoring = onPauseTrafficMonitoring,
                    onResumeMonitoring = onResumeTrafficMonitoring,
                    onGrantUsageAccess = onGrantUsageAccess,
                    onRefreshPermission = onRefreshPermission,
                    onBack = {
                        scope.launch { pagerState.animateScrollToPage(0) }
                    },
                )
                2 -> LogScreen(
                    logs = logs,
                    appLanguage = settings.appLanguage,
                    hapticFeedbackEnabled = settings.hapticFeedbackEnabled,
                    onClear = onClearLogs,
                    onBack = {
                        scope.launch { pagerState.animateScrollToPage(0) }
                    },
                )
                3 -> SettingsScreen(
                    settings = settings,
                    onThemeMode = onThemeMode,
                    onAppLanguage = onAppLanguage,
                    onDynamicColor = onDynamicColor,
                    onBypassLocalNetworks = onBypassLocalNetworks,
                    onConnectOnBoot = onConnectOnBoot,
                    onReconnectOnNetworkChange = onReconnectOnNetworkChange,
                    onShowStatsInNotification = onShowStatsInNotification,
                    onVerboseLogging = onVerboseLogging,
                    onHapticFeedbackEnabled = onHapticFeedbackEnabled,
                    onOpenSplitNetworks = onOpenSplitNetworks,
                    onBack = {
                        scope.launch { pagerState.animateScrollToPage(0) }
                    },
                )
            }
        }
    }
}
