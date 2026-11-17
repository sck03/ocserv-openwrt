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
        var secrets = ["old": "original"]
        enum Failure: Error { case disk }
        let create: (String, String) throws -> Void = { password, id in
            precondition(secrets[id] == nil)
            secrets[id] = password
        }
        let remove: (String) throws -> Void = { secrets.removeValue(forKey: $0) }
        do {
            try CredentialTransaction.perform(previous: "old", next: "new", password: "replacement",
                create: create, remove: remove, commit: {
                    // A process exit here leaves the old file and credential valid.
                    precondition(secrets["old"] == "original" && secrets["new"] == "replacement")
                    throw Failure.disk
                })
            preconditionFailure("file failure was hidden")
        } catch Failure.disk { }
        precondition(secrets == ["old": "original"])
        try CredentialTransaction.perform(previous: "old", next: "new", password: "replacement",
            create: create, remove: remove, commit: { precondition(secrets["old"] == "original") })
        precondition(secrets == ["new": "replacement"])
        try CredentialTransaction.perform(previous: "new", next: "", password: "",
            create: create, remove: remove, commit: {})
        precondition(secrets.isEmpty)
        var referenced = valid
        referenced.credentialReference = UUID().uuidString
        var changedOrigin = referenced
        changedOrigin.server = "https://other.example.com"
        precondition(referenced.credentialKey != changedOrigin.credentialKey)
        let decoded = try JSONDecoder().decode(ProfileFile.self, from: data)
        precondition(decoded.schema == 2)
        let attributes = try FileManager.default.attributesOfItem(atPath: path.path)
        precondition((attributes[.posixPermissions] as? NSNumber)?.intValue == 0o600)
        print("Apple aggregate profile budget, atomic replacement and immutable credential transactions passed")
    }
}
