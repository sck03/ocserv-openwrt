#pragma once
#include "platform.h"

namespace vpn {
enum class State {
    Idle,
    Connecting,
    Authenticating,
    Configuring,
    Connected,
    Reconnecting,
    Suspended,
    RetryWait,
    Disconnecting,
    Failed
};
enum class ErrorCategory {
    None,
    Canceled,
    Network,
    Timeout,
    Authentication,
    Certificate,
    Configuration,
    Permission,
    Adapter,
    Busy,
    Server,
    Internal
};
struct SessionStatus {
    uint64_t session_id = 0, generation = 0, elapsed_ms = 0;
    State state = State::Idle;
    ErrorCategory error = ErrorCategory::None;
    int error_code = 0;
    unsigned attempt = 1, retry_seconds = 0;
    bool terminal = false;
};
struct Statistics {
    std::wstring ipv4, ipv6, dns, tls_cipher, dtls_cipher;
    uint64_t downloaded = 0, uploaded = 0;
};
inline constexpr unsigned RetryLimit = 3;
// Retry only transport failures, with a bounded 2/4/8-second backoff.
constexpr unsigned retry_delay(ErrorCategory error, unsigned failed_attempt) {
    return (error == ErrorCategory::Network || error == ErrorCategory::Timeout) && failed_attempt >= 1 &&
                   failed_attempt <= RetryLimit
               ? 1u << failed_attempt
               : 0;
}
const char *state_name(State state);
const char *error_name(ErrorCategory error);
std::wstring error_text(ErrorCategory error, Language language);
ErrorCategory connection_error(int code, ErrorCategory fallback);
} // namespace vpn
