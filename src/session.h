#pragma once
#include "common.h"
#include "network.h"
#include <functional>
#include <cstdarg>
#include <atomic>

namespace bridge {
struct ConnectOptions {
    Profile profile;
    std::string username;
    std::string password;
    bool authentication_only = false; // Used by local protocol verification; never enabled by the GUI.
};
struct Event {
    State state = State::Idle;
    Error error = Error::None;
    bool terminal = false;
    bool statistics = false;
    std::wstring detail;
    std::wstring address;
    std::wstring transport;
    uint64_t rx = 0;
    uint64_t tx = 0;
};
class Session {
public:
    using Sink = std::function<void(Event)>;
    Session(ConnectOptions options, Sink sink);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool start();
    void cancel();
    void request_stats();
    bool finished() const;
private:
    static DWORD WINAPI thread_entry(void* context);
    static int certificate_callback(void* context, const char* reason);
    static int auth_callback(void* context, oc_auth_form* form);
    static void progress_callback(void* context, int level, const char* format, ...);
    static void stats_callback(void* context, const oc_stats* stats);
    static void reconnected_callback(void* context);
    void run();
    void cleanup();
    bool send(char command);
    void emit(State state, Error error = Error::None, bool terminal = false, std::wstring detail = {});
    int validate_certificate();
    int authenticate(oc_auth_form* form);
    bool same_origin() const;
    bool stopping() const;
    ConnectOptions options_;
    Sink sink_;
    Handle thread_;
    Handle cancel_event_;
    CRITICAL_SECTION command_lock_{};
    SOCKET command_ = INVALID_SOCKET;
    openconnect_info* vpn_ = nullptr; // Owned and accessed exclusively by the worker thread.
    NetworkConfig network_;
    State state_ = State::Idle;
    Error error_ = Error::None;
    std::wstring detail_;
    std::wstring last_protocol_error_;
    std::string origin_host_;
    int origin_port_ = 0;
    unsigned password_submissions_ = 0;
    unsigned auth_forms_ = 0;
    bool connected_once_ = false;
    bool cleanup_failed_ = false;
    std::atomic<bool> statistics_pending_{false};
};
int verify_windows_certificate(openconnect_info* vpn, const std::wstring& hostname, const std::wstring& ca_file, DWORD& error);
}
