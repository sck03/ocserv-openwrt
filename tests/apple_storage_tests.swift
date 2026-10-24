import Foundation

@main
struct StorageTests {
    static func main() throws {
        let pin = "pin-sha256:" + Data(repeating: 0, count: 32).base64EncodedString()
        let valid = VPNProfile(name: "Fixture", server: "https://vpn.example.com", username: "fixture", pin: pin)
        let data = try ProfileFile.encoded([valid])
        precondition(data.count < ProfileFile.maxBytes)
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let path = directory.appendingPathComponent("profiles.json")
        try ProfileFile.replace(data, at: path)
        var many: [VPNProfile] = []
        for index in 0..<128 {
            many.append(VPNProfile(name: "Fixture \(index)", server: valid.server,
                                   username: String(repeating: "u", count: 8192), pin: pin))
        }
        do { _ = try ProfileFile.encoded(many); preconditionFailure("oversized profiles accepted") }
        catch ClientError.invalidStore { }
        let retained = try ProfileFile.readData(path, limit: ProfileFile.maxBytes)
        precondition(retained == data)
        var secret = "original", changes = 0
        enum Failure: Error { case disk }
        do {
            try CredentialTransaction.perform(password: "new", read: { secret }, write: { secret = $0; changes += 1 }, commit: { throw Failure.disk })
            preconditionFailure("file failure was hidden")
        } catch Failure.disk { }
        precondition(secret == "original" && changes == 2)
        try CredentialTransaction.perform(password: "", read: { secret }, write: { secret = $0 }, commit: {})
        precondition(secret.isEmpty)
        let attributes = try FileManager.default.attributesOfItem(atPath: path.path)
        precondition((attributes[.posixPermissions] as? NSNumber)?.intValue == 0o600)
        print("Apple aggregate profile budget, atomic replacement and credential recovery passed")
    }
}
