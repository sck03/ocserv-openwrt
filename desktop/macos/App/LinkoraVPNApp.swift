import SwiftUI
import AppKit
import NetworkExtension
import Security
import ServiceManagement
import UniformTypeIdentifiers
import SystemExtensions

struct VPNProfile: Codable, Identifiable, Equatable {
    var id = UUID().uuidString
    var name = "新服务器"
    var server = ""
    var username = ""
    var pin = ""
    var rememberPassword = false

    func validated() throws -> VPNProfile {
        guard let url = VPNServerURL(server), !name.trimmingCharacters(in: .whitespaces).isEmpty,
              name.count <= 128, username.count <= 8192, !username.isEmpty,
              pin.hasPrefix("pin-sha256:"), let hash = Data(base64Encoded: String(pin.dropFirst(11))),
              hash.count == 32, hash.base64EncodedString() == String(pin.dropFirst(11)), UUID(uuidString: id) != nil else {
            throw ClientError.invalidProfile
        }
        var result = self
        result.server = url.absoluteString
        return result
    }
}
enum ClientError: LocalizedError {
    case invalidProfile, missingPassword, invalidStore, keychain, login, extensionPending, extensionRestart
    var errorDescription: String? {
        switch self {
        case .invalidProfile: return "请填写名称、HTTPS 服务器、用户名和管理员核实的完整 pin-sha256 指纹。"
        case .missingPassword: return "请输入密码，或先将密码保存到钥匙串。"
        case .invalidStore: return "配置文件无效，原文件已保留。"
        case .keychain: return "无法访问钥匙串，请检查系统授权。"
        case .login: return "请先保存有效配置与钥匙串密码，再启用登录后自动连接。"
        case .extensionPending: return "系统扩展授权尚未结束，请先完成系统提示。"
        case .extensionRestart: return "系统扩展将在重启后启用，请重启后连接。"
        }
    }
}
private struct ProfileFile: Codable {
    var schema = 1
    var profiles: [VPNProfile]
}
private enum Credentials {
    static func query(_ id: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: Bundle.main.bundleIdentifier! + ".password",
         kSecAttrAccount as String: id]
    }
    static func read(_ id: String) throws -> String {
        var request = query(id)
        request[kSecReturnData as String] = true
        var item: CFTypeRef?
        let status = SecItemCopyMatching(request as CFDictionary, &item)
        if status == errSecItemNotFound { return "" }
        guard status == errSecSuccess, let data = item as? Data,
              let password = String(data: data, encoding: .utf8) else { throw ClientError.keychain }
        return password
    }
    static func save(_ password: String, for id: String) throws {
        let request = query(id)
        let data = Data(password.utf8)
        let status = SecItemUpdate(request as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        if status == errSecItemNotFound {
            var item = request
            item[kSecValueData as String] = data
            item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            guard SecItemAdd(item as CFDictionary, nil) == errSecSuccess else { throw ClientError.keychain }
        } else if status != errSecSuccess { throw ClientError.keychain }
    }
    static func remove(_ id: String) throws {
        let status = SecItemDelete(query(id) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw ClientError.keychain }
    }
}

