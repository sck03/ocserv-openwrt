package dev.opentunnel.vpn.core

import android.net.VpnService
import android.os.Build
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import dev.opentunnel.vpn.BuildConfig
import dev.opentunnel.vpn.data.AppSettings
import dev.opentunnel.vpn.data.SplitTunnelMode
import dev.opentunnel.vpn.data.VpnProfile
import dev.opentunnel.vpn.util.Net
import dev.opentunnel.vpn.util.NetworkRoutes
import org.infradead.libopenconnect.LibOpenConnect
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

/** What the runner needs from the hosting VpnService. */
interface TunnelHost {
    fun newBuilder(): VpnService.Builder
    fun protectSocket(socket: Int): Boolean
    fun caBundlePath(): String?
    fun persistCertificatePin(profileID: String, server: String, fingerprint: String)
    fun onTunnelFinished(source: TunnelRunner, error: String?)
}

/** Callbacks libopenconnect raises on the tunnel thread. */
private interface SessionCallbacks {
    fun onProgress(level: Int, message: String?)
    fun onProtectSocket(fd: Int)
    fun onStatsUpdate(stats: LibOpenConnect.VPNStats?)
    fun onReconnected()
    fun onSetupTun()
    fun onValidatePeerCert(reason: String?): Int
    fun onProcessAuthForm(form: LibOpenConnect.AuthForm?): Int
}

private class Session : LibOpenConnect {
    lateinit var callbacks: SessionCallbacks
    constructor(userAgent: String) : super(userAgent)
    override fun onProgress(level: Int, msg: String?) = callbacks.onProgress(level, msg)
    override fun onProtectSocket(fd: Int) = callbacks.onProtectSocket(fd)
    override fun onStatsUpdate(stats: LibOpenConnect.VPNStats?) = callbacks.onStatsUpdate(stats)
    override fun onReconnected() = callbacks.onReconnected()
    override fun onSetupTun() = callbacks.onSetupTun()
    override fun onValidatePeerCert(msg: String?): Int = callbacks.onValidatePeerCert(msg)
    override fun onProcessAuthForm(form: LibOpenConnect.AuthForm?): Int =
        callbacks.onProcessAuthForm(form)
}

