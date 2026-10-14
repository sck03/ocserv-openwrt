import SwiftUI
import NetworkExtension

@main
struct LinkoraApp: App {
    var body: some Scene { WindowGroup { ConnectionView() } }
}

@MainActor
final class VPNModel: ObservableObject {
    @Published var status = "未连接"
    @Published var busy = true
    @Published var connected = false
    @Published var disconnecting = false
    private var manager: NETunnelProviderManager?
    private var observer: NSObjectProtocol?

    init() {
        observer = NotificationCenter.default.addObserver(
            forName: .NEVPNStatusDidChange, object: nil, queue: .main
        ) { [weak self] notification in
            Task { @MainActor in
                guard let self, let connection = notification.object as? NEVPNConnection,
                      connection === self.manager?.connection else { return }
                self.refresh()
            }
        }
        NETunnelProviderManager.loadAllFromPreferences { [weak self] managers, error in
            Task { @MainActor in
                guard let self = self else { return }
                self.busy = false
                self.manager = managers?.first(where: {
                    ($0.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == self.tunnelID
                })
                if let error = error { self.status = error.localizedDescription }
                else { self.refresh() }
            }
        }
    }

    deinit { if let observer = observer { NotificationCenter.default.removeObserver(observer) } }
    private var tunnelID: String { Bundle.main.bundleIdentifier! + ".tunnel" }
    private func refresh() {
        let state = manager?.connection.status ?? .disconnected
        connected = [.connected, .connecting, .reasserting].contains(state)
        disconnecting = state == .disconnecting
        switch state {
        case .connected: status = "已连接"
        case .connecting: status = "正在连接"
        case .disconnecting: status = "正在断开"
        case .reasserting: status = "正在重连"
        default: status = "未连接"
        }
    }

    func disconnect() { manager?.connection.stopVPNTunnel() }

    func connect(server: String, username: String, password: String, pin: String) async {
        guard !busy && !connected && !disconnecting else { return }
        guard let url = VPNServerURL(server), VPNCredentialValid(username), VPNCredentialValid(password) else {
            status = "请填写 HTTPS 服务器、用户名和密码"; return
        }
        let fingerprint = pin.trimmingCharacters(in: .whitespacesAndNewlines)
        guard VPNServerPinValid(fingerprint) else {
            status = "请输入管理员核实的完整 pin-sha256 指纹"; return
        }
        busy = true
        defer { busy = false }
        do {
            // Load again to avoid creating duplicate system VPN profiles after launch.
            let managers = try await NETunnelProviderManager.loadAllFromPreferences()
            let active = managers.first(where: {
                ($0.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == tunnelID
            }) ?? NETunnelProviderManager()
            manager = active
            guard [.invalid, .disconnected].contains(active.connection.status) else {
                refresh(); return
            }
            let config = NETunnelProviderProtocol()
            config.providerBundleIdentifier = tunnelID
            config.serverAddress = url.absoluteString
            config.providerConfiguration = ["pin": fingerprint, "username": username]
            config.includeAllNetworks = true
            config.excludeLocalNetworks = false
            active.protocolConfiguration = config
            active.localizedDescription = "Linkora"
            active.isEnabled = true
            try await active.saveToPreferences()
            try await active.loadFromPreferences()
            // Password is passed for this connection only, never written to preferences.
            try active.connection.startVPNTunnel(options: ["password": password as NSString])
            refresh()
        } catch { status = error.localizedDescription }
    }
}

struct ConnectionView: View {
    @StateObject private var vpn = VPNModel()
    @AppStorage("server") private var server = ""
    @AppStorage("username") private var username = ""
    @AppStorage("pin") private var pin = ""
    @State private var password = ""

    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("服务器")) {
                    TextField("vpn.example.com:4443", text: $server)
                        .keyboardType(.URL).textInputAutocapitalization(.never).disableAutocorrection(true)
                    TextField("用户名", text: $username).textInputAutocapitalization(.never).disableAutocorrection(true)
                    SecureField("密码（不保存）", text: $password)
                }
                Section(header: Text("服务器证书"), footer: Text("向管理员核实完整公钥指纹。指纹不匹配时拒绝提交账号密码。")) {
                    TextField("pin-sha256:…", text: $pin)
                        .textInputAutocapitalization(.never).disableAutocorrection(true)
                }
                Section {
                    Text(vpn.status).accessibilityIdentifier("vpnStatus")
                    if vpn.connected {
                        Button("断开连接", role: .destructive) { vpn.disconnect() }
                    } else {
                        Button("连接") {
                            Task {
                                await vpn.connect(server: server, username: username, password: password, pin: pin)
                                password = ""
                            }
                        }.disabled(vpn.busy || vpn.disconnecting)
                    }
                }
                Section(footer: Text("支持 ocserv 用户名/密码认证和全隧道。首次连接需允许系统添加 VPN 配置。")) { EmptyView() }
            }.navigationTitle("Linkora")
        }.navigationViewStyle(.stack)
    }
}
