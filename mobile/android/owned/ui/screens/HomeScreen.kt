package dev.opentunnel.vpn.ui.screens


import android.os.SystemClock
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Apps
import androidx.compose.material.icons.rounded.ArrowDownward
import androidx.compose.material.icons.rounded.ArrowUpward
import androidx.compose.material.icons.rounded.Article
import androidx.compose.material.icons.rounded.ChevronLeft
import androidx.compose.material.icons.rounded.ChevronRight
import androidx.compose.material.icons.rounded.DataUsage
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Home
import androidx.compose.material.icons.rounded.Public
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.lerp
import androidx.compose.ui.unit.sp
import dev.opentunnel.vpn.core.ConnectionStage
import dev.opentunnel.vpn.core.TrafficStats
import dev.opentunnel.vpn.core.TunnelStatus
import dev.opentunnel.vpn.data.AppLanguage
import dev.opentunnel.vpn.data.AppSettings
import dev.opentunnel.vpn.data.SplitTunnelMode
import dev.opentunnel.vpn.data.VpnProfile
import dev.opentunnel.vpn.ui.components.ActionableErrorBottomSheet
import dev.opentunnel.vpn.ui.components.ConnectOrb
import dev.opentunnel.vpn.ui.components.DetailRow
import dev.opentunnel.vpn.ui.components.FloatingIslandNavigation
import dev.opentunnel.vpn.ui.components.SectionCard
import dev.opentunnel.vpn.ui.components.SettingRow
import dev.opentunnel.vpn.ui.theme.LocalStatusPalette
import dev.opentunnel.vpn.ui.theme.MonoNumberStyle
import dev.opentunnel.vpn.util.Formatters
import dev.opentunnel.vpn.util.Strings
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun HomeScreen(
    status: TunnelStatus,
    stats: TrafficStats,
    profile: VpnProfile,
    profiles: List<VpnProfile>,
    settings: AppSettings,
    rxHistoryList: List<Long> = emptyList(),
    txHistoryList: List<Long> = emptyList(),
    scrollState: ScrollState = rememberScrollState(),
    onToggleConnection: () -> Unit,
    onSelectProfile: (String) -> Unit,
    onOpenProfile: () -> Unit,
    onOpenProfileManagement: () -> Unit,
    onOpenSplitTunnel: () -> Unit,
    onOpenSplitNetworks: () -> Unit = {},
    onOpenTrafficMonitor: () -> Unit = {},
    onOpenLogs: () -> Unit,
    onOpenSettings: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val lang = settings.appLanguage
    dev.opentunnel.vpn.util.RememberScrollHaptic(scrollState, settings.hapticFeedbackEnabled)

    var showErrorBottomSheet by remember { mutableStateOf(false) }

    Box(Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(scrollState)
                .padding(horizontal = 18.dp),
        ) {
            HomeTopBar(scrollState = scrollState)

            Spacer(Modifier.height(8.dp))

            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                val haptic = LocalHapticFeedback.current
                ConnectOrb(
                    stage = status.stage,
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onToggleConnection()
                    },
                    lang = settings.appLanguage,
                    enabled = true,
                )
            }

            Spacer(Modifier.height(12.dp))

            StatusLine(status = status, profile = profile, lang = settings.appLanguage)

            Spacer(Modifier.height(16.dp))


            AnimatedVisibility(
                visible = status.stage == ConnectionStage.CONNECTED ||
                    status.stage == ConnectionStage.RECONNECTING,
                enter = fadeIn() + expandVertically(),
                exit = fadeOut() + shrinkVertically(),
            ) {
                Column {
                    TrafficRow(stats, settings.appLanguage, onClick = onOpenTrafficMonitor)
                    Spacer(Modifier.height(14.dp))
                    dev.opentunnel.vpn.ui.components.SpeedChart(
                        stats = stats,
                        appLanguage = settings.appLanguage,
                        rxHistoryList = rxHistoryList,
                        txHistoryList = txHistoryList,
                    )
                    Spacer(Modifier.height(18.dp))
                }
            }

            AnimatedVisibility(
                visible = status.stage == ConnectionStage.ERROR && status.error != null,
                enter = fadeIn() + expandVertically(),
                exit = fadeOut() + shrinkVertically(),
            ) {
                Column {
                    ErrorBanner(
                        message = status.error.orEmpty(),
                        onClick = { showErrorBottomSheet = true },
                    )
                    Spacer(Modifier.height(18.dp))
                }
            }

            // ── Clean Unified Active Profile Card ──────────────────────────────
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(MaterialTheme.shapes.large),
                shape = MaterialTheme.shapes.large,
                color = MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.68f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.20f)),
                shadowElevation = 0.dp,
                tonalElevation = 0.dp,
            ) {
                ProfilePickerRow(
                    profile = profile,
                    profiles = profiles,
                    lang = settings.appLanguage,
                    onSelectProfile = onSelectProfile,
                    onOpenProfile = onOpenProfile,
                    onOpenProfileManagement = onOpenProfileManagement,
                )
            }

            Spacer(Modifier.height(16.dp))

            // ── Split Tunneling Shortcuts (Apps & Networks) ───────────────────
            SectionCard {
                SettingRow(
                    painter = painterResource(dev.opentunnel.vpn.R.drawable.ic_split_tunnel),
                    title = Strings.splitTunnelTitle(lang),
                    subtitle = splitTunnelSummary(settings, lang),
                    iconTint = scheme.tertiary,
                    iconBackground = scheme.tertiary.copy(alpha = 0.14f),
                    onClick = onOpenSplitTunnel,
                )
                HorizontalDivider(
                    modifier = Modifier.padding(horizontal = 16.dp),
                    thickness = 0.5.dp,
                    color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f),
                )
                SettingRow(
                    icon = Icons.Rounded.Public,
                    title = if (Strings.isRtl(lang)) "تونل‌سازی شبکه‌ها و سایت‌ها" else "Network & Site Split Tunneling",
                    subtitle = splitTunnelNetworksSummary(settings, lang),
                    iconTint = scheme.primary,
                    iconBackground = scheme.primary.copy(alpha = 0.14f),
                    onClick = onOpenSplitNetworks,
                )
            }

            AnimatedVisibility(
                visible = status.stage == ConnectionStage.CONNECTED,
                enter = fadeIn() + expandVertically(),
                exit = fadeOut() + shrinkVertically(),
            ) {
                Column {
                    Spacer(Modifier.height(16.dp))
                    ConnectionDetails(status, settings.appLanguage)
                }
            }

            Spacer(Modifier.height(20.dp))

            val uriHandler = LocalUriHandler.current
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        runCatching {
                            uriHandler.openUri("https://github.com/AlieShayan/OpenTunnel")
                        }
                    }
                    .padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Github@AlieShayan",
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                    textAlign = TextAlign.Center,
                )
            }

            // Bottom space to prevent floating navigation bar from covering content
            Spacer(Modifier.height(100.dp))
        }

        if (showErrorBottomSheet && status.error != null) {
            ActionableErrorBottomSheet(
                errorMessage = status.error,
                appLanguage = lang,
                onRetry = onToggleConnection,
                onEditProfile = onOpenProfile,
                onViewLogs = onOpenLogs,
                onDismiss = { showErrorBottomSheet = false },
            )
        }
    }
}

