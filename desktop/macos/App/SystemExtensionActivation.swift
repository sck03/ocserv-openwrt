import Foundation
import SystemExtensions

/// Owns one activation request and resumes its continuation exactly once.
@MainActor
final class SystemExtensionActivation: NSObject, OSSystemExtensionRequestDelegate {
    private let identifier: String
    private let approval: () -> Void
    private let completed: () -> Void
    private var ready = false
    private var completion: CheckedContinuation<Void, Error>?
    private var activeRequest: OSSystemExtensionRequest?
    var pending: Bool { completion != nil }

    init(identifier: String, approval: @escaping () -> Void, completed: @escaping () -> Void) {
        self.identifier = identifier
        self.approval = approval
        self.completed = completed
    }
    func activate() async throws {
        if ready { return }
        guard !pending else { throw ClientError.extensionPending }
        try await withCheckedThrowingContinuation { continuation in
            completion = continuation
            let request = OSSystemExtensionRequest.activationRequest(forExtensionWithIdentifier: identifier, queue: .main)
            activeRequest = request
            request.delegate = self
            OSSystemExtensionManager.shared.submitRequest(request)
        }
    }
    private func finish(_ request: OSSystemExtensionRequest, error: Error?) {
        guard activeRequest === request, let continuation = completion else { return }
        activeRequest = nil
        completion = nil
        ready = error == nil
        if let error { continuation.resume(throwing: error) }
        else { continuation.resume() }
        completed()
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, actionForReplacingExtension existing: OSSystemExtensionProperties,
                            withExtension ext: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction { .replace }
    nonisolated func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        Task { @MainActor in if activeRequest === request { approval() } }
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, didFinishWithResult result: OSSystemExtensionRequest.Result) {
        Task { @MainActor in finish(request, error: result == .completed ? nil : ClientError.extensionRestart) }
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        Task { @MainActor in finish(request, error: error) }
    }
}