/** Drives one native session; routing and ownership policies are independently tested. */
class TunnelRunner(
    private val host: TunnelHost,
    inputProfile: VpnProfile,
    private val settings: AppSettings,
) : Thread("openconnect-tunnel") {
    private val profile = inputProfile.copy(password = "")

    private val disconnectRequested = AtomicBoolean(false)
    private val userCancelled = AtomicBoolean(false)

    private val control = SessionControl<Session>()
    private val session: Session? get() = control.current()
    @Volatile private var tunFd: ParcelFileDescriptor? = null
    @Volatile private var password: String = inputProfile.password

    private var connectionFailure: String? = null
    init {
        for (secret in listOf(profile.username, password, profile.authGroup, profile.tokenString)) VpnBus.remember(secret)
    }

    fun abort() {
        requestDisconnect()
        closeTun() // Break a stuck TUN read; the worker alone destroys the native core.
    }

    private var passwordConsumed = false
    private var authGroupApplied = false

    fun requestDisconnect() {
        if (disconnectRequested.compareAndSet(false, true)) {
            VpnBus.setStage(ConnectionStage.DISCONNECTING)
            Interaction.cancelPending()
        }
        control.cancel { runCatching { it.cancel() } }
        interrupt() // Wake a Java prompt wait without a periodic polling timer.
    }

    fun requestReconnect() {
        if (disconnectRequested.get()) return
        VpnBus.setStage(ConnectionStage.RECONNECTING)
        control.use { runCatching { it.pause() } }
    }

    fun pollStats() {
        control.use { runCatching { it.requestStats() } }
    }

    override fun run() {
        var failure: String? = null
        try {
            failure = connect()
        } catch (t: Throwable) {
            failure = t.message ?: t.javaClass.simpleName
            VpnBus.error("Tunnel stopped unexpectedly: $failure")
        } finally {
            closeTun()
            control.close { runCatching { it.destroy() } }
            password = ""
            host.onTunnelFinished(this, if (disconnectRequested.get()) null else failure)
        }
    }

    private fun connect(): String? {
        if (disconnectRequested.get()) return null
        if (!NativeLibrary.isAvailable) {
            VpnBus.error(NativeLibrary.MISSING_MESSAGE)
            return NativeLibrary.MISSING_MESSAGE
        }

        VpnBus.setStage(ConnectionStage.PREPARING)
        VpnBus.info("openconnect ${NativeLibrary.version() ?: BuildConfig.OPENCONNECT_VERSION}")

        val userAgent = profile.userAgent.trim().ifEmpty { defaultUserAgent }
        val lib = Session(userAgent)
        lib.callbacks = Callbacks()
        if (!control.publish(lib)) { lib.destroy(); return null }
        if (disconnectRequested.get()) { control.cancel { it.cancel() }; return null }

        applyPreferences(lib)

        val url = normaliseServer(profile.server)
        VpnBus.info("Connecting to $url")
        if (lib.parseURL(url) != 0) return "Could not parse the HTTPS server address."

        VpnBus.setStage(ConnectionStage.AUTHENTICATING)
        if (disconnectRequested.get()) return null
        val cookieResult = lib.obtainCookie()

        if (disconnectRequested.get()) return null
        if (cookieResult != 0) {
            return when {
                userCancelled.get() -> "Sign-in was cancelled."
                cookieResult > 0 -> "Sign-in was cancelled."
                else -> "Authentication failed. Check the server address, username and password."
            }
        }

        VpnBus.remember(lib.getCookie())
        password = ""
        VpnBus.setStage(ConnectionStage.CONNECTING)
        if (lib.makeCSTPConnection() != 0) {
            return "The gateway accepted the login but refused to establish the tunnel."
        }

        VpnBus.setStage(ConnectionStage.CONNECTING, "Setting up tunnel interface…")

        val ipInfo = installTun(lib)

        if (profile.enableDtls) {
            lib.setupDTLS(DTLS_ATTEMPT_SECONDS)
        }

        VpnBus.resetStatsWindow()
        VpnBus.setConnected(SystemClock.elapsedRealtime(), describe(lib, ipInfo))
        VpnBus.info("Tunnel is up")

        while (!disconnectRequested.get()) {
            val result = lib.mainloop(RECONNECT_TIMEOUT_SECONDS, LibOpenConnect.RECONNECT_INTERVAL_MIN)
            if (result < 0) return if (disconnectRequested.get()) null else connectionFailure ?: ConnectionPolicy.mainloopFailure(result)
        }
        return null
    }

    private fun applyPreferences(lib: Session) {
        lib.setLogLevel(
            if (settings.verboseLogging) LibOpenConnect.PRG_DEBUG else LibOpenConnect.PRG_INFO
        )

        if (lib.setProtocol(profile.protocol) != 0) {
            VpnBus.error("Protocol \u201c${profile.protocol}\u201d is not supported; falling back to anyconnect")
            lib.setProtocol("anyconnect")
        }

        if (profile.trustedCertificate.isBlank()) {
            if (profile.caCertPath.isNotBlank()) lib.setCAFile(profile.caCertPath)
            else host.caBundlePath()?.let { lib.setCAFile(it) }
        }
        lib.setSystemTrust(profile.trustedCertificate.isBlank())

        if (profile.userCertPath.isNotBlank()) {
            lib.setClientCert(profile.userCertPath, profile.privateKeyPath)
            VpnBus.log(LogLevel.DEBUG, "Client certificate enabled")
        }

        if (profile.softwareTokenMode > 0 && profile.tokenString.isNotBlank()) {
            lib.setTokenMode(profile.softwareTokenMode, profile.tokenString)
            VpnBus.log(LogLevel.DEBUG, "Software token configured (mode ${profile.softwareTokenMode})")
        }

        if (profile.csdWrapper.isNotBlank()) {
            lib.setCSDWrapper(profile.csdWrapper, "", "")
        }

        lib.setReportedOS(profile.reportedOs)
        lib.setMobileInfo("1.0", profile.reportedOs, DEVICE_ID)
        lib.setXMLPost(!profile.disableXmlPost)
        lib.setPFS(profile.requirePfs)

        val reqMtu = if (profile.mtu > 0) profile.mtu else 1350
        lib.setReqMTU(reqMtu)
        VpnBus.log(LogLevel.DEBUG, "Requested MTU $reqMtu")

        val dpd = if (profile.dpdSeconds > 0) profile.dpdSeconds else 30
        lib.setDPD(dpd)
        VpnBus.log(LogLevel.DEBUG, "DPD interval set to ${dpd}s")

        if (!profile.enableDtls) lib.disableDTLS()
        if (!profile.enableIpv6) lib.disableIPv6()
        val serverHost = runCatching { java.net.URI(normaliseServer(profile.server)).host }.getOrNull().orEmpty()
        val useInsecure = profile.allowInsecureCrypto || profile.wifiCompatMode

        if (useInsecure) {
            lib.setAllowInsecureCrypto(true)
            lib.setSystemTrust(profile.trustedCertificate.isBlank())
            VpnBus.log(LogLevel.DEBUG, "TLS compatibility mode enabled for $serverHost")
        }
    }

    private fun normaliseServer(raw: String) = ConnectionPolicy.serverUrl(raw)

    private fun establishTun(ip: LibOpenConnect.IPInfo): ParcelFileDescriptor? {
        val builder = host.newBuilder()
        builder.setSession(profile.displayName)

        var haveAddress = false

        Net.parseCidr(ip.addr.orEmpty(), ip.netmask)?.let { cidr ->
            runCatching { builder.addAddress(cidr.address, cidr.prefixLength) }
                .onSuccess {
                    VpnBus.log(LogLevel.DEBUG, "IPv4 ${cidr.address}/${cidr.prefixLength}")
                    haveAddress = true
                }
                .onFailure { VpnBus.error("Rejected IPv4 address ${cidr.address}/${cidr.prefixLength}") }
        }

        val ipv6 = ConnectionPolicy.ipv6Address(profile.enableIpv6, ip.addr6, ip.netmask6)
        if (ipv6 != null) {
            builder.addAddress(ipv6.address, ipv6.prefixLength)
            haveAddress = true
        }

        if (!haveAddress) {
            VpnBus.error("The gateway did not assign a usable IP address")
            return null
        }

        val mtu = ConnectionPolicy.tunnelMtu(ip.MTU, profile.mtu, ipv6 != null)
        builder.setMtu(mtu)
        VpnBus.log(LogLevel.DEBUG, "MTU $mtu")

        applyRoutes(builder, ip, ipv6 != null)
        applyDns(builder, ip, ipv6 != null)
        applySplitTunnel(builder)

        builder.setBlocking(true)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            builder.setMetered(false)
        }

        return runCatching { builder.establish() }
            .onFailure { VpnBus.error("establish() failed: ${it.message}") }
            .getOrNull()
    }

    private fun applyRoutes(builder: VpnService.Builder, ip: LibOpenConnect.IPInfo, ipv6: Boolean) {
        val rules = NetworkRoutes.Rules(settings.splitTunnelNetworksEnabled,
            settings.splitTunnelNetworksMode == SplitTunnelMode.INCLUDE_SELECTED, settings.splitTunnelNetworks)
        val routes = NetworkRoutes.routes(ip.splitIncludes.orEmpty().toList(), ip.splitExcludes.orEmpty().toList(),
            rules, settings.bypassLocalNetworks, ipv6)
        routes.forEach { builder.addRoute(it.address, it.prefixLength) }
        VpnBus.info("Installed ${routes.size} tunnel route(s)")
    }

    private fun applyDns(builder: VpnService.Builder, ip: LibOpenConnect.IPInfo, ipv6: Boolean) {
        val customDnsList = profile.customDnsServers.ifEmpty {
            settings.customDns.split(',', ' ', ';').map { it.trim() }.filter { it.isNotEmpty() }
        }

        var added = 0
        if (customDnsList.isNotEmpty()) {
            for (server in customDnsList) {
                if (Net.isValidIp(server) && (ipv6 || ':' !in server)) {
                    runCatching { builder.addDnsServer(server) }.onSuccess { added++ }
                }
            }
            if (added > 0) VpnBus.info("Applied $added custom DNS server(s): ${customDnsList.joinToString()}")
        }

        if (added == 0) {
            for (server in ip.DNS.orEmpty()) {
                if (Net.isValidIp(server) && (ipv6 || ':' !in server)) {
                    runCatching { builder.addDnsServer(server) }.onSuccess { added++ }
                }
            }
            if (added == 0) VpnBus.log(LogLevel.DEBUG, "Gateway supplied no DNS servers")
        }

        val domains = buildList {
            ip.domain?.split(' ', ',', '\t')?.let { addAll(it) }
            addAll(ip.splitDNS.orEmpty())
        }

        for (domain in domains.map { it.trim() }.filter { it.isNotEmpty() }.distinct()) {
            runCatching { builder.addSearchDomain(domain) }
        }
    }

    private fun applySplitTunnel(builder: VpnService.Builder) {
        val packages = settings.selectedPackages
        if (!settings.splitTunnelEnabled || packages.isEmpty()) {
            VpnBus.updateInfo { it.copy(excludedApps = 0) }
            return
        }

        var applied = 0
        val stale = mutableListOf<String>()
        for (packageName in packages) {
            try {
                when (settings.splitTunnelMode) {
                    SplitTunnelMode.EXCLUDE_SELECTED -> builder.addDisallowedApplication(packageName)
                    SplitTunnelMode.INCLUDE_SELECTED -> builder.addAllowedApplication(packageName)
                }
                applied++
            } catch (e: Exception) { stale += packageName }
        }
        if (stale.isNotEmpty()) VpnBus.log(LogLevel.DEBUG, "Ignored uninstalled app(s): ${stale.joinToString()}")
        VpnBus.updateInfo { it.copy(excludedApps = applied) }
        VpnBus.info(
            when (settings.splitTunnelMode) {
                SplitTunnelMode.EXCLUDE_SELECTED -> "$applied app(s) bypass the VPN"
                SplitTunnelMode.INCLUDE_SELECTED -> "Only $applied app(s) use the VPN"
            }
        )
    }

    private fun describe(lib: Session, ip: LibOpenConnect.IPInfo) = TunnelInfo(
        server = runCatching { lib.getHostname() }.getOrNull() ?: profile.server,
        ipv4 = ip.addr,
        ipv6 = ip.netmask6 ?: ip.addr6,
        dns = ip.DNS.orEmpty().toList(),
        domain = ip.domain,
        mtu = ConnectionPolicy.tunnelMtu(ip.MTU, profile.mtu,
            ConnectionPolicy.ipv6Address(profile.enableIpv6, ip.addr6, ip.netmask6) != null),
        cstpCipher = runCatching { lib.getCSTPCipher() }.getOrNull(),
        dtlsCipher = runCatching { lib.getDTLSCipher() }.getOrNull(),
        cstpCompression = runCatching { lib.getCSTPCompression() }.getOrNull(),
        dtlsCompression = runCatching { lib.getDTLSCompression() }.getOrNull(),
        serverRoutes = ip.splitIncludes.orEmpty().toList(),
        excludedApps = VpnBus.status.value.info.excludedApps,
        certFingerprint = runCatching { lib.getPeerCertHash() }.getOrNull(),
        profileDisplayName = profile.displayName,
    )

    private fun installTun(lib: Session): LibOpenConnect.IPInfo {
        val ip = lib.getIPInfo() ?: error("The gateway did not send an IP configuration")
        val descriptor = establishTun(ip) ?: error("Android refused to create the tunnel interface")
        try {
            check(lib.setupTunFD(descriptor.fd) == 0) { "Could not attach the tunnel interface" }
        } catch (error: Exception) {
            descriptor.close()
            throw error
        }
        synchronized(this) {
            if (disconnectRequested.get()) {
                descriptor.close()
                error("Connection canceled")
            }
            val previous = tunFd
            tunFd = descriptor
            previous?.close()
        }
        return ip
    }

    @Synchronized private fun closeTun() {
        val fd = tunFd ?: return
        tunFd = null
        try { fd.close() } catch (e: IOException) { /* library may have already closed it */ }
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    // ── libopenconnect callbacks ────────────────────────────────────────────

    private inner class Callbacks : SessionCallbacks {
        private var acceptedPin = ""
        private var authAttempts = 0
        private fun sameServer(): Boolean {
            val lib = session ?: return false
            val expected = java.net.URI(ConnectionPolicy.serverUrl(profile.server))
            return expected.host.trim('[', ']').equals(lib.getDNSName()?.trim('[', ']'), ignoreCase = true) &&
                (if (expected.port < 0) 443 else expected.port) == lib.getPort()
        }

        private fun certificateMatches(): Boolean {
            val expected = profile.trustedCertificate.trim().ifEmpty { acceptedPin }
            if (expected.isEmpty()) return true // A valid system/custom CA chain.
            val actual = runCatching { session?.getPeerCertHash() }.getOrNull().orEmpty()
            return CertificatePolicy.matches(expected, actual)
        }


        override fun onProgress(level: Int, message: String?) {
            val text = message?.trim().orEmpty()
            if (text.isEmpty()) return
            val logLevel = when {
                text.contains("vhost-net", ignoreCase = true) -> LogLevel.DEBUG
                level == LibOpenConnect.PRG_ERR   -> LogLevel.ERROR
                level == LibOpenConnect.PRG_INFO  -> LogLevel.INFO
                level == LibOpenConnect.PRG_DEBUG -> LogLevel.DEBUG
                else                              -> LogLevel.TRACE
            }
            VpnBus.log(logLevel, text)
        }

        override fun onProtectSocket(fd: Int) {
            if (!host.protectSocket(fd)) {
                VpnBus.error("VpnService.protect() failed \u2014 the tunnel socket may loop back on itself")
            }
        }

        override fun onStatsUpdate(stats: LibOpenConnect.VPNStats?) {
            stats ?: return
            VpnBus.updateStats(
                rxBytes   = stats.rxBytes,
                txBytes   = stats.txBytes,
                rxPackets = stats.rxPkts,
                txPackets = stats.txPkts,
                nowElapsed = SystemClock.elapsedRealtime(),
            )
        }

        override fun onReconnected() = refreshTunnel()

        override fun onSetupTun() = refreshTunnel()

        private fun refreshTunnel() {
            val lib = session ?: return
            if (disconnectRequested.get()) return
            try {
                val ip = installTun(lib)
                if (disconnectRequested.get()) return
                val since = VpnBus.status.value.connectedAtElapsed.takeIf { it > 0L } ?: SystemClock.elapsedRealtime()
                VpnBus.setConnected(since, describe(lib, ip))
                VpnBus.info("Tunnel settings refreshed")
            } catch (error: Exception) {
                connectionFailure = "Could not refresh tunnel settings: ${error.message}"
                VpnBus.error(connectionFailure!!)
                lib.cancel()
            }
        }

        override fun onValidatePeerCert(reason: String?): Int {
            if (disconnectRequested.get()) return -1
            val lib = session ?: return -1
            if (!CertificatePolicy.validAt(runCatching { lib.getPeerCertDER() }.getOrNull())) {
                userCancelled.set(true)
                VpnBus.error("Server certificate is expired, not yet valid, or unreadable.")
                return -1
            }
            val fingerprint = runCatching { lib.getPeerCertHash() }.getOrNull().orEmpty()

            val pinned = profile.trustedCertificate.trim().ifEmpty { acceptedPin }
            if (pinned.isNotEmpty()) {
                if (CertificatePolicy.matches(pinned, fingerprint)) return 0
                userCancelled.set(true)
                VpnBus.error("Server public key changed or the saved pin is incomplete; verify it with your administrator.")
                return -1
            }
            if (!CertificatePolicy.isFullPin(fingerprint)) return -1

            val prompt = UserPrompt.CertTrust(
                id = Interaction.nextId(),
                host = runCatching { lib.getHostname() }.getOrNull() ?: profile.server,
                reason = reason?.trim().orEmpty().ifEmpty { "The certificate could not be verified." },
                fingerprint = fingerprint,
                details = runCatching { lib.getPeerCertDetails() }.getOrNull().orEmpty(),
            )
            VpnBus.error("Server certificate needs review: ${prompt.reason}")

            return if (Interaction.await(prompt) { disconnectRequested.get() } is PromptResult.Accept) {
                acceptedPin = fingerprint
                host.persistCertificatePin(profile.id, profile.server, fingerprint)
                VpnBus.info("Server certificate accepted and pinned for this profile")
                0
            } else {
                userCancelled.set(true)
                VpnBus.error("Server certificate rejected")
                -1
            }
        }

        override fun onProcessAuthForm(form: LibOpenConnect.AuthForm?): Int {
            if (disconnectRequested.get()) return LibOpenConnect.OC_FORM_RESULT_CANCELLED
            form ?: return LibOpenConnect.OC_FORM_RESULT_ERR
            form.opts.orEmpty().forEach { VpnBus.remember(it.value) }
            if (++authAttempts > 64 || form.opts.orEmpty().size > 256 || !sameServer() || !certificateMatches()) {
                userCancelled.set(true)
                return LibOpenConnect.OC_FORM_RESULT_CANCELLED
            }

            form.banner?.trim()?.takeIf { it.isNotEmpty() }?.let { VpnBus.info(it) }
            form.error?.trim()?.takeIf { it.isNotEmpty() }?.let { VpnBus.error(it) }

            val groupOpt = form.authgroupOpt
            if (groupOpt != null && profile.authGroup.isNotBlank() && !authGroupApplied) {
                val match = matchChoice(groupOpt.choices, profile.authGroup)
                if (match != null) {
                    authGroupApplied = true
                    if (groupOpt.value != match.name) {
                        groupOpt.value = match.name
                        VpnBus.info("Selected authentication group \u201c${match.label ?: match.name}\u201d")
                        return LibOpenConnect.OC_FORM_RESULT_NEWGROUP
                    }
                }
            }

            val unfilled = mutableListOf<LibOpenConnect.FormOpt>()

            for (opt in form.opts.orEmpty()) {
                if (opt.flags and LibOpenConnect.OC_FORM_OPT_IGNORE.toLong() != 0L) continue
                when (opt.type) {
                    LibOpenConnect.OC_FORM_OPT_TEXT ->
                        if (profile.username.isNotBlank() && looksLikeUsername(opt)) {
                            VpnBus.remember(profile.username)
                        opt.value = profile.username
                        } else { unfilled += opt }

                    LibOpenConnect.OC_FORM_OPT_PASSWORD ->
                        if (password.isNotEmpty() && !passwordConsumed && !looksLikeToken(opt)) {
                            VpnBus.remember(password)
                        opt.value = password
                            passwordConsumed = true
                        } else { unfilled += opt }

                    LibOpenConnect.OC_FORM_OPT_SELECT -> {
                        val choices = opt.choices.orEmpty()
                        val match = profile.authGroup.takeIf { it.isNotBlank() }
                            ?.let { matchChoice(opt.choices, it) }
                        when {
                            match != null       -> opt.value = match.name
                            choices.size == 1   -> opt.value = choices[0].name
                            else                -> unfilled += opt
                        }
                    }
                    else -> Unit
                }
            }

            if (unfilled.isEmpty()) return LibOpenConnect.OC_FORM_RESULT_OK
            if (disconnectRequested.get()) return LibOpenConnect.OC_FORM_RESULT_CANCELLED

            val prompt = UserPrompt.Auth(
                id      = Interaction.nextId(),
                title   = form.authID?.trim()?.takeIf { it.isNotEmpty() } ?: "Sign in",
                banner  = form.banner?.trim()?.takeIf { it.isNotEmpty() },
                message = form.message?.trim()?.takeIf { it.isNotEmpty() },
                error   = form.error?.trim()?.takeIf { it.isNotEmpty() },
                fields  = unfilled.map { opt -> toPromptField(opt) },
            )

            return when (val result = Interaction.await(prompt) { disconnectRequested.get() }) {
                is PromptResult.Values -> {
                    for (opt in unfilled) {
                        val value = result.values[opt.name] ?: continue
                        VpnBus.remember(value)
                        opt.value = value
                        if (opt.type == LibOpenConnect.OC_FORM_OPT_PASSWORD) {
                            password = value
                            passwordConsumed = true
                        }
                    }
                    LibOpenConnect.OC_FORM_RESULT_OK
                }
                else -> {
                    userCancelled.set(true)
                    LibOpenConnect.OC_FORM_RESULT_CANCELLED
                }
            }
        }

        private fun toPromptField(opt: LibOpenConnect.FormOpt) = PromptField(
            name   = opt.name.orEmpty(),
            label  = opt.label?.trim()?.removeSuffix(":")?.takeIf { it.isNotEmpty() } ?: opt.name.orEmpty(),
            type   = when (opt.type) {
                LibOpenConnect.OC_FORM_OPT_PASSWORD -> PromptFieldType.PASSWORD
                LibOpenConnect.OC_FORM_OPT_SELECT   -> PromptFieldType.SELECT
                else                                -> PromptFieldType.TEXT
            },
            prefill = if (opt.type == LibOpenConnect.OC_FORM_OPT_TEXT && looksLikeUsername(opt)) profile.username else "",
            choices = opt.choices.orEmpty().map { PromptChoice(it.name.orEmpty(), it.label ?: it.name.orEmpty()) },
        )

        private fun matchChoice(choices: List<LibOpenConnect.FormChoice>?, wanted: String): LibOpenConnect.FormChoice? =
            choices.orEmpty().firstOrNull {
                it.name.equals(wanted, ignoreCase = true) || it.label.equals(wanted, ignoreCase = true)
            }

        private fun looksLikeUsername(opt: LibOpenConnect.FormOpt): Boolean {
            val name  = opt.name.orEmpty().lowercase()
            val label = opt.label.orEmpty().lowercase()
            return name.contains("user") || name.contains("uname") || name.contains("login") ||
                name == "id" || label.contains("user") || label.contains("login") || label.contains("email")
        }

        private fun looksLikeToken(opt: LibOpenConnect.FormOpt): Boolean {
            val text = (opt.name.orEmpty() + " " + opt.label.orEmpty()).lowercase()
            return text.contains("token") || text.contains("otp") ||
                text.contains("one-time") || text.contains("secondary") ||
                text.contains("verification") || text.contains("passcode") ||
                text.contains("second password")
        }
    }

    private val defaultUserAgent: String
        get() = "AnyConnect Android 4.10.05065"

    private companion object {
        /** Reduced to 1 s so that blocked UDP/DTLS fails fast and falls back immediately to CSTP without freezing. */
        const val DTLS_ATTEMPT_SECONDS      = 1
        const val RECONNECT_TIMEOUT_SECONDS = 300
        const val DEVICE_ID                 = "0123456789ABCDEF0123456789ABCDEF01234567"

    }
}