// ── Profile Picker Sheet & Row ─────────────────────────────────────────────

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProfilePickerRow(
    profile: VpnProfile,
    profiles: List<VpnProfile>,
    lang: AppLanguage,
    onSelectProfile: (String) -> Unit,
    onOpenProfile: () -> Unit,
    onOpenProfileManagement: () -> Unit,
) {
    var showSheet by remember { mutableStateOf(false) }
    val haptic = LocalHapticFeedback.current

    Box(Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { showSheet = true }
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Surface(
                shape = CircleShape,
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
                modifier = Modifier.size(42.dp),
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.Public,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(22.dp),
                    )
                }
            }

            Spacer(Modifier.width(14.dp))

            Column(Modifier.weight(1f)) {
                Text(
                    text = profile.displayName,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Spacer(Modifier.height(2.dp))
                Text(
                    text = when {
                        !profile.isComplete -> Strings.tapToSetupProfile(lang)
                        profile.server.isNotBlank() -> "${profile.server} \u00B7 ${profile.protocol}"
                        else -> profile.protocol
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            IconButton(onClick = onOpenProfileManagement) {
                Icon(
                    imageVector = Icons.Rounded.Tune,
                    contentDescription = Strings.manageProfilesAction(lang),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        if (showSheet) {
            val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

            ModalBottomSheet(
                onDismissRequest = { showSheet = false },
                sheetState = sheetState,
                containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
                shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 20.dp, vertical = 10.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = Strings.selectProfileTitle(lang),
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface,
                            fontWeight = FontWeight.Bold,
                        )

                        TextButton(onClick = {
                            showSheet = false
                            onOpenProfileManagement()
                        }) {
                            Text(Strings.manageProfilesAction(lang))
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f))

                    profiles.forEach { p ->
                        val isSelected = p.id == profile.id
                        Surface(
                            shape = MaterialTheme.shapes.medium,
                            color = if (isSelected) MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f) else Color.Transparent,
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                    onSelectProfile(p.id)
                                    showSheet = false
                                },
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                RadioButton(
                                    selected = isSelected,
                                    onClick = {
                                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                        onSelectProfile(p.id)
                                        showSheet = false
                                    },
                                )
                                Spacer(Modifier.width(12.dp))
                                Column(Modifier.weight(1f)) {
                                    Text(
                                        text = p.displayName,
                                        style = MaterialTheme.typography.bodyLarge,
                                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                    )
                                    if (p.server.isNotBlank()) {
                                        Text(
                                            text = p.server,
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                }
                            }
                        }
                    }

                    Spacer(Modifier.height(16.dp))
                }
            }
        }
    }
}

