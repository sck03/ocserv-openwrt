"""Small, fail-closed adaptations to the checksum-pinned Android upstream."""
from pathlib import Path
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
    for source, directory in (("CertificatePolicy.kt", "main"), ("CertificatePolicyTest.kt", "test")):
        target = app / f"app/src/{directory}/java/dev/opentunnel/vpn/core" / source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / "mobile/android" / source, target)
