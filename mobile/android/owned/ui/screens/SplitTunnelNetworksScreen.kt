package dev.opentunnel.vpn.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Public
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import dev.opentunnel.vpn.data.AppSettings
import dev.opentunnel.vpn.data.SplitTunnelMode
import dev.opentunnel.vpn.ui.components.SectionCard

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SplitTunnelNetworksScreen(
    settings: AppSettings,
    onToggleEnabled: (Boolean) -> Unit,
    onChangeMode: (SplitTunnelMode) -> Unit,
    onAddNetwork: (String) -> Unit,
    onRemoveNetwork: (String) -> Unit,
    onClearAll: () -> Unit,
    onBack: () -> Unit,
) {
    val lang = settings.appLanguage
    val isPersian = lang == dev.opentunnel.vpn.data.AppLanguage.PERSIAN
    val context = LocalContext.current

    var newEntryText by remember { mutableStateOf("") }

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = if (isChinese) "IP 网络分流" else if (isPersian) "تونل‌سازی شبکه‌های IP" else "IP Network Split Tunneling",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                    )
                },
                navigationIcon = {
                    IconButton(onClick = {
                        dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                        onBack()
                    }) {
                        Icon(Icons.Rounded.ArrowBack, contentDescription = if (isChinese) "返回" else if (isPersian) "بازگشت" else "Back")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = Color.Transparent,
                ),
                actions = {
                    if (settings.splitTunnelNetworks.isNotEmpty()) {
                        TextButton(onClick = {
                            dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                            onClearAll()
                        }) {
                            Text(
                                text = if (isChinese) "清空" else if (isPersian) "پاکسازی همه" else "Clear All",
                                color = MaterialTheme.colorScheme.error,
                            )
                        }
                    }
                },
            )
        }
    ) { padding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 18.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            contentPadding = PaddingValues(bottom = 32.dp),
        ) {
            item {
                SectionCard {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = if (isChinese) "启用 IP 网络分流" else if (isPersian) "فعال‌سازی تونل‌سازی شبکه" else "Enable Network Split Tunneling",
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.Bold,
                            )
                            Text(
                                text = if (isChinese) "按 IPv4、IPv6 地址或 CIDR 网段分流，下次连接生效。" else if (isPersian)
                                    "مدیریت ترافیک با IP/CIDR؛ اعمال در اتصال بعدی"
                                else
                                    "Route by numeric IPv4/IPv6 or CIDR; applies on the next connection",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Switch(
                            checked = settings.splitTunnelNetworksEnabled,
                            onCheckedChange = {
                                dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                                onToggleEnabled(it)
                            },
                        )
                    }
                }
            }

            if (settings.splitTunnelNetworksEnabled) {
                item {
                    SectionCard(title = if (isChinese) "分流模式" else if (isPersian) "حالت تونل‌سازی" else "Split Tunnel Mode") {
                        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                                FilterChip(
                                    selected = settings.splitTunnelNetworksMode == SplitTunnelMode.EXCLUDE_SELECTED,
                                    onClick = {
                                        dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                                        onChangeMode(SplitTunnelMode.EXCLUDE_SELECTED)
                                    },
                                    label = { Text(if (isChinese) "绕过选定网络" else if (isPersian) "استثناء کردن شبکه‌های انتخابی" else "Bypass selected networks") },
                                )
                                FilterChip(
                                    selected = settings.splitTunnelNetworksMode == SplitTunnelMode.INCLUDE_SELECTED,
                                    onClick = {
                                        dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                                        onChangeMode(SplitTunnelMode.INCLUDE_SELECTED)
                                    },
                                    label = { Text(if (isChinese) "仅代理选定网络" else if (isPersian) "فقط شبکه‌های انتخابی" else "Only include selected") },
                                )
                            }
                            Text(
                                text = if (settings.splitTunnelNetworksMode == SplitTunnelMode.EXCLUDE_SELECTED) {
                                    if (isChinese) "除列表中的 IP 网络外，其余流量经过 VPN。" else if (isPersian) "همه ترافیک به جز شبکه‌های IP فهرست‌شده از VPN می‌گذرد."
                                    else "All traffic routes through VPN except listed IP networks."
                                } else {
                                    if (isChinese) "仅列表中的 IP 网络经过 VPN。" else if (isPersian) "فقط شبکه‌های IP فهرست‌شده از VPN می‌گذرند."
                                    else "Only listed IP networks route through VPN."
                                },
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }

                item {
                    SectionCard(title = if (isChinese) "添加 IP 地址或 CIDR 网段" else if (isPersian) "افزودن آدرس IP یا شبکه CIDR" else "Add IP Address or CIDR Network") {
                        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                OutlinedTextField(
                                    value = newEntryText,
                                    onValueChange = { newEntryText = it },
                                    modifier = Modifier.weight(1f),
                                    placeholder = {
                                        Text(if (isChinese) "例如：192.168.1.0/24 或 2001:db8::/64" else if (isPersian) "مثال: 192.168.1.0/24 یا 2001:db8::/64" else "e.g. 192.168.1.0/24 or 2001:db8::/64")
                                    },
                                    singleLine = true,
                                    isError = newEntryText.isNotBlank() && dev.opentunnel.vpn.util.Net.parseCidr(newEntryText) == null,
                                    shape = RoundedCornerShape(12.dp),
                                )
                                Button(
                                    onClick = {
                                        if (dev.opentunnel.vpn.util.Net.parseCidr(newEntryText) != null) {
                                            dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                                            onAddNetwork(newEntryText)
                                            newEntryText = ""
                                        }
                                    },
                                    enabled = dev.opentunnel.vpn.util.Net.parseCidr(newEntryText) != null,
                                    shape = RoundedCornerShape(12.dp),
                                ) {
                                    Icon(Icons.Rounded.Add, contentDescription = if (isChinese) "添加" else if (isPersian) "افزودن" else "Add")
                                }
                            }
                            Text(
                                text = if (isChinese) "仅支持数字 IP 和 CIDR，不支持域名或通配符。" else if (isPersian)
                                    "فقط IP/CIDR پشتیبانی می‌شود؛ نه دامنه یا wildcard."
                                else
                                    "Use numeric IP/CIDR rules. Domains and wildcards are unsupported.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }

                item {
                    Text(
                        text = if (isChinese) "已添加 IP 网络（${settings.splitTunnelNetworks.size}）" else if (isPersian) "شبکه‌های IP اضافه‌شده (${settings.splitTunnelNetworks.size})"
                        else "Added IP Networks (${settings.splitTunnelNetworks.size})",
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface,
                    )
                }

                if (settings.splitTunnelNetworks.isEmpty()) {
                    item {
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(16.dp),
                            colors = CardDefaults.cardColors(
                                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
                            )
                        ) {
                            Box(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(24.dp),
                                contentAlignment = Alignment.Center,
                            ) {
                                Text(
                                    text = if (isChinese) "尚未添加 IP 网络" else if (isPersian) "هنوز شبکه IP اضافه نشده است" else "No IP networks added yet",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                    }
                } else {
                    items(settings.splitTunnelNetworks.toList()) { net ->
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(14.dp),
                            colors = CardDefaults.cardColors(
                                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
                            )
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.SpaceBetween,
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.weight(1f)) {
                                    Icon(
                                        imageVector = if (net.contains("/")) Icons.Rounded.Public else Icons.Rounded.Language,
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.primary,
                                        modifier = Modifier.size(20.dp),
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = net,
                                        style = MaterialTheme.typography.bodyMedium,
                                        fontWeight = FontWeight.SemiBold,
                                    )
                                }
                                IconButton(onClick = {
                                    dev.opentunnel.vpn.util.HapticHelper.performClick(context, settings.hapticFeedbackEnabled)
                                    onRemoveNetwork(net)
                                }) {
                                    Icon(
                                        Icons.Rounded.Delete,
                                        contentDescription = if (isChinese) "移除" else if (isPersian) "حذف" else "Remove",
                                        tint = MaterialTheme.colorScheme.error.copy(alpha = 0.8f),
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