// ── Location Badge ─────────────────────────────────────────────────────────

// ── Top bar ────────────────────────────────────────────────────────────────

@Composable
private fun HomeTopBar(scrollState: ScrollState) {
    val scrollFraction = (scrollState.value / 350f).coerceIn(0f, 1f)
    val alphaValue = 1f - scrollFraction
    val fontSizeSp = lerp(28.sp, 16.sp, scrollFraction)

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 8.dp, bottom = 4.dp)
            .graphicsLayer {
                alpha = alphaValue
            },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        val gradient = Brush.linearGradient(
            colors = listOf(
                MaterialTheme.colorScheme.primary,
                MaterialTheme.colorScheme.tertiary,
            )
        )
        Text(
            text = "岚序 Lansyra",
            style = MaterialTheme.typography.headlineMedium.copy(
                fontSize = fontSizeSp,
                brush = gradient,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = (-0.5).sp,
            ),
            textAlign = TextAlign.Center,
        )
        Text(
            text = "AnyConnect \u00B7 openconnect",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.75f * alphaValue),
            textAlign = TextAlign.Center,
        )
    }
}

// ── Status ─────────────────────────────────────────────────────────────────

@Composable
private fun StatusLine(status: TunnelStatus, profile: VpnProfile, lang: AppLanguage) {
    val palette = LocalStatusPalette.current

    val label = when (status.stage) {
        ConnectionStage.CONNECTED -> status.info.server ?: Strings.connected(lang)
        ConnectionStage.IDLE -> Strings.readyToConnect(lang, profile.displayName)
        ConnectionStage.ERROR -> Strings.connectionFailed(lang)
        ConnectionStage.AUTHENTICATING -> Strings.authenticating(lang)
        ConnectionStage.PREPARING -> Strings.preparing(lang)
        ConnectionStage.CONNECTING -> Strings.connecting(lang)
        ConnectionStage.DISCONNECTING -> Strings.disconnecting(lang)
        ConnectionStage.RECONNECTING -> Strings.reconnecting(lang)
        else -> status.detail ?: Strings.connecting(lang)
    }

    val tint = when (status.stage) {
        ConnectionStage.CONNECTED -> palette.connected
        ConnectionStage.ERROR -> palette.error
        ConnectionStage.IDLE -> MaterialTheme.colorScheme.onSurfaceVariant
        else -> palette.connecting
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.titleMedium,
            color = tint,
            textAlign = TextAlign.Center,
        )

        if (status.stage == ConnectionStage.CONNECTED && status.connectedAtElapsed > 0L) {
            val elapsed by produceState(0L, status.connectedAtElapsed) {
                while (true) {
                    value = SystemClock.elapsedRealtime() - status.connectedAtElapsed
                    delay(1000)
                }
            }
            Text(
                text = Formatters.duration(elapsed),
                style = MaterialTheme.typography.displaySmall.merge(MonoNumberStyle),
                color = MaterialTheme.colorScheme.onSurface,
            )
        }
    }
}

// ── Traffic ────────────────────────────────────────────────────────────────

@Composable
private fun TrafficRow(
    stats: TrafficStats,
    lang: AppLanguage,
    onClick: (() -> Unit)? = null,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        TrafficTile(
            modifier = Modifier.weight(1f),
            label = Strings.downloaded(lang),
            total = Formatters.bytes(stats.rxBytes),
            rate = Formatters.rate(stats.rxRate),
            tint = LocalStatusPalette.current.connected,
            up = false,
            onClick = onClick,
        )
        TrafficTile(
            modifier = Modifier.weight(1f),
            label = Strings.uploaded(lang),
            total = Formatters.bytes(stats.txBytes),
            rate = Formatters.rate(stats.txRate),
            tint = MaterialTheme.colorScheme.secondary,
            up = true,
            onClick = onClick,
        )
    }
}

