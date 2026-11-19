import Foundation

@main struct DiagnosticTests {
    @MainActor static func main() async throws {
        let valid: [String: Any] = ["session_id": UUID().uuidString, "generation": 3,
                                  "state": "connected", "dropped_to_core": 2,
                                  "password": "must-not-export", "server": "must-not-export"]
        let data = try JSONSerialization.data(withJSONObject: valid)
        let clean = TunnelDiagnostics.sanitize(data)!
        precondition(clean.count == 4 && clean["password"] == nil && clean["server"] == nil)
        for field in ["generation", "dropped_to_core"] {
            for invalid in [true, -1, 1.5, "7"] as [Any] {
                var value = valid; value[field] = invalid
                let result = TunnelDiagnostics.sanitize(try JSONSerialization.data(withJSONObject: value))
                if field == "generation" { precondition(result == nil) }
                else { precondition(result?[field] == nil) }
            }
        }
        precondition(TunnelDiagnostics.sanitize(Data(repeating: 0, count: 16385)) == nil)
        precondition(TunnelDiagnostics.sanitize(Data("{}".utf8)) == nil)
        var completion: ((Data?) -> Void)?
        let missing = await TunnelDiagnostics.collect { completion = $0 }
        precondition(missing == nil)
        completion?(data) // Late and duplicate callbacks must not resume a continuation twice.
        completion?(data)
        let immediate = await TunnelDiagnostics.collect { reply in reply(data); reply(data) }
        precondition(immediate?["state"] as? String == "connected")
        struct Failure: Error {}
        let failed = await TunnelDiagnostics.collect { _ in throw Failure() }
        precondition(failed == nil)
        print("Apple diagnostic sanitization, timeout and callback lifecycle checks passed")
    }
}