@MainActor
final class DesktopVPN: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    @Published var profiles: [VPNProfile] = []
    @Published var draft = VPNProfile()
    @Published var password = ""
    @Published var status = "未连接"
    @Published var detail = ""
    @Published var busy = false
    @Published var connected = false
    @Published var loginEnabled = false
    @Published var retryFailed = UserDefaults.standard.bool(forKey: "retryFailed") {
        didSet { UserDefaults.standard.set(retryFailed, forKey: "retryFailed") }
    }
    @Published var events: [String] = []
    private var manager: NETunnelProviderManager?
    private var observer: NSObjectProtocol?
    private var retry: Task<Void, Never>?
    private var epoch: UInt64 = 0
    private var sessionID = ""
    private var wanted = false
    private var attemptStarted = false
    private var attempt = 0
    private var sessionPassword = ""
    private var currentProfile: VPNProfile?
    private var errorCategory = "none"
    private var errorCode = 0
    private var storageReady = false
    private var extensionReady = false
    private var extensionCompletion: CheckedContinuation<Void, Error>?
    private var tunnelID: String { Bundle.main.bundleIdentifier! + ".tunnel" }
    private var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("LinkoraVPN", isDirectory: true)
    }

    override init() {
        super.init()
        do {
            let url = directory.appendingPathComponent("profiles.json")
            if FileManager.default.fileExists(atPath: url.path) {
                let data = try Data(contentsOf: url)
                guard data.count <= 1_048_576 else { throw ClientError.invalidStore }
                let file = try JSONDecoder().decode(ProfileFile.self, from: data)
                guard file.schema == 1, file.profiles.count <= 128,
                      Set(file.profiles.map(\.id)).count == file.profiles.count else { throw ClientError.invalidStore }
                profiles = try file.profiles.map { try $0.validated() }
            }
            draft = profiles.first(where: { $0.id == UserDefaults.standard.string(forKey: "selected") }) ?? profiles.first ?? VPNProfile()
            storageReady = true
        } catch { detail = ClientError.invalidStore.localizedDescription }
        loginEnabled = UserDefaults.standard.bool(forKey: "autoConnect") && SMAppService.mainApp.status != .notRegistered
        observer = NotificationCenter.default.addObserver(forName: .NEVPNStatusDidChange, object: nil, queue: .main) {
            [weak self] notification in
            Task { @MainActor in
                guard let self, let connection = notification.object as? NEVPNConnection,
                      connection === self.manager?.connection else { return }
                self.refresh()
            }
        }
        Task { await restore() }
    }
    deinit {
        retry?.cancel()
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }
    private func record(_ message: String) {
        // Only fixed state/error messages enter diagnostics. Core and authentication output are never logged.
        events.append(ISO8601DateFormatter().string(from: Date()) + " " + message)
        if events.count > 256 { events.removeFirst(events.count - 256) }
    }
    private func restore() async {
        busy = true
        do {
            let managers = try await NETunnelProviderManager.loadAllFromPreferences()
            if let active = managers.first(where: {
                ($0.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == tunnelID &&
                    ![.disconnected, .invalid].contains($0.connection.status)
            }) {
                manager = active
                let id = (active.protocolConfiguration as? NETunnelProviderProtocol)?.providerConfiguration?["profileID"] as? String
                currentProfile = profiles.first { $0.id == id }
                epoch &+= 1
                sessionID = UUID().uuidString
                attempt = 1
                wanted = true
                attemptStarted = true
                refresh()
            } else {
                busy = false
                if loginEnabled && storageReady,
                   let profile = profiles.first(where: { $0.id == UserDefaults.standard.string(forKey: "autoProfile") }) {
                    draft = profile
                    connect()
                }
            }
        } catch { busy = false; show(error) }
    }
    func select(_ profile: VPNProfile) {
        guard !busy else { return }
        draft = profile
        password = ""
        UserDefaults.standard.set(profile.id, forKey: "selected")
    }
    func newProfile() {
        guard !busy else { return }
        draft = VPNProfile()
        password = ""
    }
    private func write(_ values: [VPNProfile]) throws {
        guard storageReady else { throw ClientError.invalidStore }
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        let destination = directory.appendingPathComponent("profiles.json")
        try JSONEncoder().encode(ProfileFile(profiles: values)).write(to: destination, options: .atomic)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: destination.path)
        profiles = values
    }
    @discardableResult func save() -> Bool {
        do {
            let profile = try draft.validated()
            guard profiles.count < 128 || profiles.contains(where: { $0.id == profile.id }) else { throw ClientError.invalidStore }
            if profile.rememberPassword {
                if !password.isEmpty { try Credentials.save(password, for: profile.id) }
            } else { try Credentials.remove(profile.id) }
            var values = profiles.filter { $0.id != profile.id }
            values.append(profile)
            try write(values)
            draft = profile
            UserDefaults.standard.set(profile.id, forKey: "selected")
            detail = "配置已保存"
            return true
        } catch { show(error); return false }
    }
    func remove() {
        guard !busy else { return }
        let id = draft.id
        busy = true
        Task {
            defer { busy = false }
            do {
                let managers = try await NETunnelProviderManager.loadAllFromPreferences()
                for item in managers where (item.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == tunnelID {
                    if (item.protocolConfiguration as? NETunnelProviderProtocol)?.providerConfiguration?["profileID"] as? String == id {
                        try await item.removeFromPreferences()
                    }
                }
                try Credentials.remove(id)
                try write(profiles.filter { $0.id != id })
                if UserDefaults.standard.string(forKey: "selected") == id { setLogin(false) }
                draft = profiles.first ?? VPNProfile()
                password = ""
            } catch { show(error) }
        }
    }
    func setLogin(_ enabled: Bool) {
        do {
            if enabled {
                guard save(), draft.rememberPassword, !(try Credentials.read(draft.id)).isEmpty else { throw ClientError.login }
                try SMAppService.mainApp.register()
            } else { try SMAppService.mainApp.unregister() }
            loginEnabled = enabled
            UserDefaults.standard.set(enabled, forKey: "autoConnect")
            UserDefaults.standard.set(enabled ? draft.id : "", forKey: "autoProfile")
            if enabled && SMAppService.mainApp.status == .requiresApproval {
                detail = "请在系统设置的“登录项”中允许 Linkora VPN。"
            }
        } catch { show(error) }
    }
    func connect() {
        guard !busy, storageReady, save() else { return }
        do {
            sessionPassword = password.isEmpty && draft.rememberPassword ? try Credentials.read(draft.id) : password
            guard !sessionPassword.isEmpty else { throw ClientError.missingPassword }
            epoch &+= 1
            sessionID = UUID().uuidString
            wanted = true
            busy = true
            attempt = 1
            currentProfile = draft
            beginAttempt(epoch)
        } catch { show(error) }
    }
    func disconnect() {
        epoch &+= 1
        wanted = false
        attemptStarted = false
        retry?.cancel()
        retry = nil
        sessionPassword = ""
        password = ""
        manager?.connection.stopVPNTunnel()
        record("canceled")
        refresh()
    }
    private func beginAttempt(_ request: UInt64) {
        guard wanted, request == epoch, let profile = currentProfile else { return }
        status = "正在连接…"
        detail = ""
        errorCategory = "none"
        record("connecting; attempt=\(attempt)")
        Task {
            do {
                try await activateExtension()
                guard wanted, request == epoch else { return }
                let managers = try await NETunnelProviderManager.loadAllFromPreferences()
                guard wanted, request == epoch else { return }
                let active = managers.first(where: {
                    let config = $0.protocolConfiguration as? NETunnelProviderProtocol
                    return config?.providerBundleIdentifier == tunnelID && config?.providerConfiguration?["profileID"] as? String == profile.id
                }) ?? NETunnelProviderManager()
                let config = NETunnelProviderProtocol()
                config.providerBundleIdentifier = tunnelID
                config.serverAddress = profile.server
                config.providerConfiguration = ["profileID": profile.id, "pin": profile.pin, "username": profile.username]
                config.includeAllNetworks = true
                config.excludeLocalNetworks = false
                config.disconnectOnSleep = false
                active.protocolConfiguration = config
                active.localizedDescription = "Linkora VPN — " + profile.name
                active.isEnabled = true
                try await active.saveToPreferences()
                guard wanted, request == epoch else { return }
                try await active.loadFromPreferences()
                guard wanted, request == epoch else { return }
                manager = active
                attemptStarted = true
                try active.connection.startVPNTunnel(options: ["password": sessionPassword as NSString])
                password = ""
                refresh()
            } catch {
                guard wanted, request == epoch else { return }
                attemptStarted = false
                finish(error, request: request)
            }
        }
    }
    private func refresh() {
        let state = manager?.connection.status ?? .disconnected
        connected = state == .connected
        busy = wanted || extensionCompletion != nil || ![.invalid, .disconnected].contains(state)
        switch state {
        case .connected: status = "已连接"; detail = ""; record("connected")
        case .connecting: status = "正在验证身份并建立隧道…"
        case .reasserting: status = "正在恢复网络连接…"; record("reconnecting")
        case .disconnecting: status = "正在断开并清理网络…"
        default:
            if wanted && attemptStarted, let connection = manager?.connection {
                attemptStarted = false
                let request = epoch
                connection.fetchLastDisconnectError { [weak self] error in
                    Task { @MainActor in self?.finish(error, request: request) }
                }
            } else if !wanted { status = "未连接" }
        }
    }
    private func finish(_ error: Error?, request: UInt64) {
        guard wanted, request == epoch else { return }
        let failure = error as NSError?
        errorCode = failure?.code ?? 0
        let categories = [1: "canceled", 2: "network", 3: "timeout", 4: "authentication", 5: "certificate",
                          6: "configuration", 7: "permission", 8: "adapter", 9: "busy", 10: "server", 11: "internal"]
        errorCategory = failure?.domain == "io.github.sck03.linkoravpn" ? categories[errorCode] ?? "internal" : "system"
        record("failed; category=\(errorCategory); code=\(errorCode)")
        if retryFailed, ["network", "timeout"].contains(errorCategory), attempt <= 3 {
            let seconds = 1 << attempt
            retry = Task {
                for remaining in stride(from: seconds, through: 1, by: -1) {
                    guard wanted, epoch == request, !Task.isCancelled else { return }
                    status = "\(remaining) 秒后重试（\(attempt)/3）"
                    do { try await Task.sleep(nanoseconds: 1_000_000_000) } catch { return }
                }
                guard wanted, epoch == request, !Task.isCancelled else { return }
                attempt += 1
                beginAttempt(request)
            }
        } else {
            wanted = false
            busy = false
            connected = false
            sessionPassword = ""
            status = "连接失败"
            detail = failure?.localizedDescription ?? "服务器已结束会话，请重新连接。"
        }
    }
    private func show(_ error: Error) {
        detail = error.localizedDescription
    }
    private func activateExtension() async throws {
        if extensionReady { return }
        guard extensionCompletion == nil else { throw ClientError.extensionPending }
        try await withCheckedThrowingContinuation { continuation in
            extensionCompletion = continuation
            let request = OSSystemExtensionRequest.activationRequest(forExtensionWithIdentifier: tunnelID, queue: .main)
            request.delegate = self
            OSSystemExtensionManager.shared.submitRequest(request)
        }
    }
    func request(_ request: OSSystemExtensionRequest, actionForReplacingExtension existing: OSSystemExtensionProperties,
                 withExtension ext: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction { .replace }
    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        detail = "请在系统设置中允许 Linkora VPN 系统扩展。"
    }
    func request(_ request: OSSystemExtensionRequest, didFinishWithResult result: OSSystemExtensionRequest.Result) {
        extensionReady = result == .completed
        let completion = extensionCompletion
        extensionCompletion = nil
        if extensionReady { completion?.resume() }
        else { completion?.resume(throwing: ClientError.extensionRestart) }
        if !wanted { refresh() }
    }
    func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        let completion = extensionCompletion
        extensionCompletion = nil
        completion?.resume(throwing: error)
        if !wanted { refresh() }
    }
    func importProfile() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "vpn") ?? .plainText]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let data = try Data(contentsOf: url)
            guard data.count <= 65536, let text = String(data: data, encoding: .utf8) else { throw ClientError.invalidProfile }
            var values: [String: String] = [:]
            var section = false
            for line in text.components(separatedBy: .newlines) {
                if line.hasPrefix("[") { section = line == "[VPN]"; continue }
                let pair = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
                if section && pair.count == 2 { values[String(pair[0])] = String(pair[1]) }
            }
            guard values["Protocol", default: "anyconnect"] == "anyconnect" else { throw ClientError.invalidProfile }
            draft = VPNProfile(name: values["Name"] ?? url.deletingPathExtension().lastPathComponent,
                               server: values["Server"] ?? "", username: "", pin: values["ServerPin"] ?? "")
            password = ""
            detail = "已读取服务器配置；填写用户名和密码后保存。"
        } catch { show(error) }
    }
    func exportDiagnostics() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "LinkoraVPN-diagnostics.json"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let snapshot: [String: Any] = ["schema": 1, "product": "Linkora VPN", "platform": "macOS",
            "version": Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "",
            "created_utc": ISO8601DateFormatter().string(from: Date()), "session_id": sessionID,
            "generation": epoch, "attempt": attempt, "state": status, "error_category": errorCategory,
            "error_code": errorCode, "events": events]
        do { try JSONSerialization.data(withJSONObject: snapshot, options: [.prettyPrinted, .sortedKeys]).write(to: url, options: .atomic) }
        catch { show(error) }
    }
}

