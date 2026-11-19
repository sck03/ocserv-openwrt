import Foundation
import NetworkExtension
import CoreFoundation

// Whitelist the extension's diagnostic fields; never export credentials, URLs or raw errors.
enum TunnelDiagnostics {
    static func sanitize(_ data: Data?) -> [String: Any]? {
        guard let data, data.count <= 16 * 1024,
              let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let id = value["session_id"] as? String, UUID(uuidString: id) != nil,
              let generation = counter(value["generation"]),
              let phase = value["state"] as? String,
              ["idle", "connecting", "authenticating", "connected", "reconnecting", "suspended", "disconnecting", "failed"].contains(phase) else { return nil }
        var result: [String: Any] = ["session_id": id, "generation": generation, "state": phase]
        if let dropped = counter(value["dropped_to_core"]) { result["dropped_to_core"] = dropped }
        return result
    }

    private static func counter(_ value: Any?) -> UInt64? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        return UInt64(number.stringValue)
    }

    static func state(_ status: NEVPNStatus) -> String {
        switch status {
        case .connected: return "connected"
        case .connecting: return "connecting"
        case .reasserting: return "reconnecting"
        case .disconnecting: return "disconnecting"
        default: return "idle"
        }
    }

    @MainActor static func collect(_ session: NETunnelProviderSession) async -> [String: Any]? {
        await collect { reply in try session.sendProviderMessage(Data("diagnostics".utf8), responseHandler: reply) }
    }

    // An unavailable/paused extension must not leave an export waiting indefinitely.
    @MainActor static func collect(send: (@escaping (Data?) -> Void) throws -> Void) async -> [String: Any]? {
        await withCheckedContinuation { continuation in
            let reply = DiagnosticReply(continuation)
            do { try send { data in Task { @MainActor in reply.finish(data) } } }
            catch { reply.finish(nil) }
        }
    }
}

@MainActor private final class DiagnosticReply {
    private var continuation: CheckedContinuation<[String: Any]?, Never>?
    private var timeout: Task<Void, Never>?

    init(_ continuation: CheckedContinuation<[String: Any]?, Never>) {
        self.continuation = continuation
        timeout = Task { @MainActor [self] in
            try? await Task.sleep(nanoseconds: 1_000_000_000)
            if !Task.isCancelled { finish(nil) }
        }
    }

    func finish(_ data: Data?) {
        guard let continuation else { return }
        self.continuation = nil
        timeout?.cancel()
        timeout = nil
        continuation.resume(returning: TunnelDiagnostics.sanitize(data))
    }
}
