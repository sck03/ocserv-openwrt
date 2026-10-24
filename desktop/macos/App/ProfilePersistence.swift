import Foundation
import Security
import Darwin

struct VPNProfile: Codable, Identifiable, Equatable {
    var id = UUID().uuidString
    var name = "新服务器"
    var server = ""
    var username = ""
    var pin = ""
    var rememberPassword = false
    var origin: String {
        guard let url = VPNServerURL(server) else { return "" }
        return (url.host?.lowercased() ?? "") + ":" + String(url.port ?? 443)
    }

    func validated() throws -> VPNProfile {
        guard let url = VPNServerURL(server), !name.trimmingCharacters(in: .whitespaces).isEmpty,
              name.count <= 128, VPNCredentialValid(username), VPNServerPinValid(pin), UUID(uuidString: id) != nil else {
            throw ClientError.invalidProfile
        }
        var result = self
        result.server = url.absoluteString
        return result
    }
}
enum ClientError: LocalizedError {
    case invalidProfile, missingPassword, invalidStore, keychain, keychainRecovery, login, extensionPending, extensionRestart
    var errorDescription: String? {
        switch self {
        case .invalidProfile: return "请填写名称、HTTPS 服务器、用户名和管理员核实的完整 pin-sha256 指纹。"
        case .missingPassword: return "请输入密码，或先将密码保存到钥匙串。"
        case .invalidStore: return "配置文件无效，原文件已保留。"
        case .keychain: return "无法访问钥匙串，请检查系统授权。"
        case .keychainRecovery: return "配置保存失败，钥匙串恢复未完成，请重新保存密码。"
        case .login: return "请先保存有效配置与钥匙串密码，再启用登录后自动连接。"
        case .extensionPending: return "系统扩展授权尚未结束，请先完成系统提示。"
        case .extensionRestart: return "系统扩展将在重启后启用，请重启后连接。"
        }
    }
}
struct ProfileFile: Codable {
    static let maxBytes = 1_048_576
    var schema = 1
    var profiles: [VPNProfile]

    static func encoded(_ profiles: [VPNProfile]) throws -> Data {
        guard profiles.count <= 128, Set(profiles.map(\.id)).count == profiles.count else { throw ClientError.invalidStore }
        let values = try profiles.map { try $0.validated() }
        let data = try JSONEncoder().encode(ProfileFile(profiles: values))
        guard data.count <= maxBytes else { throw ClientError.invalidStore }
        return data
    }

    static func replace(_ data: Data, at destination: URL) throws {
        guard data.count <= maxBytes else { throw ClientError.invalidStore }
        let parent = destination.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        let temporary = parent.appendingPathComponent(".profiles-" + UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: temporary) }
        try data.write(to: temporary, options: .withoutOverwriting)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: temporary.path)
        let file = try FileHandle(forWritingTo: temporary)
        do { try file.synchronize(); try file.close() } catch { try? file.close(); throw error }
        guard rename(temporary.path, destination.path) == 0 else {
            throw NSError(domain: NSPOSIXErrorDomain, code: Int(errno))
        }
    }

    static func readData(_ url: URL, limit: Int) throws -> Data {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        let data = try file.read(upToCount: limit + 1) ?? Data()
        guard data.count <= limit else { throw ClientError.invalidProfile }
        return data
    }
}
enum Credentials {
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

// A failed file replacement restores the original credential, including deletion.
// The closures also let regression tests exercise failures without a real keychain.
enum CredentialTransaction {
    static func perform(password: String, read: () throws -> String,
                        write: (String) throws -> Void, commit: () throws -> Void) throws {
        let previous = try read()
        guard password != previous else { try commit(); return }
        try write(password)
        do { try commit() } catch {
            let failure = error
            do { try write(previous) } catch { throw ClientError.keychainRecovery }
            throw failure
        }
    }
}
