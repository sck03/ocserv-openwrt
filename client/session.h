#pragma once
#include "profile.h"
#include <openconnect.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace bulijie {
enum class State {
    Idle,
    Connecting,
    Authenticating,
    Configuring,
    Connected,
    Reconnecting,
    Disconnecting,
    Failed
};
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
        if (done.load())
            return;
        accepted = ok;
        response = std::move(value);
        done.store(true, std::memory_order_release);
        SetEvent(answered.get());
    }
};
struct Statistics {
    std::wstring ipv4, ipv6, dns, tls_cipher, dtls_cipher;
    uint64_t downloaded = 0, uploaded = 0;
};
struct Event {
    enum class Kind { State, Log, Statistics, Prompt, ProfilesChanged };
    Kind kind = Kind::State;
    State state = State::Idle;
    bool terminal = false;
    int level = 1;
    std::wstring text;
    Statistics statistics;
    std::shared_ptr<Prompt> prompt;
};
struct Protocol {
    std::string name;
    std::wstring label, description;
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
    using Sink = std::function<void(Event)>;
    Session(Profile profile, ProfileStore *store, Language language, Sink sink,
            bool authentication_only = false);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    bool start();
    void cancel();
    void request_statistics();
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
    void cleanup();
    bool prepare_script_log();
    void read_script_log();
    void state(State state, bool terminal = false, std::wstring message = {});
    void log(int level, std::wstring message);
    bool stopped() const;
    bool send(char command);
    void persist(const std::function<void(Profile &)> &change);
    std::wstring current_origin() const;
    std::string redact(std::string message) const;
    Profile profile_;
    ProfileStore *store_;
    Language language_;
    Sink sink_;
    bool authentication_only_;
    Handle cancel_event_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker_;
    std::mutex command_mutex_;
    SOCKET command_ = INVALID_SOCKET;
    openconnect_info *vpn_ = nullptr;
    std::atomic<bool> finished_{false};
    std::atomic<int> log_level_{1};
    State state_ = State::Idle;
    std::wstring error_, origin_;
    std::string pending_username_, pending_password_, pending_group_;
    std::vector<std::string> secrets_;
    unsigned forms_ = 0;
    bool group_selected_ = false, used_saved_username_ = false, used_saved_password_ = false,
         last_empty_ = false;
    bool tun_failed_ = false;
    bool tun_ready_ = false;
    std::unique_lock<std::mutex> script_environment_lock_;
    std::filesystem::path script_log_;
    std::wstring previous_script_log_;
    size_t script_log_read_ = 0;
    bool script_environment_set_ = false;
};
} // namespace bulijie
