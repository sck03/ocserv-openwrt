import Foundation

enum PublicProfileError: LocalizedError {
    case invalid, unsupported
    var errorDescription: String? {
        switch self {
        case .invalid: return "公共配置无效，请使用服务端重新导出的版本 1 .vpn 文件。"
        case .unsupported: return "此客户端支持 AnyConnect 和固定公钥指纹。请在服务端选择对应的 Apple 客户端导出。"
        }
    }
}

struct PublicConnection: Sendable {
    static let maxBytes = 131_072
    var name: String
    var server: String
    var pin: String

    static func read(_ url: URL) throws -> PublicConnection {
        let accessed = url.startAccessingSecurityScopedResource()
        defer { if accessed { url.stopAccessingSecurityScopedResource() } }
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        return try parse(file.read(upToCount: maxBytes + 1) ?? Data())
    }
    static func parse(_ data: Data) throws -> PublicConnection {
        guard data.count <= maxBytes, let text = String(data: data, encoding: .utf8), !text.contains("\0") else { throw PublicProfileError.invalid }
        let allowed: Set<String> = ["Version", "Name", "Server", "Protocol", "Trust", "ServerPin", "CABase64"]
        var fields: [String: String] = [:]
        var section = false
        for raw in text.components(separatedBy: "\n") {
            let line = raw.hasSuffix("\r") ? String(raw.dropLast()) : raw
            if line.isEmpty || line.hasPrefix("#") || line.hasPrefix(";") { continue }
            if line == "[VPN]" && !section { section = true; continue }
            let pair = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard section, pair.count == 2, allowed.contains(String(pair[0])), fields[String(pair[0])] == nil else { throw PublicProfileError.invalid }
            fields[String(pair[0])] = String(pair[1])
        }
        guard fields["Version"] == "1", let url = VPNServerURL(fields["Server"] ?? "") else { throw PublicProfileError.invalid }
        guard fields["Protocol", default: "anyconnect"] == "anyconnect", fields["Trust"] == "pin" else { throw PublicProfileError.unsupported }
        let pin = fields["ServerPin"] ?? ""
        let name = fields["Name"].flatMap { $0.isEmpty ? nil : $0 } ?? String((url.host ?? "Lansyra").prefix(128))
        guard VPNServerPinValid(pin), fields["CABase64", default: ""].isEmpty, name.utf16.count <= 128,
              name.utf8.count <= 512, url.absoluteString.utf8.count <= 4096,
              !name.contains("\r"), !name.contains("\n") else { throw PublicProfileError.invalid }
        return PublicConnection(name: name, server: url.absoluteString, pin: pin)
    }
}
