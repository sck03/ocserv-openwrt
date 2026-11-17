package dev.opentunnel.vpn.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.BackHandler
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Clear
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import dev.opentunnel.vpn.data.AppLanguage
import dev.opentunnel.vpn.data.VpnProfile
import dev.opentunnel.vpn.ui.components.SectionCard
import dev.opentunnel.vpn.util.HapticHelper
import dev.opentunnel.vpn.util.RememberScrollHaptic
import dev.opentunnel.vpn.util.Strings
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch

private val REPORTED_OS = listOf(
    "android" to "Android",
    "linux-64" to "Linux 64-bit",
    "linux" to "Linux 32-bit",
    "win" to "Windows",
    "mac-intel" to "macOS",
    "apple-ios" to "iOS",
)

private val PROTOCOLS = listOf(
    "anyconnect" to "Cisco AnyConnect",
    "nc" to "Juniper Network Connect",
    "pulse" to "Ivanti / Pulse Connect Secure",
    "gp" to "Palo Alto GlobalProtect",
    "f5" to "F5 BIG-IP",
    "fortinet" to "Fortinet FortiGate",
    "array" to "Array Networks",
)

private val SOFTWARE_TOKENS = listOf(
    "0" to "Disabled",
    "1" to "RSA SecurID (stoken)",
    "2" to "TOTP (Google Auth, etc.)",
    "3" to "HOTP (Counter-based)",
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileScreen(
    profile: VpnProfile,
    appLanguage: AppLanguage = AppLanguage.SYSTEM,
    hapticFeedbackEnabled: Boolean = true,
    onSave: suspend (VpnProfile) -> VpnProfile,
    onDelete: (suspend (String) -> Unit)? = null,
    onForgetCertificate: suspend (String, String) -> Unit,
    onBack: () -> Unit,
) {
    val lang = appLanguage
    val context = LocalContext.current
    var draft by remember(profile.id) { mutableStateOf(profile) }
    var baseline by remember(profile.id) { mutableStateOf(profile) }
    var busy by remember(profile.id) { mutableStateOf(false) }
    var operationError by remember(profile.id) { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    var showPassword by remember { mutableStateOf(false) }
    var showAdvanced by remember { mutableStateOf(false) }
    var showDeleteConfirm by remember { mutableStateOf(false) }
    var showDiscardConfirm by remember { mutableStateOf(false) }
    var triedSaving by remember { mutableStateOf(false) }

    val scrollState = rememberScrollState()
    RememberScrollHaptic(scrollState, hapticFeedbackEnabled)

    // Form Validation Rules
    val nameError = draft.name.trim().isBlank()
    val serverError = draft.server.trim().isBlank()
    val mtuError = draft.mtu > 0 && draft.mtu !in 576..1500
    val dpdError = draft.overrideDpdTimeout && draft.dpdSeconds !in 1..86400

    val isFormValid = !nameError && !serverError && !mtuError && !dpdError

    val handleSaveAndExit = {
        triedSaving = true
        if (isFormValid && !busy) {
            HapticHelper.performClick(context, hapticFeedbackEnabled)
            val submitted = draft
            busy = true
            operationError = null
            scope.launch {
                try {
                    val saved = onSave(submitted.copy(name = submitted.name.trim(), server = submitted.server.trim()))
                    val unchanged = draft == submitted
                    baseline = saved
                    draft = if (unchanged) saved else draft.copy(id = saved.id,
                        caCertPath = if (draft.caCertPath == submitted.caCertPath) saved.caCertPath else draft.caCertPath,
                        userCertPath = if (draft.userCertPath == submitted.userCertPath) saved.userCertPath else draft.userCertPath,
                        privateKeyPath = if (draft.privateKeyPath == submitted.privateKeyPath) saved.privateKeyPath else draft.privateKeyPath)
                    if (unchanged) onBack()
                } catch (error: CancellationException) { throw error }
                catch (error: Exception) { operationError = "Could not save. Check the fields, selected files and storage. Your edits are still here." }
                finally { busy = false }
            }
        }
    }

    val handleBackNavigation = {
        if (busy) {
            Unit
        } else if (draft == baseline) {
            onBack()
        } else if (isFormValid) {
            handleSaveAndExit()
        } else {
            showDiscardConfirm = true
        }
    }

    BackHandler(onBack = handleBackNavigation)

    val caCertLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let { draft = draft.copy(caCertPath = it.toString()) }
    }
    val userCertLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let { draft = draft.copy(userCertPath = it.toString()) }
    }
    val privateKeyLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let { draft = draft.copy(privateKeyPath = it.toString()) }
    }

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = { Text(Strings.editProfileTitle(lang, draft.name)) },
                navigationIcon = {
                    IconButton(onClick = handleBackNavigation, enabled = !busy) {
                        Icon(Icons.Rounded.ArrowBack, contentDescription = "Back")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = Color.Transparent,
                ),
                actions = {
                    if (draft.id.isNotBlank() && onDelete != null) {
                        IconButton(onClick = { showDeleteConfirm = true }, enabled = !busy) {
                            Icon(
                                Icons.Rounded.Delete,
                                contentDescription = Strings.delete(lang),
                                tint = MaterialTheme.colorScheme.error,
                            )
                        }
                    }
                    TextButton(
                        onClick = handleSaveAndExit,
                        enabled = !busy && (isFormValid || !triedSaving),
                    ) {
                        Icon(Icons.Rounded.Check, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.size(6.dp))
                        Text(Strings.save(lang), fontWeight = FontWeight.Bold)
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(scrollState)
                .padding(horizontal = 18.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            operationError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            // ── Section 1: Essential Connection Information ──────────────────
            SectionCard(title = if (Strings.isRtl(lang)) "مشخصات سرور و پروتکل" else "Essential Connection") {
                Column(
                    Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                ) {
                    OutlinedTextField(
                        value = draft.name,
                        enabled = !busy,
                        onValueChange = { draft = draft.copy(name = it) },
                        label = { Text(Strings.profileName(lang)) },
                        placeholder = { Text("e.g. Work VPN") },
                        singleLine = true,
                        isError = triedSaving && nameError,
                        supportingText = if (triedSaving && nameError) {
                            { Text(Strings.fieldRequired(lang), color = MaterialTheme.colorScheme.error) }
                        } else null,
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    OutlinedTextField(
                        value = draft.server,
                        enabled = !busy,
                        onValueChange = { draft = draft.copy(server = it) },
                        label = { Text(Strings.serverAddress(lang)) },
                        placeholder = { Text("vpn.example.com") },
                        singleLine = true,
                        isError = triedSaving && serverError,
                        supportingText = if (triedSaving && serverError) {
                            { Text(Strings.fieldRequired(lang), color = MaterialTheme.colorScheme.error) }
                        } else null,
                        keyboardOptions = KeyboardOptions(
                            keyboardType = KeyboardType.Uri,
                            imeAction = ImeAction.Next,
                        ),
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    LabelledDropdown(
                        label = Strings.vpnProtocol(lang),
                        enabled = !busy,
                        options = PROTOCOLS,
                        selected = draft.protocol,
                        onSelected = { draft = draft.copy(protocol = it) },
                    )
                }
            }

            // ── Section 2: Credentials & Authentication ─────────────────────
            SectionCard(title = if (Strings.isRtl(lang)) "احراز هویت و گواهی‌ها" else "Credentials & Security") {
                Column(
                    Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                ) {
                    OutlinedTextField(
                        value = draft.username,
                        enabled = !busy,
                        onValueChange = { draft = draft.copy(username = it) },
                        label = { Text(Strings.username(lang)) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    OutlinedTextField(
                        value = draft.password,
                        enabled = !busy,
                        onValueChange = { draft = draft.copy(password = it) },
                        label = { Text(Strings.password(lang)) },
                        supportingText = { Text("Leave blank to keep the saved password. Turn off Save password to remove it.") },
                        singleLine = true,
                        visualTransformation = if (showPassword) VisualTransformation.None else PasswordVisualTransformation(),
                        keyboardOptions = KeyboardOptions(
                            keyboardType = KeyboardType.Password,
                            imeAction = ImeAction.Done,
                        ),
                        trailingIcon = {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                if (draft.password.isNotEmpty()) {
                                    IconButton(onClick = { draft = draft.copy(password = "") }, enabled = !busy) {
                                        Icon(
                                            imageVector = Icons.Rounded.Clear,
                                            contentDescription = "Clear password",
                                        )
                                    }
                                }
                                IconButton(onClick = { showPassword = !showPassword }) {
                                    Icon(
                                        imageVector = if (showPassword) Icons.Rounded.VisibilityOff else Icons.Rounded.Visibility,
                                        contentDescription = if (showPassword) "Hide password" else "Show password",
                                    )
                                }
                            }
                        },
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    LabelledDropdown(
                        label = Strings.softwareToken(lang),
                        enabled = !busy,
                        options = SOFTWARE_TOKENS,
                        selected = draft.softwareTokenMode.toString(),
                        onSelected = { draft = draft.copy(softwareTokenMode = it.toIntOrNull() ?: 0) },
                    )

                    if (draft.softwareTokenMode > 0) {
                        OutlinedTextField(
                            value = draft.tokenString,
                            enabled = !busy,
                            onValueChange = { draft = draft.copy(tokenString = it) },
                            label = { Text(Strings.tokenString(lang)) },
                            supportingText = { Text("Leave blank to keep the saved token. Disable software token to remove it.") },
                            singleLine = true,
                            shape = MaterialTheme.shapes.small,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }

                    CertificateField(Strings.caCertificate(lang), draft.caCertPath.isNotEmpty(), !busy,
                        onSelect = { caCertLauncher.launch("*/*") }, onClear = { draft = draft.copy(caCertPath = "") })
                    CertificateField(Strings.userCertificate(lang), draft.userCertPath.isNotEmpty(), !busy,
                        onSelect = { userCertLauncher.launch("*/*") }, onClear = { draft = draft.copy(userCertPath = "") })
                    CertificateField(Strings.privateKey(lang), draft.privateKeyPath.isNotEmpty(), !busy,
                        onSelect = { privateKeyLauncher.launch("*/*") }, onClear = { draft = draft.copy(privateKeyPath = "") })

                    CheckboxLine(
                        title = "Save password",
                        enabled = !busy,
                        subtitle = "Encrypt the password on this device. Turn off to remove the saved password.",
                        checked = draft.savePassword,
                        hapticEnabled = hapticFeedbackEnabled,
                        onCheckedChange = { draft = draft.copy(savePassword = it) },
                    )
                }
            }

            // ── Section 3: Advanced Network & Transport (Collapsible) ────────
            TextButton(
                onClick = { showAdvanced = !showAdvanced },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(
                    imageVector = if (showAdvanced) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                    contentDescription = null,
                )
                Spacer(Modifier.size(8.dp))
                Text(
                    if (showAdvanced) Strings.hideAdvancedOptions(lang) else Strings.advancedOptions(lang),
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold,
                )
            }

            AnimatedVisibility(visible = showAdvanced) {
                Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    SectionCard(title = if (Strings.isRtl(lang)) "تنظیمات کانال انتقال داده" else "Transport & Network") {
                        Column(Modifier.padding(vertical = 4.dp)) {
                            ToggleLine(
                                title = "Use DTLS (UDP)",
                                enabled = !busy,
                                subtitle = "Fast UDP data channel when network allows",
                                checked = draft.enableDtls,
                                onCheckedChange = { draft = draft.copy(enableDtls = it) },
                            )
                            ToggleLine(
                                title = "Enable IPv6",
                                enabled = !busy,
                                subtitle = "Turn off if your gateway advertises broken IPv6",
                                checked = draft.enableIpv6,
                                onCheckedChange = { draft = draft.copy(enableIpv6 = it) },
                            )
                            ToggleLine(
                                title = "Allow legacy ciphers",
                                enabled = !busy,
                                subtitle = "Only for very old gateways with outdated crypto",
                                checked = draft.allowInsecureCrypto,
                                onCheckedChange = { draft = draft.copy(allowInsecureCrypto = it) },
                            )

                            Column(
                                Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalArrangement = Arrangement.spacedBy(14.dp),
                            ) {
                                NumberField(
                                    label = "MTU override (576 - 1500)",
                                    enabled = !busy,
                                    value = draft.mtu,
                                    placeholder = "Automatic",
                                    isError = mtuError,
                                    supportingText = if (mtuError) Strings.invalidMtuRange(lang) else null,
                                    onValueChange = { draft = draft.copy(mtu = it) },
                                )
                            }
                        }
                    }

                    SectionCard(title = if (Strings.isRtl(lang)) "پارامترهای پیشرفته هندشیک" else "Handshake & Protocol Tuning") {
                        Column(
                            Modifier.padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(14.dp),
                        ) {
                            LabelledDropdown(
                                label = Strings.reportedOs(lang),
                                enabled = !busy,
                                options = REPORTED_OS,
                                selected = draft.reportedOs,
                                onSelected = { draft = draft.copy(reportedOs = it) },
                            )

                            CheckboxLine(
                                title = Strings.disableXmlPost(lang),
                                enabled = !busy,
                                subtitle = Strings.disableXmlPostSub(lang),
                                checked = draft.disableXmlPost,
                                hapticEnabled = hapticFeedbackEnabled,
                                onCheckedChange = { draft = draft.copy(disableXmlPost = it) },
                            )

                            CheckboxLine(
                                title = Strings.requirePfs(lang),
                                enabled = !busy,
                                subtitle = Strings.requirePfsSub(lang),
                                checked = draft.requirePfs,
                                hapticEnabled = hapticFeedbackEnabled,
                                onCheckedChange = { draft = draft.copy(requirePfs = it) },
                            )

                            CheckboxLine(
                                title = Strings.overrideDpdTimeout(lang),
                                enabled = !busy,
                                subtitle = Strings.overrideDpdTimeoutSub(lang),
                                checked = draft.overrideDpdTimeout,
                                hapticEnabled = hapticFeedbackEnabled,
                                onCheckedChange = { draft = draft.copy(overrideDpdTimeout = it) },
                            )

                            if (draft.overrideDpdTimeout) {
                                NumberField(
                                    label = Strings.dpdSeconds(lang),
                                    enabled = !busy,
                                    value = draft.dpdSeconds,
                                    placeholder = "30",
                                    isError = dpdError,
                                    supportingText = if (dpdError) "Use 1 to 86400 seconds" else null,
                                    onValueChange = { draft = draft.copy(dpdSeconds = it) },
                                )
                            }

                            OutlinedTextField(
                                value = draft.userAgent,
                                enabled = !busy,
                                onValueChange = { draft = draft.copy(userAgent = it) },
                                label = { Text("User agent override (optional)") },
                                singleLine = true,
                                shape = MaterialTheme.shapes.small,
                                modifier = Modifier.fillMaxWidth(),
                            )
                        }
                    }

                    if (draft.trustedCertificate.isNotBlank()) {
                        SectionCard(title = "Pinned Certificate") {
                            Column(Modifier.padding(16.dp)) {
                                Text(
                                    text = draft.trustedCertificate,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                                Spacer(Modifier.height(10.dp))
                                Button(
                                    onClick = {
                                        busy = true
                                        operationError = null
                                        val id = profile.id
                                        val server = profile.server
                                        scope.launch {
                                            try {
                                                onForgetCertificate(id, server)
                                                draft = draft.copy(trustedCertificate = "")
                                                baseline = baseline.copy(trustedCertificate = "")
                                            } catch (error: CancellationException) { throw error }
                                            catch (error: Exception) { operationError = "Could not forget the certificate. The saved trust was preserved." }
                                            finally { busy = false }
                                        }
                                    },
                                    enabled = !busy,
                                    shape = MaterialTheme.shapes.small,
                                ) {
                                    Text("Forget this certificate")
                                }
                            }
                        }
                    }
                }
            }

            Spacer(Modifier.height(32.dp))
        }
    }

    if (showDeleteConfirm && onDelete != null) {
        AlertDialog(
            onDismissRequest = { showDeleteConfirm = false },
            title = { Text(Strings.delete(lang)) },
            text = { Text(Strings.deleteProfileConfirm(lang, draft.displayName)) },
            confirmButton = {
                TextButton(onClick = {
                    busy = true
                    operationError = null
                    showDeleteConfirm = false
                    scope.launch {
                        try { onDelete(draft.id); onBack() }
                        catch (error: CancellationException) { throw error }
                        catch (error: Exception) { operationError = "Could not delete this profile. Your edits are still here." }
                        finally { busy = false }
                    }
                }) {
                    Text(Strings.delete(lang), color = MaterialTheme.colorScheme.error, fontWeight = FontWeight.Bold)
                }
            },
            dismissButton = {
                TextButton(onClick = { showDeleteConfirm = false }) {
                    Text(Strings.cancel(lang))
                }
            },
        )
    }

    if (showDiscardConfirm) {
        AlertDialog(
            onDismissRequest = { showDiscardConfirm = false },
            title = { Text(if (Strings.isRtl(lang)) "تغییرات ذخیره نشده" else "Unsaved Changes") },
            text = {
                Text(
                    if (Strings.isRtl(lang)) "پروفایل دارای خطای اعتبارسنجی است. آیا مایل به لغو تغییرات و بازگشت هستید؟"
                    else "The profile has incomplete or invalid fields. Discard changes and leave?"
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    showDiscardConfirm = false
                    onBack()
                }) {
                    Text(
                        if (Strings.isRtl(lang)) "لغو تغییرات" else "Discard",
                        color = MaterialTheme.colorScheme.error,
                        fontWeight = FontWeight.Bold,
                    )
                }
            },
            dismissButton = {
                TextButton(onClick = { showDiscardConfirm = false }) {
                    Text(if (Strings.isRtl(lang)) "ادامه ویرایش" else "Keep Editing")
                }
            },
        )
    }
}

@Composable
private fun CertificateField(label: String, selected: Boolean, enabled: Boolean, onSelect: () -> Unit, onClear: () -> Unit) {
    OutlinedTextField(
        value = if (selected) "Selected file" else "",
        onValueChange = {},
        readOnly = true,
        enabled = enabled,
        label = { Text(label) },
        placeholder = { Text("Choose a file") },
        trailingIcon = {
            Row {
                if (selected) IconButton(onClick = onClear, enabled = enabled) {
                    Icon(Icons.Rounded.Clear, contentDescription = "Remove $label")
                }
                IconButton(onClick = onSelect, enabled = enabled) {
                    Icon(Icons.Rounded.FolderOpen, contentDescription = "Select $label")
                }
            }
        },
        singleLine = true,
        shape = MaterialTheme.shapes.small,
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun CheckboxLine(
    title: String,
    subtitle: String,
    checked: Boolean,
    enabled: Boolean = true,
    hapticEnabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit,
) {
    val context = LocalContext.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = enabled) {
                HapticHelper.performClick(context, hapticEnabled)
                onCheckedChange(!checked)
            }
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Checkbox(
            checked = checked,
            enabled = enabled,
            onCheckedChange = {
                HapticHelper.performClick(context, hapticEnabled)
                onCheckedChange(it)
            },
        )
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun ToggleLine(
    title: String,
    subtitle: String,
    checked: Boolean,
    enabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit,
) {
    val context = LocalContext.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(enabled = enabled) {
                HapticHelper.performClick(context, true)
                onCheckedChange(!checked)
            }
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
            Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Spacer(Modifier.width(12.dp))
        Switch(
            checked = checked,
            enabled = enabled,
            onCheckedChange = {
                HapticHelper.performClick(context, true)
                onCheckedChange(it)
            },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LabelledDropdown(
    label: String,
    options: List<Pair<String, String>>,
    selected: String,
    enabled: Boolean = true,
    onSelected: (String) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val display = options.firstOrNull { it.first == selected }?.second ?: selected

    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { if (enabled) expanded = it },
        modifier = Modifier.fillMaxWidth(),
    ) {
        OutlinedTextField(
            value = display,
            enabled = enabled,
            onValueChange = {},
            readOnly = true,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            shape = MaterialTheme.shapes.small,
            modifier = Modifier
                .menuAnchor()
                .fillMaxWidth(),
        )
        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
        ) {
            options.forEach { (value, name) ->
                DropdownMenuItem(
                    text = { Text(name) },
                    enabled = enabled,
                    onClick = {
                        onSelected(value)
                        expanded = false
                    },
                )
            }
        }
    }
}

@Composable
private fun NumberField(
    label: String,
    value: Int,
    placeholder: String,
    enabled: Boolean = true,
    isError: Boolean = false,
    supportingText: String? = null,
    onValueChange: (Int) -> Unit,
) {
    OutlinedTextField(
        value = if (value > 0) value.toString() else "",
        enabled = enabled,
        onValueChange = { input ->
            val digits = input.filter { it.isDigit() }.take(5)
            onValueChange(digits.toIntOrNull() ?: 0)
        },
        label = { Text(label) },
        placeholder = { Text(placeholder) },
        singleLine = true,
        isError = isError,
        supportingText = if (isError && supportingText != null) {
            { Text(supportingText, color = MaterialTheme.colorScheme.error) }
        } else null,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        shape = MaterialTheme.shapes.small,
        modifier = Modifier.fillMaxWidth(),
    )
}
