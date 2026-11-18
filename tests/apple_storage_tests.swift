import Foundation

@main
struct StorageTests {
    static func main() throws {
        precondition(CommandLine.arguments.count == 2, "Expected the public profile fixture directory")
        let corpus = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        for name in ["pinned", "prompt", "ambiguous"] {
            let bytes = try Data(contentsOf: corpus.appendingPathComponent(name + ".vpn.fixture"))
            precondition(((try? PublicConnection.parse(bytes)) != nil) == (name == "pinned"))
        }
        let pin = "pin-sha256:" + Data(repeating: 0, count: 32).base64EncodedString()
        let valid = VPNProfile(name: "Fixture", server: "https://vpn.example.com", username: "fixture", pin: pin)
        let data = try ProfileFile.encoded([valid])
        let another = VPNProfile(name: "Second", server: valid.server, username: "second", pin: pin)
        precondition(ProfileFile.upsert(valid, in: [valid, another]) == [valid, another])
        let portable = try PublicConnection.parse(Data("[VPN]\nVersion=1\nServer=https://vpn.example.com\nTrust=pin\nServerPin=\(pin)\n".utf8))
        precondition(portable.pin == pin)
        for raw in ["[VPN]\nServer=https://vpn.example.com", "[VPN]\nVersion=1\nServer=https://vpn.example.com\nTrust=ca\nCABase64=invalid"] {
            do { _ = try PublicConnection.parse(Data(raw.utf8)); preconditionFailure("unsupported public profile accepted") }
            catch { }
        }
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
        var saved: [VPNProfile] = []
        var writes = 0
        var creates = 0
        var removes = 0
        func save(_ candidate: VPNProfile, _ password: String, failCommit: Bool = false) throws {
            saved = try ProfileFile.update([candidate], previous: saved, credentialID: candidate.id, credential: password,
                readCredential: { secrets[$0] ?? "" },
                createCredential: { password, id in creates += 1; try create(password, id) },
                removeCredential: { id in removes += 1; try remove(id) },
                commit: { _ in if failCommit { throw Failure.disk }; writes += 1 })
        }
        var candidate = valid
        candidate.rememberPassword = true
        try save(candidate, "original")
        let firstKey = saved[0].credentialKey
        for _ in 0..<100 { try save(saved[0], "original") }
        precondition(writes == 1 && creates == 1 && removes == 0)
        candidate = saved[0]
        candidate.name = "Renamed"
        try save(candidate, "original")
        precondition(saved[0].credentialKey == firstKey && writes == 2 && creates == 1 && removes == 0)
        do { try save(saved[0], "replacement", failCommit: true); preconditionFailure("commit failure hidden") }
        catch Failure.disk { }
        precondition(saved[0].credentialKey == firstKey && secrets == [firstKey: "original"])
        try save(saved[0], "replacement")
        precondition(saved[0].credentialKey != firstKey && secrets[saved[0].credentialKey] == "replacement")
        let replacementKey = saved[0].credentialKey
        candidate = saved[0]
        candidate.server = "https://other.example.com"
        try save(candidate, "replacement")
        precondition(saved[0].credentialKey != replacementKey && secrets[replacementKey] == nil)
        candidate = saved[0]
        candidate.rememberPassword = false
        try save(candidate, "")
        precondition(saved[0].credentialReference.isEmpty && secrets.isEmpty)
        print("Apple profile budgets, atomic credential changes and 100 unchanged saves without writes passed")
    }
}
