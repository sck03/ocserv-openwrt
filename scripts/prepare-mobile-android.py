"""Small, fail-closed adaptations to the checksum-pinned Android upstream."""
import shutil


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Android upstream changed near: {old[:80]}")
    return text.replace(old, new)


def section(text, start, end, replacement):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ValueError("Android upstream section changed")
    a, b = text.index(start), text.index(end)
    if b <= a:
        raise ValueError("Android upstream section order changed")
    return text[:a] + replacement + text[b:]


def prepare(app, root):
    gradle = app / "app/build.gradle.kts"
    value = once(gradle.read_text(encoding="utf-8"), 'applicationId = "dev.opentunnel.vpn"',
                 'applicationId = "com.bulijie.vpn"')
    value = once(value, '        versionCode = 16', '        versionCode = 17')
    value = section(value, '        versionName =', '\n\n        ndk {', '        versionName = "0.1.1"')
    gradle.write_text(value, encoding="utf-8")
    resources = app / "app/src/main/res/values-zh-rCN"
    resources.mkdir(exist_ok=True)
    (resources / "strings.xml").write_text(
        '<resources><string name="app_name">布利杰VPN</string></resources>\n', encoding="utf-8")
    manifest = app / "app/src/main/AndroidManifest.xml"
    manifest.write_text(once(manifest.read_text(encoding="utf-8"), 'android:allowBackup="true"',
                             'android:allowBackup="false"'), encoding="utf-8")
    native = app / "native/build-openconnect.sh"
    value = once(native.read_text(encoding="utf-8"),
                 '| grep -q "Java_org_infradead_libopenconnect_LibOpenConnect_mainloop"; then',
                 '| grep "Java_org_infradead_libopenconnect_LibOpenConnect_mainloop" >/dev/null; then')
    value = once(value, 'LDFLAGS="-L$PREFIX/lib -Wl,--gc-sections"',
                 'LDFLAGS="-L$PREFIX/lib -Wl,--gc-sections -Wl,-z,max-page-size=16384"')
    native.write_text(value, encoding="utf-8")
    runner = app / "app/src/main/java/dev/opentunnel/vpn/core/TunnelRunner.kt"
    value = runner.read_text(encoding="utf-8")
    value = section(value, '        val rawUrl = normaliseServer(profile.server)',
                    '        VpnBus.setStage(ConnectionStage.AUTHENTICATING)', '''        val url = normaliseServer(profile.server)
        VpnBus.info("Connecting to $url")
        if (lib.parseURL(url) != 0) return "Could not parse the HTTPS server address."

''')
    value = section(value, '        var cookieResult = lib.obtainCookie()',
                    '        if (disconnectRequested.get()) return null',
                    '        val cookieResult = lib.obtainCookie()\n\n')
    # A configured pin takes precedence over both system roots and a custom CA.
    value = section(value, '        if (profile.caCertPath.isNotBlank()) {',
                    '        if (profile.userCertPath.isNotBlank()) {', '''        if (profile.trustedCertificate.isBlank()) {
            if (profile.caCertPath.isNotBlank()) lib.setCAFile(profile.caCertPath)
            else host.caBundlePath()?.let { lib.setCAFile(it) }
        }
        lib.setSystemTrust(profile.trustedCertificate.isBlank())

''')
    value = once(value, 'profile.allowInsecureCrypto || profile.wifiCompatMode || (serverHost.isNotEmpty() && compatHosts.contains(serverHost))',
                 'profile.allowInsecureCrypto || profile.wifiCompatMode')
    value = once(value, '            runCatching { lib.setSystemTrust(true) }',
                 '            lib.setSystemTrust(profile.trustedCertificate.isBlank())')
    # Let the core resolve every A/AAAA candidate and re-resolve DDNS on reconnect.
    # The upstream pre-resolver discarded all but one address, the query string,
    # and cached an IPv4-only DoH result independently of the network's DNS TTL.
    value = section(value, '    private fun normaliseServer(raw: String): String {',
                    '    private fun establishTun(',
                    '    private fun normaliseServer(raw: String) = ConnectionPolicy.serverUrl(raw)\n\n')
    value = section(value, '        private data class DnsCacheEntry(', '    }\n}', '')
    value = section(value, '        val mtu = when {', '        builder.setMtu(mtu)',
                    '        val mtu = ConnectionPolicy.tunnelMtu(ip.MTU, profile.mtu, profile.enableIpv6)\n')
    value = once(value, '        mtu = if (profile.mtu > 0) profile.mtu else ip.MTU,',
                 '        mtu = ConnectionPolicy.tunnelMtu(ip.MTU, profile.mtu, profile.enableIpv6),')
    value = once(value, '        const val MIN_MTU                   = 1280\n', '')
    value = once(value, '        const val DEFAULT_MTU               = 1350\n', '')
    value = section(value, '        runCatching { builder.addRoute(route.address, route.prefixLength) }',
                    '    private fun applyDns(', '''        // A missing required route must fail setup, not report a connected tunnel.
        if (route.isIpv6 && !profile.enableIpv6) return
        builder.addRoute(route.address, route.prefixLength)
    }

''')
    value = once(value, 'import dev.opentunnel.vpn.util.Net',
                 'import dev.opentunnel.vpn.util.Net\nimport dev.opentunnel.vpn.util.RoutePolicy')
    value = section(value, '\n        if (settings.splitTunnelNetworksEnabled &&\n',
                    '    private fun addRoute(', '''
        val includes = when {
            settings.splitTunnelNetworksEnabled &&
                settings.splitTunnelNetworksMode == SplitTunnelMode.INCLUDE_SELECTED && customCidrs.isNotEmpty() -> customCidrs
            serverIncludes.isNotEmpty() -> serverIncludes
            else -> listOf(Cidr("0.0.0.0", 0, false), Cidr("::", 0, true))
        }
        val excludes = buildList {
            addAll(serverExcludes)
            if (settings.bypassLocalNetworks) addAll(Net.LOCAL_NETWORKS)
            if (settings.splitTunnelNetworksEnabled &&
                settings.splitTunnelNetworksMode == SplitTunnelMode.EXCLUDE_SELECTED) addAll(customCidrs)
        }
        val routes = RoutePolicy.routes(includes.filter { profile.enableIpv6 || !it.isIpv6 }, excludes)
        routes.forEach { addRoute(builder, it) }
        VpnBus.info("Installed ${routes.size} tunnel route(s)")
    }

''')
    # A DNS server also enables its address family in VpnService.Builder.
    # Do not reopen IPv6 through DNS when the profile explicitly disabled it.
    for loop in ('            for (server in customDnsList) {',
                 '            for (server in ip.DNS.orEmpty()) {'):
        value = once(value, loop + '\n                if (Net.isValidIp(server)) {',
                     loop + "\n                if (Net.isValidIp(server) && (profile.enableIpv6 || ':' !in server)) {")
    value = once(value, '    private var passwordConsumed = false',
                 '    private var connectionFailure: String? = null\n    private var passwordConsumed = false')
    value = section(value, '        val ipInfo = lib.getIPInfo()', '        if (profile.enableDtls)',
                    '        val ipInfo = installTun(lib)\n\n')
    value = once(value, '        return null\n    }\n\n    private fun applyPreferences',
                 '        return connectionFailure\n    }\n\n    private fun applyPreferences')
    value = once(value, '    private fun closeTun() {', '''    private fun installTun(lib: Session): LibOpenConnect.IPInfo {
        val ip = lib.getIPInfo() ?: error("The gateway did not send an IP configuration")
        val descriptor = establishTun(ip) ?: error("Android refused to create the tunnel interface")
        try {
            check(lib.setupTunFD(descriptor.fd) == 0) { "Could not attach the tunnel interface" }
        } catch (error: Exception) {
            descriptor.close()
            throw error
        }
        val previous = tunFd
        tunFd = descriptor
        previous?.close()
        return ip
    }

    private fun closeTun() {''')
    value = section(value, '        override fun onReconnected() {',
                    '        override fun onValidatePeerCert(', '''        override fun onReconnected() = refreshTunnel()

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

''')
    value = once(value, '    private inner class Callbacks : SessionCallbacks {', '''    private inner class Callbacks : SessionCallbacks {
        private var acceptedPin = ""

        private fun certificateMatches(): Boolean {
            val expected = profile.trustedCertificate.trim().ifEmpty { acceptedPin }
            if (expected.isEmpty()) return true // A valid system/custom CA chain.
            val actual = runCatching { session?.getPeerCertHash() }.getOrNull().orEmpty()
            return CertificatePolicy.matches(expected, actual)
        }
''')
    value = section(value, '            val pinned = profile.trustedCertificate',
                    '            val prompt = UserPrompt.CertTrust(', '''            val pinned = profile.trustedCertificate.trim().ifEmpty { acceptedPin }
            if (pinned.isNotEmpty()) {
                if (CertificatePolicy.matches(pinned, fingerprint)) return 0
                userCancelled.set(true)
                VpnBus.error("Server public key changed or the saved pin is incomplete; verify it with your administrator.")
                return -1
            }
            if (!CertificatePolicy.isFullPin(fingerprint)) return -1

''')
    value = once(value, '                if (fingerprint.isNotBlank()) host.persistCertificatePin(fingerprint)',
                 '                acceptedPin = fingerprint\n                host.persistCertificatePin(fingerprint)')
    value = once(value, '            form ?: return LibOpenConnect.OC_FORM_RESULT_ERR', '''            form ?: return LibOpenConnect.OC_FORM_RESULT_ERR
            if (!certificateMatches()) {
                userCancelled.set(true)
                return LibOpenConnect.OC_FORM_RESULT_CANCELLED
            }''')
    runner.write_text(value, encoding="utf-8")
    net = app / "app/src/main/java/dev/opentunnel/vpn/util/Net.kt"
    value = net.read_text(encoding="utf-8")
    for name in ("Inet4Address", "Inet6Address", "InetAddress"):
        value = once(value, f"import java.net.{name}\n", "")
    value = once(value, '            if (host.isEmpty()) return null',
                 '            if (!isValidIp(host)) return null')
    value = once(value, "        val ipv6 = value.contains(':')", "        if (!isValidIp(value)) return null\n        val ipv6 = value.contains(':')")
    value = once(value, '            else -> maskToPrefix(netmask) ?: if (ipv6) 128 else 32',
                 '            else -> maskToPrefix(netmask) ?: return null')
    value = section(value, '    fun ipv4DefaultMinus(', '    fun ipv4ToLong(', '''    fun ipv4DefaultMinus(excluded: List<Cidr>): List<Cidr> =
        RoutePolicy.routes(listOf(Cidr("0.0.0.0", 0, false)), excluded)

''')
    net.write_text(value, encoding="utf-8")
    service = app / "app/src/main/java/dev/opentunnel/vpn/service/OpenTunnelVpnService.kt"
    value = once(service.read_text(encoding="utf-8"), '            override fun onLost(network: Network) {',
                 '            override fun onLost(network: Network) {\n                if (network.networkHandle != lastNetworkId) return')
    service.write_text(value, encoding="utf-8")
    for source, directory in (("CertificatePolicy.kt", "main"), ("CertificatePolicyTest.kt", "test"),
                              ("ConnectionPolicy.kt", "main"), ("ConnectionPolicyTest.kt", "test"),
                              ("RoutePolicy.kt", "main"), ("RoutePolicyTest.kt", "test")):
        package = "util" if source.startswith("RoutePolicy") else "core"
        target = app / f"app/src/{directory}/java/dev/opentunnel/vpn/{package}" / source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / "mobile/android" / source, target)