struct ConnectionView: View {
    @ObservedObject var vpn: DesktopVPN
    var body: some View {
        HStack(spacing: 0) {
            VStack {
                List(vpn.profiles) { profile in
                    Button(profile.name) { vpn.select(profile) }.buttonStyle(.plain).disabled(vpn.busy)
                }
                HStack {
                    Button("新建") { vpn.newProfile() }
                    Button("删除") { vpn.remove() }
                }.disabled(vpn.busy)
                Button("导入 .vpn 配置…") { vpn.importProfile() }.disabled(vpn.busy).padding(.bottom)
            }.frame(width: 185)
            Divider()
            VStack(alignment: .leading, spacing: 16) {
                Text("Linkora VPN").font(.title2.weight(.semibold))
                Form {
                    TextField("名称", text: $vpn.draft.name)
                    TextField("HTTPS 服务器", text: $vpn.draft.server)
                    TextField("用户名", text: $vpn.draft.username)
                    SecureField("密码", text: $vpn.password)
                    TextField("pin-sha256 指纹", text: $vpn.draft.pin)
                    Toggle("在钥匙串中保存密码", isOn: $vpn.draft.rememberPassword)
                }.disabled(vpn.busy)
                Text("请使用管理员核实的服务器公钥指纹。支持 ocserv 用户名和密码认证。")
                    .font(.caption).foregroundStyle(.secondary)
                Toggle("登录后自动启动并连接", isOn: Binding(get: { vpn.loginEnabled }, set: vpn.setLogin)).disabled(vpn.busy)
                Toggle("网络失败后重试（最多 3 次）", isOn: $vpn.retryFailed).disabled(vpn.busy)
                HStack {
                    Circle().fill(vpn.connected ? Color.green : vpn.busy ? Color.blue : Color.secondary).frame(width: 8, height: 8)
                    Text(vpn.status).fontWeight(.medium)
                }
                Text(vpn.detail).font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
                HStack {
                    Button("保存") { vpn.save() }.disabled(vpn.busy)
                    Button(vpn.busy ? "断开 / 取消" : "连接") { vpn.busy ? vpn.disconnect() : vpn.connect() }
                        .keyboardShortcut(.defaultAction)
                    Spacer()
                    Button("导出诊断…") { vpn.exportDiagnostics() }
                }
            }.padding(24).frame(minWidth: 440, minHeight: 450)
        }.frame(minWidth: 660, minHeight: 510)
    }
}
@main
struct LinkoraVPNApp: App {
    @StateObject private var vpn = DesktopVPN()
    var body: some Scene {
        WindowGroup("Linkora VPN") { ConnectionView(vpn: vpn) }.windowResizability(.contentSize)
        MenuBarExtra("Linkora VPN", systemImage: "network") {
            Text(vpn.status)
            Button("显示窗口") {
                NSApp.activate(ignoringOtherApps: true)
                NSApp.windows.first?.makeKeyAndOrderFront(nil)
            }
            Button(vpn.busy ? "断开连接" : "连接") { vpn.busy ? vpn.disconnect() : vpn.connect() }
            Button("导出诊断…") { vpn.exportDiagnostics() }
        }
    }
}
