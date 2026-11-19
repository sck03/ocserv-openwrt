package dev.opentunnel.vpn.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.BackHandler
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
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
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
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
import dev.opentunnel.vpn.data.ProfileEdit
import dev.opentunnel.vpn.data.ProfileField
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

private val SOFTWARE_TOKENS by lazy {
    listOf("0" to "Disabled") + if (dev.opentunnel.vpn.core.NativeLibrary.isAvailable &&
        org.infradead.libopenconnect.LibOpenConnect.hasOATHSupport())
        listOf("2" to "TOTP", "3" to "HOTP") else emptyList()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileScreen(
    profile: VpnProfile,
    appLanguage: AppLanguage = AppLanguage.SYSTEM,
    hapticFeedbackEnabled: Boolean = true,
    onSave: suspend (ProfileEdit) -> VpnProfile,
    onDelete: (suspend (String) -> Unit)? = null,
    onForgetCertificate: suspend (String, String) -> Unit,
    onBack: () -> Unit,
) {
    val lang = appLanguage
    val context = LocalContext.current
    var draft by remember(profile.id) { mutableStateOf(profile) }
    var dnsText by remember(profile.id) { mutableStateOf(profile.customDnsServers.joinToString("\n")) }
    var baseline by remember(profile.id) { mutableStateOf(profile) }
    var editedFields by remember(profile.id) { mutableStateOf(emptySet<ProfileField>()) }
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
    val dnsError = draft.customDnsServers.size > 3 || draft.customDnsServers.any { !dev.opentunnel.vpn.util.Net.isValidIp(it) }

    val isFormValid = !nameError && !serverError && !mtuError && !dpdError && !dnsError

    val handleSaveAndExit = {
        triedSaving = true
        if (isFormValid && !busy) {
            HapticHelper.performClick(context, hapticFeedbackEnabled)
            val submitted = draft
            busy = true
            operationError = null
            scope.launch {
                try {
                    val saved = onSave(ProfileEdit(submitted.copy(name = submitted.name.trim(), server = submitted.server.trim()), editedFields))
                    val unchanged = draft == submitted
                    baseline = saved
                    draft = if (unchanged) saved else draft.copy(id = saved.id,
                        caCertPath = if (draft.caCertPath == submitted.caCertPath) saved.caCertPath else draft.caCertPath,
                        userCertPath = if (draft.userCertPath == submitted.userCertPath) saved.userCertPath else draft.userCertPath,
                        privateKeyPath = if (draft.privateKeyPath == submitted.privateKeyPath) saved.privateKeyPath else draft.privateKeyPath)
                    if (unchanged) onBack()
                } catch (error: CancellationException) { throw error }
                catch (error: Exception) { operationError = Strings.profileSaveFailed(lang) }
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
        uri?.let { editedFields += setOf(ProfileField.CA, ProfileField.PIN); draft = draft.copy(caCertPath = it.toString(), trustedCertificate = "") }
    }
    val userCertLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let { editedFields += ProfileField.CERTIFICATE; draft = draft.copy(userCertPath = it.toString()) }
    }
    val privateKeyLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let { editedFields += ProfileField.KEY; draft = draft.copy(privateKeyPath = it.toString()) }
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
            SectionCard(title = Strings.essentialConnection(lang)) {
                Column(
                    Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                ) {
                    OutlinedTextField(
                        value = draft.name,
                        enabled = !busy,
                        onValueChange = { draft = draft.copy(name = it) },
                        label = { Text(Strings.profileName(lang)) },
                        placeholder = { Text(Strings.profileNameExample(lang)) },
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
                        supportingText = { Text(if (triedSaving && serverError) Strings.fieldRequired(lang) else Strings.serverCredentials(lang)) },
                        keyboardOptions = KeyboardOptions(
                            keyboardType = KeyboardType.Uri,
                            imeAction = ImeAction.Next,
                        ),
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    ProfileDropdown(
                        label = Strings.vpnProtocol(lang),
                        enabled = !busy,
                        options = PROTOCOLS,
                        selected = draft.protocol,
                        onSelected = { draft = draft.copy(protocol = it) },
                    )
                }
            }

            // ── Section 2: Credentials & Authentication ─────────────────────
            SectionCard(title = Strings.credentialsSecurity(lang)) {
                Column(
                    Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp),
                ) {
                    OutlinedTextField(
                        value = draft.username,
                        enabled = !busy,
                        onValueChange = { editedFields += ProfileField.USERNAME; draft = draft.copy(username = it) },
                        label = { Text(Strings.username(lang)) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    OutlinedTextField(
                        value = draft.authGroup,
                        enabled = !busy,
                        onValueChange = { editedFields += ProfileField.GROUP; draft = draft.copy(authGroup = it) },
                        label = { Text(Strings.authGroup(lang)) },
                        singleLine = true,
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    OutlinedTextField(
                        value = draft.password,
                        enabled = !busy,
                        onValueChange = { editedFields += ProfileField.PASSWORD; draft = draft.copy(password = it) },
                        label = { Text(Strings.password(lang)) },
                        supportingText = { Text(Strings.savedPasswordHint(lang)) },
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
                                            contentDescription = Strings.clearPassword(lang),
                                        )
                                    }
                                }
                                IconButton(onClick = { showPassword = !showPassword }) {
                                    Icon(
                                        imageVector = if (showPassword) Icons.Rounded.VisibilityOff else Icons.Rounded.Visibility,
                                        contentDescription = if (showPassword) Strings.hidePassword(lang) else Strings.showPassword(lang),
                                    )
                                }
                            }
                        },
                        shape = MaterialTheme.shapes.small,
                        modifier = Modifier.fillMaxWidth(),
                    )

                    ProfileDropdown(
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
                            onValueChange = { editedFields += ProfileField.TOKEN; draft = draft.copy(tokenString = it) },
                            label = { Text(Strings.tokenString(lang)) },
                            supportingText = { Text(Strings.savedTokenHint(lang)) },
                            singleLine = true,
                            shape = MaterialTheme.shapes.small,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }

                    ProfileCertificateField(lang, Strings.caCertificate(lang), draft.caCertPath.isNotEmpty(), !busy,
                        onSelect = { caCertLauncher.launch("*/*") }, onClear = { editedFields += ProfileField.CA; draft = draft.copy(caCertPath = "") })
                    ProfileCertificateField(lang, Strings.userCertificate(lang), draft.userCertPath.isNotEmpty(), !busy,
                        onSelect = { userCertLauncher.launch("*/*") }, onClear = { editedFields += ProfileField.CERTIFICATE; draft = draft.copy(userCertPath = "") })
                    ProfileCertificateField(lang, Strings.privateKey(lang), draft.privateKeyPath.isNotEmpty(), !busy,
                        onSelect = { privateKeyLauncher.launch("*/*") }, onClear = { editedFields += ProfileField.KEY; draft = draft.copy(privateKeyPath = "") })

                    ProfileCheckboxLine(
                        title = Strings.savePassword(lang),
                        enabled = !busy,
                        subtitle = Strings.savePasswordHint(lang),
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
                    SectionCard(title = Strings.transportNetwork(lang)) {
                        Column(Modifier.padding(vertical = 4.dp)) {
                            ProfileToggleLine(
                                title = Strings.useDtls(lang),
                                enabled = !busy,
                                subtitle = Strings.useDtlsHint(lang),
                                checked = draft.enableDtls,
                                onCheckedChange = { draft = draft.copy(enableDtls = it) },
                            )
                            ProfileToggleLine(
                                title = Strings.enableIpv6(lang),
                                enabled = !busy,
                                subtitle = Strings.enableIpv6Hint(lang),
                                checked = draft.enableIpv6,
                                onCheckedChange = { draft = draft.copy(enableIpv6 = it) },
                            )
                            ProfileToggleLine(
                                title = Strings.allowLegacyCiphers(lang),
                                enabled = !busy,
                                subtitle = Strings.legacyCiphersHint(lang),
                                checked = draft.allowInsecureCrypto,
                                onCheckedChange = { draft = draft.copy(allowInsecureCrypto = it) },
                            )

                            Column(
                                Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalArrangement = Arrangement.spacedBy(14.dp),
                            ) {
                                OutlinedTextField(
                                    value = dnsText,
                                    enabled = !busy,
                                    onValueChange = { text -> dnsText = text; draft = draft.copy(customDnsServers = text.lines().map(String::trim).filter(String::isNotEmpty)) },
                                    isError = triedSaving && dnsError,
                                    label = { Text(Strings.customDns(lang)) },
                                    supportingText = { Text(Strings.customDnsHint(lang)) },
                                    modifier = Modifier.fillMaxWidth(),
                                )
                                ProfileNumberField(
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

                    SectionCard(title = Strings.protocolTuning(lang)) {
                        Column(
                            Modifier.padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(14.dp),
                        ) {
                            ProfileDropdown(
                                label = Strings.reportedOs(lang),
                                enabled = !busy,
                                options = REPORTED_OS,
                                selected = draft.reportedOs,
                                onSelected = { draft = draft.copy(reportedOs = it) },
                            )

                            ProfileCheckboxLine(
                                title = Strings.disableXmlPost(lang),
                                enabled = !busy,
                                subtitle = Strings.disableXmlPostSub(lang),
                                checked = draft.disableXmlPost,
                                hapticEnabled = hapticFeedbackEnabled,
                                onCheckedChange = { draft = draft.copy(disableXmlPost = it) },
                            )

                            ProfileCheckboxLine(
                                title = Strings.requirePfs(lang),
                                enabled = !busy,
                                subtitle = Strings.requirePfsSub(lang),
                                checked = draft.requirePfs,
                                hapticEnabled = hapticFeedbackEnabled,
                                onCheckedChange = { draft = draft.copy(requirePfs = it) },
                            )

                            ProfileCheckboxLine(
                                title = Strings.overrideDpdTimeout(lang),
                                enabled = !busy,
                                subtitle = Strings.overrideDpdTimeoutSub(lang),
                                checked = draft.overrideDpdTimeout,
                                hapticEnabled = hapticFeedbackEnabled,
                                onCheckedChange = { draft = draft.copy(overrideDpdTimeout = it) },
                            )

                            if (draft.overrideDpdTimeout) {
                                ProfileNumberField(
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
                                label = { Text(Strings.userAgentOverride(lang)) },
                                singleLine = true,
                                shape = MaterialTheme.shapes.small,
                                modifier = Modifier.fillMaxWidth(),
                            )
                        }
                    }

                    SectionCard(title = Strings.serverPin(lang)) {
                        OutlinedTextField(
                            value = draft.trustedCertificate,
                            enabled = !busy,
                            singleLine = true,
                            onValueChange = { pin ->
                                editedFields += setOf(ProfileField.PIN, ProfileField.CA)
                                draft = draft.copy(trustedCertificate = pin.trim(), caCertPath = if (pin.isBlank()) draft.caCertPath else "")
                            },
                            label = { Text("pin-sha256:…") },
                            supportingText = { Text(Strings.serverPinHint(lang)) },
                            modifier = Modifier.padding(16.dp).fillMaxWidth(),
                        )
                    }
                    if (draft.trustedCertificate.isNotBlank()) {
                        SectionCard(title = Strings.pinnedCertificate(lang)) {
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
                                            catch (error: Exception) { operationError = Strings.forgetCertificateFailed(lang) }
                                            finally { busy = false }
                                        }
                                    },
                                    enabled = !busy,
                                    shape = MaterialTheme.shapes.small,
                                ) {
                                    Text(Strings.forgetCertificate(lang))
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
                        catch (error: Exception) { operationError = Strings.deleteProfileFailed(lang) }
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
            title = { Text(Strings.unsavedChanges(lang)) },
            text = {
                Text(
                    Strings.discardInvalidProfile(lang)
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    showDiscardConfirm = false
                    onBack()
                }) {
                    Text(
                        Strings.discardChanges(lang),
                        color = MaterialTheme.colorScheme.error,
                        fontWeight = FontWeight.Bold,
                    )
                }
            },
            dismissButton = {
                TextButton(onClick = { showDiscardConfirm = false }) {
                    Text(Strings.keepEditing(lang))
                }
            },
        )
    }
}
