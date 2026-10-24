"""Lifecycle, storage and diagnostics adaptations kept separate from build/branding edits."""
import shutil
from source_edits import once, section


def maintain(app, root):
    source = app / "app/src/main/java/dev/opentunnel/vpn"

    def edit(name, transform):
        path = source / name
        path.write_text(transform(path.read_text(encoding="utf-8")), encoding="utf-8")

    def runner(value):
        value = once(value, '    fun persistCertificatePin(fingerprint: String)',
                     '    fun persistCertificatePin(profileID: String, server: String, fingerprint: String)')
        value = once(value, '    fun onTunnelFinished(error: String?)',
                     '    fun onTunnelFinished(source: TunnelRunner, error: String?)')
        value = once(value, 'host.onTunnelFinished(if (disconnectRequested.get()) null else failure)',
                     'password = ""\n            host.onTunnelFinished(this, if (disconnectRequested.get()) null else failure)')
        value = once(value, 'host.persistCertificatePin(fingerprint)',
                     'host.persistCertificatePin(profile.id, profile.server, fingerprint)')
        value = once(value, '    private var passwordConsumed = false', '''    init {
        for (secret in listOf(profile.username, profile.password, profile.authGroup, profile.tokenString)) VpnBus.remember(secret)
    }

    fun abort() {
        requestDisconnect()
        closeTun() // Break a stuck TUN read; the worker alone destroys the native core.
    }

    private var passwordConsumed = false''')
        value = once(value, '    private fun closeTun() {', '    @Synchronized private fun closeTun() {')
        value = once(value, '        val previous = tunFd\n        tunFd = descriptor\n        previous?.close()', '''        synchronized(this) {
            if (disconnectRequested.get()) {
                descriptor.close()
                error("Connection canceled")
            }
            val previous = tunFd
            tunFd = descriptor
            previous?.close()
        }''')
        value = once(value, '        VpnBus.setStage(ConnectionStage.CONNECTING)\n',
                     '        VpnBus.remember(lib.getCookie())\n        password = ""\n        VpnBus.setStage(ConnectionStage.CONNECTING)\n')
        value = once(value, '        private var acceptedPin = ""', '''        private var acceptedPin = ""
        private var authAttempts = 0
        private fun sameServer(): Boolean {
            val lib = session ?: return false
            val expected = java.net.URI(ConnectionPolicy.serverUrl(profile.server))
            return expected.host.trim('[', ']').equals(lib.getDNSName()?.trim('[', ']'), ignoreCase = true) &&
                (if (expected.port < 0) 443 else expected.port) == lib.getPort()
        }''')
        value = once(value, '            if (!certificateMatches()) {',
                     '            form.opts.orEmpty().forEach { VpnBus.remember(it.value) }\n'
                     '            if (++authAttempts > 64 || form.opts.orEmpty().size > 256 || !sameServer() || !certificateMatches()) {')
        for expression in ('profile.username', 'password', 'value'):
            value = once(value, f'                        opt.value = {expression}',
                         f'                        VpnBus.remember({expression})\n                        opt.value = {expression}')
        return value

    edit("core/TunnelRunner.kt", runner)

    def service(value):
        value = once(value, 'import dev.opentunnel.vpn.core.ConnectionStage',
                     'import dev.opentunnel.vpn.core.SessionOwner\nimport android.os.Handler\nimport android.os.Looper\n'
                     'import kotlinx.coroutines.CancellationException\nimport dev.opentunnel.vpn.core.ConnectionStage')
        value = section(value, '    @Volatile\n    private var runner:', '    private var statsJob:', '''    private val runner: TunnelRunner? get() = sessions.current()
    private var startupJob: Job? = null
    private var ownerWait: Job? = null
    private val mainHandler = Handler(Looper.getMainLooper())

''')
        value = section(value, '    private fun startTunnel() {', '    private fun stopSelfSafely() {', '''    private fun startTunnel() {
        val ticket = sessions.begin()
        if (ticket == null) {
            // A restarted Android service may inherit a live worker owned by its
            // predecessor. Keep the foreground contract until that lease ends.
            goForeground()
            if (ownerWait?.isActive != true) ownerWait = scope.launch {
                while (sessions.busy() && isActive) delay(250L)
                if (isActive) stopSelfSafely()
            }
            return
        }
        VpnBus.reset()
        VpnBus.setStage(ConnectionStage.PREPARING)
        goForeground()
        acquireWakeLock()
        startupJob = scope.launch {
            try {
                val profile = repository.currentProfile()
                val settings = repository.currentSettings()
                if (!isActive) return@launch
                check(profile.isComplete) { "Add a server address and username before connecting." }
                check(NativeLibrary.isAvailable) { NativeLibrary.MISSING_MESSAGE }
                reconnectOnNetworkChange = settings.reconnectOnNetworkChange
                showStatsInNotification = settings.showStatsInNotification
                appLanguage = settings.appLanguage
                val tunnel = TunnelRunner(this@OpenTunnelVpnService, profile, settings)
                if (!sessions.attach(ticket, tunnel)) return@launch
                try {
                    startNetworkMonitoring()
                    startStatsPolling()
                    startPingPolling()
                    tunnel.start()
                } catch (error: Exception) {
                    val completion = sessions.finish(tunnel)
                    if (completion != null && sessions.idle(completion)) {
                        VpnBus.setError("Could not start the VPN worker; previous settings were preserved.")
                        stopSelfSafely()
                    }
                }
            } catch (error: CancellationException) {
                if (sessions.cancelStart(ticket)) stopSelfSafely()
                throw error
            } catch (error: Exception) {
                if (sessions.cancelStart(ticket)) {
                    VpnBus.setError("Cannot start VPN. Check the profile, storage and native core.")
                    stopSelfSafely()
                }
            }
        }
    }

    private fun stopTunnel() {
        val tunnel = runner
        if (tunnel == null) {
            sessions.cancelStart()
            startupJob?.cancel()
            stopSelfSafely()
            return
        }
        tunnel.requestDisconnect()
        scope.launch {
            delay(FORCE_STOP_AFTER_MS)
            if (sessions.owns(tunnel)) {
                VpnBus.info("Waiting for the previous tunnel to finish cleanup")
                tunnel.abort()
            }
        }
    }

''')
        value = once(value, '        runner?.requestDisconnect()\n        releaseWakeLock()',
                     '        sessions.cancelStart()\n        startupJob?.cancel()\n        runner?.requestDisconnect()\n        pingJob?.cancel()\n        releaseWakeLock()')
        value = section(value, '    override fun persistCertificatePin(', '    companion object {', '''    override fun persistCertificatePin(profileID: String, server: String, fingerprint: String) {
        scope.launch {
            try { repository.pinCertificate(profileID, server, fingerprint) }
            catch (error: Exception) { VpnBus.error("Could not save the server certificate; original profiles were preserved") }
        }
    }

    override fun onTunnelFinished(source: TunnelRunner, error: String?) {
        val completion = sessions.finish(source) ?: return
        // This must also release a lease after the old service's scope was canceled.
        mainHandler.post {
            if (!sessions.idle(completion)) return@post
            locationAttemptCount = 0
            locationJob?.cancel()
            locationJob = null
            if (error != null) {
                VpnBus.setError(error)
                Notifications.postActionRequired(this@OpenTunnelVpnService, "VPN connection failed; see the diagnostic log.")
            } else {
                VpnBus.reset()
                Notifications.clearActionRequired(this@OpenTunnelVpnService)
            }
            stopSelfSafely()
        }
    }

''')
        value = once(value, '    companion object {', '    companion object {\n        private val sessions = SessionOwner<TunnelRunner>()')
        value = once(value, '                delay(STATS_INTERVAL_MS)',
                     '                delay(if (getSystemService<PowerManager>()?.isInteractive == true) STATS_INTERVAL_MS else 5_000L)')
        value = once(value, '                if (VpnBus.status.value.stage == ConnectionStage.CONNECTED) {\n                    val pingMs = measurePing()',
                     '                if (getSystemService<PowerManager>()?.isInteractive == true && VpnBus.status.value.stage == ConnectionStage.CONNECTED) {\n                    val pingMs = measurePing()')
        value = once(value, 'private const val PING_INTERVAL_MS    = 4_000L', 'private const val PING_INTERVAL_MS    = 30_000L')
        value = once(value, 'on slow networks before the hard kill path fires.', 'on slow networks before closing TUN; ownership lasts until the worker exits.')
        return value

    edit("service/OpenTunnelVpnService.kt", service)

    def bus(value):
        value = once(value, '    private const val MAX_LOG_LINES = 800', '''    private const val MAX_LOG_LINES = 800
    private const val MAX_LOG_CHARACTERS = 256 * 1024
    private var logCharacters = 0
    private var lastLogPublish = 0L
    private var publishPending = false
    private val publisher by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }
    private val diagnostic = DiagnosticLog()
    fun remember(value: String?) = diagnostic.remember(value)''')
        value = once(value, '    fun reset() {', '    fun reset() {\n        diagnostic.clear()')
        value = once(value, '    fun setError(message: String) {', '    fun setError(message: String) {\n        val safeMessage = diagnostic.clean(message)')
        value = once(value, '                error = message,', '                error = safeMessage,')
        value = section(value, '    fun log(level: LogLevel, message: String) {', '    fun info(message: String)', '''    fun log(level: LogLevel, message: String) {
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

''')
        value = once(value, '            logDeque.clear()', '            logDeque.clear()\n            logCharacters = 0\n            publisher.removeCallbacksAndMessages(null)\n            publishPending = false')
        return value

    edit("core/VpnBus.kt", bus)

    def repository(value):
        value = section(value, '        // Legacy single-profile keys', '        val themeMode', '')
        value = once(value, '        val legacyMigrated = booleanPreferencesKey("settings.legacyMigrated")\n', '')
        value = section(value, '    /**\n     * One-time migration:', '    // ── settings', '')
        value = once(value, 'import dev.opentunnel.vpn.util.SecretBox\n', '')
        value = once(value, '    val profiles: Flow<List<VpnProfile>> = profileStore.profiles',
                     '    val storageError = profileStore.storageError\n    val profiles: Flow<List<VpnProfile>> = profileStore.profiles')
        value = once(value, '    suspend fun pinCertificate(fingerprint: String) {', '''    suspend fun pinCertificate(profileID: String, server: String, fingerprint: String) =
        profileStore.pinCertificate(profileID, server, fingerprint)

    suspend fun pinCertificate(fingerprint: String) {''')
        return value

    edit("data/Repository.kt", repository)

    def store(value):
        value = once(value, '    private val json = Json', '''    private val _storageError = kotlinx.coroutines.flow.MutableStateFlow<String?>(null)
    val storageError: kotlinx.coroutines.flow.StateFlow<String?> = _storageError
    private val json = Json''')
        value = once(value, '.map { prefs -> decode(prefs[Keys.profilesJson].orEmpty()) }', '''.map { prefs ->
            try { decode(prefs[Keys.profilesJson].orEmpty()).also { _storageError.value = null } }
            catch (error: Exception) { _storageError.value = "Profiles could not be read; the original data was preserved."; emptyList() }
        }''')
        value = once(value, 'suspend fun allProfiles(): List<VpnProfile> = profiles.first()',
                     'suspend fun allProfiles(): List<VpnProfile> = decode(store.data.first()[Keys.profilesJson].orEmpty())')
        value = once(value, 'it.copy(password = "")', 'it.copy(password = "", tokenString = "")')
        value = once(value, '            if (idx >= 0) current[idx] = toSave else current.add(toSave)',
                     '            if (idx >= 0) current[idx] = ProfilePolicy.updated(current[idx], toSave) else current.add(toSave)')
        value = once(value, '    suspend fun importJson(jsonStr: String): Int {\n        return runCatching {', '''    suspend fun pinCertificate(id: String, server: String, fingerprint: String) {
        store.edit { prefs ->
            val current = decode(prefs[Keys.profilesJson].orEmpty())
            val updated = ProfilePolicy.pin(current, id, server, fingerprint)
            if (updated != current) prefs[Keys.profilesJson] = encode(updated)
        }
    }

    suspend fun importJson(jsonStr: String): Int {
        require(jsonStr.toByteArray(Charsets.UTF_8).size <= ProfilePolicy.MAX_BYTES) { "Profile import is too large" }
        return run {''')
        value = once(value, '                    if (idx >= 0) current[idx] = p else current.add(p)',
                     '                    if (idx >= 0) current[idx] = ProfilePolicy.updated(current[idx], p) else current.add(p)')
        value = once(value, '        }.getOrDefault(0)', '        }')
        value = section(value, '    private fun encode(profiles:', '    companion object {', '''    private val codec = ProfileCodec(SecretBox::encrypt, SecretBox::decrypt)
    private fun encode(profiles: List<VpnProfile>) = codec.encode(profiles)
    private fun decode(raw: String) = codec.decode(raw)

''')
        return value

    edit("data/ProfileStore.kt", store)

    def view_model(value):
        value = once(value, '        // One-time migration of legacy single-profile data.\n        viewModelScope.launch { repository.migrateLegacyProfileIfNeeded() }', '''        viewModelScope.launch {
            repository.storageError.collect { error -> if (error != null) VpnBus.setError(error) }
        }''')
        # Scope all storage/UI mutations behind one error boundary.
        for expression in ('repository.saveProfile(profile)', 'repository.reorderProfiles(profiles)',
                           'repository.deleteProfile(profileId)', 'repository.setActiveProfile(profileId)',
                           'onResult(repository.exportProfilesJson())', 'onResult(repository.importProfilesJson(jsonStr))',
                           'repository.pinCertificate("")'):
            value = once(value, 'viewModelScope.launch { ' + expression + ' }', 'profileOperation { ' + expression + ' }')
        value = once(value, '    fun saveProfile(profile: VpnProfile) {', '''    private fun profileOperation(action: suspend () -> Unit) {
        viewModelScope.launch {
            try { action() }
            catch (error: kotlinx.coroutines.CancellationException) { throw error }
            catch (error: Exception) { VpnBus.setError("Profile operation failed. Check the input, storage and device key; original data was preserved.") }
        }
    }

    fun saveProfile(profile: VpnProfile) {''')
        return value

    edit("ui/AppViewModel.kt", view_model)
    def secrets(value):
        if value.count('}.getOrDefault("")') != 2:
            raise ValueError("Pinned credential encryption/decryption boundaries changed")
        return value.replace('}.getOrDefault("")',
                             '}.getOrElse { throw IllegalStateException("Device credential encryption is unavailable", it) }')

    edit("util/SecretBox.kt", secrets)
    edit("data/Models.kt", lambda value: once(value, 'val enableGeoIpLookup: Boolean = true', 'val enableGeoIpLookup: Boolean = false'))
    for name in ("SessionOwner", "DiagnosticLog", "ProfilePolicy", "ProfileCodec"):
        package = "data" if name.startswith("Profile") else "core"
        for suffix, directory in (("", "main"), ("Test", "test")):
            target = app / f"app/src/{directory}/java/dev/opentunnel/vpn/{package}/{name}{suffix}.kt"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / "mobile/android" / f"{name}{suffix}.kt", target)
