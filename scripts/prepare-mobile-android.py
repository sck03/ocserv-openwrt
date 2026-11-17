"""Small, fail-closed adaptations to the checksum-pinned Android upstream."""
import shutil
import tomllib
from source_edits import once


def prepare(app, root):
    gradle = app / "app/build.gradle.kts"
    shutil.copy2(root / "mobile/android/build.gradle.kts", gradle)
    shutil.copy2(root / "mobile/android/libs.versions.toml", app / "gradle/libs.versions.toml")
    root_gradle = app / "build.gradle.kts"
    value = once(root_gradle.read_text(encoding="utf-8"),
                 '    alias(libs.plugins.kotlin.android) apply false\n', '')
    # AGP owns Kotlin compilation; select the current stable compiler explicitly.
    kotlin_version = tomllib.loads((root / "mobile/android/libs.versions.toml").read_text(encoding="utf-8"))["versions"]["kotlin"]
    root_gradle.write_text('buildscript { dependencies { classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:' +
                          kotlin_version + '") } }\n' + value, encoding="utf-8")
    wrapper = app / "gradle/wrapper/gradle-wrapper.properties"
    value = once(wrapper.read_text(encoding="utf-8"), 'gradle-8.9-bin.zip', 'gradle-9.9.1-bin.zip')
    wrapper.write_text(value + '\ndistributionSha256Sum=d8a18a7a841dd5df825d35a685fad6f2d7f12ac0c280e57b67c27b4a520ba228\n', encoding="utf-8")
    resources = app / "app/src/main/res"
    for resource in resources.glob("values*/strings.xml"):
        value = resource.read_text(encoding="utf-8")
        if 'name="app_name"' in value:
            value = once(value, '<string name="app_name">OpenTunnel</string>',
                         '<string name="app_name">岚序 Lansyra</string>')
            resource.write_text(value.replace("OpenTunnel", "岚序 Lansyra"), encoding="utf-8")
    widget = resources / "layout/widget_opentunnel_4x1.xml"
    widget.write_text(once(widget.read_text(encoding="utf-8"), 'android:text="OpenTunnel"',
                           'android:text="@string/app_name"'), encoding="utf-8")
    # These visible names bypass Android string resources in the pinned upstream.
    source = app / "app/src/main/java/dev/opentunnel/vpn"
    for file, replacements in {
        "util/Strings.kt": [('"OpenTunnel"', '"岚序 Lansyra"')],
        "ui/screens/SettingsScreen.kt": [('append("OpenTunnel ', 'append("岚序 Lansyra ')],
        "ui/screens/LogScreen.kt": [('OpenTunnel log', '岚序 Lansyra log')],
    }.items():
        path = source / file
        value = path.read_text(encoding="utf-8")
        for before, after in replacements:
            if before not in value:
                raise ValueError(f"Android branding changed in {file}")
            value = value.replace(before, after)
        path.write_text(value, encoding="utf-8")
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
    net_test = app / "app/src/test/java/dev/opentunnel/vpn/NetTest.kt"
    value = once(net_test.read_text(encoding="utf-8"), 'import dev.opentunnel.vpn.util.Net',
                 'import dev.opentunnel.vpn.util.Net\nimport dev.opentunnel.vpn.util.RoutePolicy')
    value = once(value, 'Net.ipv4DefaultMinus(excluded)',
                 'RoutePolicy.routes(listOf(Net.parseCidr("0.0.0.0/0")!!), excluded)')
    net_test.write_text(value, encoding="utf-8")
    for source, directory in (("CertificatePolicy.kt", "main"), ("CertificatePolicyTest.kt", "test"),
                              ("ConnectionPolicy.kt", "main"), ("ConnectionPolicyTest.kt", "test"),
                              ("RoutePolicy.kt", "main"), ("RoutePolicyTest.kt", "test"),
                              ("NetworkRoutes.kt", "main"), ("NetworkRoutesTest.kt", "test")):
        package = "util" if source.startswith(("RoutePolicy", "NetworkRoutes")) else "core"
        target = app / f"app/src/{directory}/java/dev/opentunnel/vpn/{package}" / source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / "mobile/android" / source, target)

    for name in ("SessionOwner", "SessionControl", "PromptWait", "DiagnosticLog", "ProfilePolicy", "ProfileCodec",
                 "CertificateFiles", "AppRoutingPolicy"):
        package = "data" if name.startswith("Profile") or name == "CertificateFiles" else "core"
        for suffix, directory in (("", "main"), ("Test", "test")):
            target = app / f"app/src/{directory}/java/dev/opentunnel/vpn/{package}/{name}{suffix}.kt"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / "mobile/android" / f"{name}{suffix}.kt", target)
    owned = root / "mobile/android/owned"
    for path in sorted(owned.rglob("*.kt")):
        target = app / "app/src/main/java/dev/opentunnel/vpn" / path.relative_to(owned)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    shutil.copy2(root / "mobile/android/AppTrafficCollector.kt", app / "app/src/main/java/dev/opentunnel/vpn/core/AppTrafficCollector.kt")
    shutil.copy2(root / "mobile/android/AppTrafficCollectorTest.kt", app / "app/src/test/java/dev/opentunnel/vpn/AppTrafficCollectorTest.kt")
