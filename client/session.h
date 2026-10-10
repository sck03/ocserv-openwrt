#pragma once
#include "profile.h"
#include "diagnostics.h"
#include <openconnect.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace vpn {
struct Choice {
    std::wstring label;
    std::string value;
};
struct Prompt {
    enum class Kind { Text, Password, Selection, Certificate, Notice };
    Kind kind = Kind::Text;
    std::wstring title, label, banner, message, error, server, details;
    std::string field, initial, pin, previous_pin;
    std::vector<Choice> choices;
    Handle answered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> done{false};
    std::mutex answer_mutex;
    bool accepted = false;
    std::string response;
    ~Prompt() {
        erase(response);
        erase(initial);
    }
    void answer(bool ok, std::string value = {}) {
        std::lock_guard<std::mutex> lock(answer_mutex);
        if (done.load()) {
            erase(value);
            return;
        }
        accepted = ok;
        response = std::move(value);
        done.store(true, std::memory_order_release);
        SetEvent(answered.get());
    }
};
struct Event : SessionStatus {
    enum class Kind { State, Log, Statistics, Prompt, ProfilesChanged };
    Kind kind = Kind::State;
    std::wstring text;
    Statistics statistics;
    std::shared_ptr<Prompt> prompt;
};
struct Protocol {
    std::string name;
    std::wstring label;
};
struct SystemCertificate {
    std::wstring label, certificate, key;
};
std::vector<Protocol> supported_protocols();
std::vector<SystemCertificate> system_certificates();
bool certificate_file_info(const std::filesystem::path &path, std::wstring &fingerprint, std::wstring &error);
bool verify_peer_with_windows(openconnect_info *vpn, const std::wstring &hostname,
                              const std::wstring &ca_file, DWORD &error);
bool peer_certificate_in_date(openconnect_info *vpn, DWORD &error);

class Session {
public:
    struct Options {
        bool authentication_only = false;
        bool retry_failed = false;
    };
    using Sink = std::function<void(Event)>;
    Session(Profile profile, ProfileStore *store, Language language, Sink sink, Options options);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    bool start();
    void cancel();
    void network_changed();
    void suspend(bool value);
    void request_statistics();
    uint64_t id() const {
        return id_;
    }
    void set_log_level(int level) {
        log_level_.store(level);
    }
    bool finished() const {
        return finished_.load();
    }

private:
    static int certificate_callback(void *context, const char *reason);
    static int authentication_callback(void *context, oc_auth_form *form);
    static void progress_callback(void *context, int level, const char *format, ...);
    static void statistics_callback(void *context, const oc_stats *stats);
    static void setup_tun_callback(void *context);
    static void reconnected_callback(void *context);
    static int lock_token_callback(void *context);
    static int unlock_token_callback(void *context, const char *token);
    int authenticate(oc_auth_form *form);
    int validate_certificate(const char *reason);
    bool ask(const std::shared_ptr<Prompt> &prompt);
    void run();
    bool run_attempt();
    void cleanup() noexcept;
    bool wait_until_resumed();
    void emit(Event event);
    bool fail(ErrorCategory category, int code = 0);
    bool prepare_script_log();
    void read_script_log();
    void state(State state, bool terminal = false, std::wstring message = {});
    void log(int level, std::wstring message);
    bool stopped() const;
    bool send(char command);
    void persist(const std::function<void(Profile &)> &change);
    std::wstring current_origin() const;
    Profile profile_;
    ProfileStore *store_;
    Language language_;
    Sink sink_;
    Options options_;
    const uint64_t id_;
    uint64_t generation_ = 0, started_ = 0;
    unsigned attempt_ = 1, retry_seconds_ = 0;
    ErrorCategory error_category_ = ErrorCategory::None;
    int error_code_ = 0;
    unsigned http_status_ = 0;
    Handle cancel_event_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle resumed_event_{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    std::atomic<bool> suspended_{false}, tunnel_loop_{false}, pause_pending_{false};
    std::thread worker_;
    std::mutex command_mutex_;
    SOCKET command_ = INVALID_SOCKET;
    openconnect_info *vpn_ = nullptr;
    std::atomic<bool> finished_{false};
    std::atomic<int> log_level_{1};
    State state_ = State::Idle;
    std::wstring error_, origin_;
    std::string pending_username_, pending_password_, pending_group_;
    Redactor redactor_;
    unsigned forms_ = 0;
    bool group_selected_ = false, used_saved_username_ = false, used_saved_password_ = false,
         last_empty_ = false;
    bool tun_failed_ = false;
    bool tun_ready_ = false;
    bool authentication_rejected_ = false;
    bool cleaning_ = false, cleanup_failed_ = false;
    std::unique_lock<std::mutex> script_environment_lock_;
    std::filesystem::path script_log_;
    std::wstring previous_script_log_;
    size_t script_log_read_ = 0;
    bool script_environment_set_ = false;
};
} // namespace vpn
