package dev.opentunnel.vpn.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.opentunnel.vpn.data.AppLanguage
import dev.opentunnel.vpn.data.ConnectionFile
import dev.opentunnel.vpn.data.VpnProfile
import dev.opentunnel.vpn.util.Strings
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** File IO belongs to the operation; navigating away cannot publish a late result. */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ProfileManagementScreen(
    activeProfileId: String, profiles: List<VpnProfile>, appLanguage: AppLanguage,
    hapticFeedbackEnabled: Boolean,
    onSelectProfile: (String) -> Unit, onEditProfile: (String) -> Unit,
    onAddProfile: () -> Unit, onDeleteProfile: suspend (String) -> Unit,
    onExportConnection: suspend () -> String, onImportConnection: suspend (String) -> VpnProfile,
    onReorderProfiles: (List<VpnProfile>) -> Unit, onBack: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var deleting by remember { mutableStateOf<VpnProfile?>(null) }
    fun operate(action: suspend () -> Unit) {
        if (busy) return
        busy = true; error = null
        scope.launch {
            try { action() }
            catch (cancelled: CancellationException) { throw cancelled }
            catch (_: Exception) { error = "操作失败，原配置已保留；请检查文件格式、存储和权限。 / Operation failed; original profiles retained." }
            finally { busy = false }
        }
    }
    val importFile = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) operate {
            val raw = withContext(Dispatchers.IO) {
                checkNotNull(context.contentResolver.openInputStream(uri)).use { ConnectionFile.text(ConnectionFile.read(it)) }
            }
            val imported = onImportConnection(raw)
            onEditProfile(imported.id)
        }
    }
    val exportFile = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) operate {
            val raw = onExportConnection()
            withContext(Dispatchers.IO) {
                checkNotNull(context.contentResolver.openOutputStream(uri, "wt")).use { it.write(raw.toByteArray(Charsets.UTF_8)) }
            }
        }
    }
    Scaffold(topBar = {
        TopAppBar(title = { Text(Strings.profileManagementTitle(appLanguage)) },
            navigationIcon = { TextButton(onClick = onBack, enabled = !busy) { Text("返回 / Back") } })
    }) { padding ->
        LazyColumn(Modifier.fillMaxSize().padding(padding).padding(horizontal = 18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            item {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onAddProfile, enabled = !busy) { Text(Strings.addProfile(appLanguage)) }
                    OutlinedButton(onClick = { importFile.launch("*/*") }, enabled = !busy) { Text("导入 .vpn") }
                    OutlinedButton(onClick = { exportFile.launch("Lansyra.vpn") }, enabled = !busy && profiles.isNotEmpty()) { Text("导出所选") }
                }
                Text("公共配置不包含账号、密码或令牌。 / Public profiles contain no credentials.",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                if (busy) LinearProgressIndicator(Modifier.fillMaxWidth().padding(top = 8.dp))
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
            if (profiles.isEmpty()) item { Text(Strings.noProfilesYet(appLanguage)) }
            itemsIndexed(profiles, key = { _, p -> p.id }) { index, profile ->
                Card(Modifier.fillMaxWidth()) {
                    Row(Modifier.fillMaxWidth().clickable(enabled = !busy) { onSelectProfile(profile.id) }.padding(12.dp)) {
                        RadioButton(selected = profile.id == activeProfileId, enabled = !busy, onClick = { onSelectProfile(profile.id) })
                        Column(Modifier.weight(1f).padding(start = 8.dp)) {
                            Text(profile.displayName, style = MaterialTheme.typography.titleMedium)
                            Text(profile.server, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Row {
                                TextButton(onClick = { onEditProfile(profile.id) }, enabled = !busy) { Text("编辑 / Edit") }
                                TextButton(onClick = { deleting = profile }, enabled = !busy) { Text(Strings.delete(appLanguage)) }
                                TextButton(onClick = {
                                    val reordered = profiles.toMutableList()
                                    java.util.Collections.swap(reordered, index, index - 1)
                                    onReorderProfiles(reordered)
                                }, enabled = !busy && index > 0) { Text("↑") }
                            }
                        }
                    }
                }
            }
        }
    }
    deleting?.let { profile ->
        AlertDialog(onDismissRequest = { deleting = null }, title = { Text(Strings.delete(appLanguage)) },
            text = { Text(Strings.deleteProfileConfirm(appLanguage, profile.displayName)) },
            confirmButton = { TextButton(onClick = { deleting = null; operate { onDeleteProfile(profile.id) } }) { Text(Strings.delete(appLanguage)) } },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text(Strings.cancel(appLanguage)) } })
    }
}
