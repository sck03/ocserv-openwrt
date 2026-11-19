import SwiftUI
import NetworkExtension
import UniformTypeIdentifiers

@main
struct LansyraApp: App {
    var body: some Scene { WindowGroup { ConnectionView() } }
}

@MainActor
final class VPNModel: ObservableObject {
    @Published var status = "未连接"
    @Published var busy = true
    @Published var connected = false
    @Published var disconnecting = false
    @Published var exporting = false
    private var manager: NETunnelProviderManager?
    private var observer: NSObjectProtocol?
    private var epoch: UInt64 = 0
    private var attemptStarted = false
    private var lastError = ""

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
                self.attemptStarted = self.manager.map { ![.invalid, .disconnected].contains($0.connection.status) } ?? false
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
        case .connected: status = "已连接"; lastError = ""
        case .connecting: status = "正在连接"; lastError = ""
        case .disconnecting: status = "正在断开"
        case .reasserting: status = "正在重连"
        default:
            status = lastError.isEmpty ? "未连接" : lastError
            if attemptStarted, let connection = manager?.connection {
                attemptStarted = false
                let request = epoch
                    connection.fetchLastDisconnectError { [weak self] error in
                        Task { @MainActor in
                            guard let self, self.epoch == request,
                                  connection === self.manager?.connection,
                                  [.invalid, .disconnected].contains(connection.status) else { return }
                            self.lastError = error?.localizedDescription ?? "服务器已结束连接，请重新连接。"
                            self.status = self.lastError
                        }
                    }
            }
        }
    }

    func diagnostics() async throws -> Data {
        exporting = true
        defer { exporting = false }
        let request = epoch
        let connection = manager?.connection
        var report: [String: Any] = ["schema": 1, "product": "Lansyra", "platform": "iOS",
            "version": Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "",
            "created_utc": ISO8601DateFormatter().string(from: Date()),
            "generation": request, "state": TunnelDiagnostics.state(connection?.status ?? .disconnected)]
        if let session = connection as? NETunnelProviderSession, connected,
           let tunnel = await TunnelDiagnostics.collect(session), epoch == request,
           session === manager?.connection, connected {
            report["tunnel"] = tunnel
        }
        return try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
    }

    func disconnect() {
        epoch &+= 1
        attemptStarted = false
        lastError = ""
        manager?.connection.stopVPNTunnel()
        refresh()
    }

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
        epoch &+= 1
        let request = epoch
        lastError = ""
        defer { busy = false }
        do {
            // Load again to avoid creating duplicate system VPN profiles after launch.
            let managers = try await NETunnelProviderManager.loadAllFromPreferences()
            guard epoch == request else { return }
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
            config.includeAllNetworks = false
            config.excludeLocalNetworks = false
            active.protocolConfiguration = config
            active.localizedDescription = "岚序 Lansyra"
            active.isEnabled = true
            try await active.saveToPreferences()
            guard epoch == request else { return }
            try await active.loadFromPreferences()
            guard epoch == request else { return }
            // Password is passed for this connection only, never written to preferences.
            attemptStarted = true
            try active.connection.startVPNTunnel(options: ["password": password as NSString])
            refresh()
        } catch {
            guard epoch == request else { return }
            attemptStarted = false
            lastError = error.localizedDescription
            status = lastError
        }
    }
}

struct ConnectionView: View {
    @StateObject private var vpn = VPNModel()
    @AppStorage("server") private var server = ""
    @AppStorage("username") private var username = ""
    @AppStorage("pin") private var pin = ""
    @State private var password = ""
    @State private var importing = false
    @State private var importBusy = false
    @State private var importError: String?
    @State private var diagnosticDocument = DiagnosticDocument()
    @State private var exporting = false
    @State private var exportError: String?

    var body: some View {
        NavigationStack {
            Form {
                Section(header: Text("服务器")) {
                    Button("导入 .vpn 公共配置…") { importing = true }
                    TextField("vpn.example.com:4443", text: $server)
                        .keyboardType(.URL).textInputAutocapitalization(.never).disableAutocorrection(true)
                    TextField("用户名", text: $username).textInputAutocapitalization(.never).disableAutocorrection(true)
                    SecureField("密码（不保存）", text: $password)
                }.disabled(vpn.busy || vpn.connected || vpn.disconnecting)
                Section(header: Text("服务器证书"), footer: Text("向管理员核实完整公钥指纹。指纹不匹配时拒绝提交账号密码。")) {
                    TextField("pin-sha256:…", text: $pin)
                        .textInputAutocapitalization(.never).disableAutocorrection(true)
                }
                Section {
                    if importBusy { ProgressView("正在读取公共配置…") }
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
                Section(footer: Text("诊断文件仅包含版本、连接状态和隧道计数，不包含服务器地址、账号或密码。")) {
                    Button(vpn.exporting ? "正在收集诊断…" : "导出诊断 JSON…") {
                        Task {
                            do {
                                diagnosticDocument = DiagnosticDocument(data: try await vpn.diagnostics())
                                exporting = true
                            } catch { exportError = error.localizedDescription }
                        }
                    }.disabled(vpn.exporting)
                }
                Section(footer: Text("支持 ocserv 账号认证；路由和 DNS 随服务器配置应用。首次连接需允许系统添加 VPN 配置。")) { EmptyView() }
            }.disabled(importBusy).navigationTitle("岚序 Lansyra")
                .fileImporter(isPresented: $importing, allowedContentTypes: [UTType(filenameExtension: "vpn") ?? .data]) { result in
                    guard !importBusy else { return }
                    importBusy = true
                    Task {
                        defer { importBusy = false }
                        do {
                            let url = try result.get()
                            let value = try await Task.detached {
                                return try PublicConnection.read(url)
                            }.value
                            guard !vpn.connected && !vpn.disconnecting else { return }
                            server = value.server; pin = value.pin; username = ""; password = ""
                        } catch { importError = error.localizedDescription }
                    }
                }
                .fileExporter(isPresented: $exporting, document: diagnosticDocument, contentType: .json,
                              defaultFilename: "Lansyra-diagnostics") { result in
                    if case .failure(let error) = result { exportError = error.localizedDescription }
                }
                .alert("无法导出诊断", isPresented: Binding(get: { exportError != nil }, set: { if !$0 { exportError = nil } })) {
                    Button("关闭") { exportError = nil }
                } message: { Text(exportError ?? "") }
                .alert("无法导入配置", isPresented: Binding(get: { importError != nil }, set: { if !$0 { importError = nil } })) {
                    Button("关闭") { importError = nil }
                } message: { Text(importError ?? "") }
        }
    }
}

private struct DiagnosticDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.json] }
    var data: Data
    init(data: Data = Data()) { self.data = data }
    init(configuration: ReadConfiguration) throws {
        data = configuration.file.regularFileContents ?? Data()
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}