@Composable
private fun TrafficTile(
    modifier: Modifier,
    label: String,
    total: String,
    rate: String,
    tint: Color,
    up: Boolean,
    onClick: (() -> Unit)? = null,
) {
    Surface(
        modifier = if (onClick != null) {
            modifier
                .clip(MaterialTheme.shapes.large)
                .clickable(onClick = onClick)
        } else {
            modifier
        },
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.62f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.18f)),
        tonalElevation = 0.dp,
    ) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Surface(
                    shape = CircleShape,
                    color = tint.copy(alpha = 0.15f),
                    modifier = Modifier.size(24.dp),
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            imageVector = if (up) Icons.Rounded.ArrowUpward else Icons.Rounded.ArrowDownward,
                            contentDescription = null,
                            tint = tint,
                            modifier = Modifier.size(14.dp),
                        )
                    }
                }
                Spacer(Modifier.width(8.dp))
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    fontWeight = FontWeight.Medium,
                )
            }
            Spacer(Modifier.height(10.dp))
            Text(
                text = total,
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface,
            )
            Spacer(Modifier.height(2.dp))
            Text(
                text = rate,
                style = MaterialTheme.typography.bodySmall.merge(MonoNumberStyle),
                color = tint,
                fontWeight = FontWeight.SemiBold,
            )
        }
    }
}

// ── Error Banner ───────────────────────────────────────────────────────────

@Composable
private fun ErrorBanner(message: String, onClick: () -> Unit) {
    Surface(
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.errorContainer,
        modifier = Modifier
            .fillMaxWidth()
            .clip(MaterialTheme.shapes.large)
            .clickable(onClick = onClick),
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                Icons.Rounded.ErrorOutline,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onErrorContainer,
                modifier = Modifier.size(22.dp),
            )
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onErrorContainer,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = "Tap for troubleshooting & recovery actions",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f),
                )
            }
            Icon(
                Icons.Rounded.ChevronRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onErrorContainer,
                modifier = Modifier.size(18.dp),
            )
        }
    }
}

// ── Connection details ─────────────────────────────────────────────────────

@Composable
private fun ConnectionDetails(status: TunnelStatus, lang: AppLanguage) {
    val info = status.info
    SectionCard(title = Strings.connectionDetails(lang)) {
        Column(Modifier.padding(vertical = 6.dp)) {
            info.ipv4?.takeIf { it.isNotBlank() }?.let { DetailRow(Strings.ipv4Address(lang), it) }
            info.ipv6?.takeIf { it.isNotBlank() }?.let { DetailRow(Strings.ipv6Address(lang), it) }
            if (info.dns.isNotEmpty()) DetailRow(Strings.dnsServers(lang), info.dns.joinToString("\n"))
            info.domain?.takeIf { it.isNotBlank() }?.let { DetailRow(Strings.searchDomain(lang), it) }
            if (info.mtu > 0) DetailRow(Strings.mtuLabel(lang), info.mtu.toString())
            info.cstpCipher?.takeIf { it.isNotBlank() }?.let { DetailRow(Strings.tlsChannel(lang), it) }
            val dtls = info.dtlsCipher?.takeIf { it.isNotBlank() }
            DetailRow(Strings.dtlsChannel(lang), dtls ?: Strings.dtlsNotEstablished(lang))
            if (info.serverRoutes.isNotEmpty()) {
                DetailRow(Strings.gatewayRoutes(lang), info.serverRoutes.joinToString("\n"))
            }
            if (info.excludedApps > 0) {
                DetailRow(Strings.appsOutsideTunnel(lang), info.excludedApps.toString())
            }
        }
    }
}

private fun splitTunnelSummary(settings: AppSettings, lang: AppLanguage): String = when {
    !settings.splitTunnelEnabled -> Strings.splitTunnelOffSummary(lang)
    settings.selectedPackages.isEmpty() -> Strings.splitTunnelNoAppsSelectedSummary(lang)
    settings.splitTunnelMode == SplitTunnelMode.EXCLUDE_SELECTED ->
        "${settings.selectedPackages.size} app(s) bypass the VPN"
    else -> "Only ${settings.selectedPackages.size} app(s) use the VPN"
}

private fun splitTunnelNetworksSummary(settings: AppSettings, lang: AppLanguage): String = when {
    !settings.splitTunnelNetworksEnabled -> Strings.splitTunnelNetworksOffSummary(lang)
    settings.splitTunnelNetworks.isEmpty() -> Strings.splitTunnelNetworksNoEntriesSummary(lang)
    settings.splitTunnelNetworksMode == SplitTunnelMode.EXCLUDE_SELECTED ->
        if (Strings.isRtl(lang)) "${settings.splitTunnelNetworks.size} شبکه/سایت خارج از تونل" else "${settings.splitTunnelNetworks.size} IP network(s) bypass the VPN"
    else ->
        if (Strings.isRtl(lang)) "فقط ${settings.splitTunnelNetworks.size} شبکه/سایت در تونل" else "Only ${settings.splitTunnelNetworks.size} IP network(s) use the VPN"
}
