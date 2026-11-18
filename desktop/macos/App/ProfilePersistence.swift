import Foundation
import Security
import Darwin

struct VPNProfile: Codable, Identifiable, Equatable, Sendable {
    var id = UUID().uuidString
    var name = "新服务器"
    var server = ""
    var username = ""
    var pin = ""
    var rememberPassword = false
    var credentialReference = ""
    var credentialKey: String { credentialReference.isEmpty ? "" : id + "/" + origin + "/" + credentialReference }
    var origin: String {
        guard let url = VPNServerURL(server) else { return "" }
        return (url.host?.lowercased() ?? "") + ":" + String(url.port ?? 443)
    }

    func validated() throws -> VPNProfile {
        guard let url = VPNServerURL(server), !name.trimmingCharacters(in: .whitespaces).isEmpty,
              name.count <= 128, (credentialReference.isEmpty || UUID(uuidString: credentialReference) != nil), VPNCredentialValid(username), VPNServerPinValid(pin), UUID(uuidString: id) != nil else {
            throw ClientError.invalidProfile
        }
        var result = self
        result.server = url.absoluteString
        return result
    }
}

// All synchronous filesystem and Keychain work is serialized off MainActor.
// The UI submits value snapshots; this actor never owns view or tunnel state.
actor ProfileRepository {
    private let directory: URL
    init(directory: URL) { self.directory = directory }

    func load() throws -> [VPNProfile] {
        let url = directory.appendingPathComponent("profiles.json")
        guard FileManager.default.fileExists(atPath: url.path) else { return [] }
        let data = try ProfileFile.readData(url, limit: ProfileFile.maxBytes)
        let file = try JSONDecoder().decode(ProfileFile.self, from: data)
        guard file.schema == 2, file.profiles.count <= 128,
              Set(file.profiles.map(\.id)).count == file.profiles.count else { throw ClientError.invalidStore }
        return try file.profiles.map { try $0.validated() }
    }

    func credential(_ profile: VPNProfile) throws -> String {
        guard profile.rememberPassword, !profile.credentialKey.isEmpty else { return "" }
        return try Credentials.read(profile.credentialKey)
    }

    func write(_ values: [VPNProfile], credentialID: String, credential: String) throws -> [VPNProfile] {
        let destination = directory.appendingPathComponent("profiles.json")
        return try ProfileFile.update(values, previous: load(), credentialID: credentialID, credential: credential,
            readCredential: Credentials.read, createCredential: Credentials.save, removeCredential: Credentials.remove,
            commit: { try ProfileFile.replace($0, at: destination) })
    }

    func importProfile(_ url: URL) throws -> VPNProfile {
        let value = try PublicConnection.read(url)
        return VPNProfile(name: value.name, server: value.server, username: "", pin: value.pin)
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
struct ProfileFile: Codable {
    static let maxBytes = 1_048_576
    var schema = 2
    var profiles: [VPNProfile]

    static func upsert(_ profile: VPNProfile, in profiles: [VPNProfile]) -> [VPNProfile] {
        var values = profiles
        if let index = values.firstIndex(where: { $0.id == profile.id }) { values[index] = profile }
        else { values.append(profile) }
        return values
    }

    static func update(_ values: [VPNProfile], previous: [VPNProfile], credentialID: String, credential: String,
                       readCredential: (String) throws -> String,
                       createCredential: (String, String) throws -> Void,
                       removeCredential: (String) throws -> Void,
                       commit: (Data) throws -> Void) throws -> [VPNProfile] {
        var updated = try values.map { try $0.validated() }
        let old = previous.first { $0.id == credentialID }
        if let index = updated.firstIndex(where: { $0.id == credentialID }) {
            guard credential.isEmpty || updated[index].rememberPassword else { throw ClientError.invalidProfile }
            var reference = ""
            if !credential.isEmpty {
                if let old, old.rememberPassword, !old.credentialKey.isEmpty,
                   old.origin == updated[index].origin, try readCredential(old.credentialKey) == credential {
                    reference = old.credentialReference
                } else {
                    reference = UUID().uuidString
                }
            }
            updated[index].credentialReference = reference
        }
        // Comparing normalized values avoids new file and Keychain versions on every connection.
        if updated == previous { return updated }
        let data = try encoded(updated)
        let next = updated.first { $0.id == credentialID }
        try CredentialTransaction.perform(previous: old?.credentialKey ?? "", next: next?.credentialKey ?? "",
            password: credential, create: createCredential, remove: removeCredential,
            commit: { try commit(data) })
        return updated
    }

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
        var item = query(id)
        item[kSecValueData as String] = Data(password.utf8)
        item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        guard SecItemAdd(item as CFDictionary, nil) == errSecSuccess else { throw ClientError.keychain }
    }
    static func remove(_ id: String) throws {
        let status = SecItemDelete(query(id) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw ClientError.keychain }
    }
}

// Immutable versions keep the old file valid even if the process exits before commit.
// Cleanup failures leave an unreferenced key; they cannot redirect a saved credential.
enum CredentialTransaction {
    static func perform(previous: String, next: String, password: String,
                        create: (String, String) throws -> Void,
                        remove: (String) throws -> Void, commit: () throws -> Void) throws {
        if next == previous { try commit(); return }
        if !next.isEmpty { try create(password, next) }
        do { try commit() } catch {
            if !next.isEmpty { try? remove(next) }
            throw error
        }
        if !previous.isEmpty { try? remove(previous) }
    }
}
