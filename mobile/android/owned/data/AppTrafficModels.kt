// Adapted from the checksum-pinned OpenTunnel source; see THIRD-PARTY-NOTICES.md.
package dev.opentunnel.vpn.data

import androidx.compose.runtime.Immutable

@Immutable
data class AppTrafficEntry(
    val packageName: String, val uid: Int, val label: String, val isSystem: Boolean,
    val isVpnRouted: Boolean, val rxBytes: Long = 0, val txBytes: Long = 0,
    val totalBytes: Long = 0, val rxRate: Long = 0, val txRate: Long = 0,
    val totalRate: Long = 0, val trafficSharePercent: Float = 0f,
    val firstSeenTime: Long = 0, val lastActiveTime: Long = 0, val isActive: Boolean = false,
)

@Immutable
data class AppTrafficSummary(
    val totalRxBytes: Long = 0, val totalTxBytes: Long = 0, val totalBytes: Long = 0,
    val currentRxRate: Long = 0, val currentTxRate: Long = 0,
    val activeAppsCount: Int = 0, val totalAppsCount: Int = 0,
    val sessionStartTime: Long = 0, val available: Boolean = true,
)

enum class TrafficSortBy { TOTAL_TRAFFIC, DOWNLOAD, UPLOAD, DOWNLOAD_SPEED, UPLOAD_SPEED, APP_NAME }
enum class SortDirection { DESCENDING, ASCENDING }
enum class TrafficFilterMode { ALL, ACTIVE_ONLY }
