// Native adaptation of the callback lifecycle in OpenConnect GUI v1.6.2.
// Upstream: Copyright (C) 2014 Red Hat, GPL-2.0-or-later.
#include "session.h"
#include "system_proxy.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace vpn {
namespace {
std::mutex script_environment_mutex;
std::atomic<uint64_t> next_session{0};
std::wstring safe(const char *value) {
    return value ? wide(value) : std::wstring();
}
} // namespace
Session::Session(Profile profile, ProfileStore *store, Language language, Sink sink, Options options)
    : profile_(std::move(profile)), store_(store), language_(language), sink_(std::move(sink)),
      options_(options), id_(++next_session) {
    if (profile_.log_level >= 0)
        log_level_ = profile_.log_level;
    redactor_.remember(profile_.password);
    redactor_.remember(profile_.token);
    redactor_.remember(profile_.username);
    redactor_.remember(profile_.group);
}
Session::~Session() {
    cancel();
    if (worker_.joinable())
        worker_.join();
    erase(profile_.password);
    erase(profile_.token);
    erase(pending_password_);
}
bool Session::start() {
    if (worker_.joinable() || !cancel_event_ || !resumed_event_)
        return false;
    started_ = GetTickCount64();
    try {
        worker_ = std::thread([this] {
            try {
                run();
            } catch (...) {
                error_ = tr(language_, L"连接处理失败。", L"Connection processing failed.");
                cleanup();
                fail(ErrorCategory::Internal);
                state(State::Failed, true, error_);
            }
            erase(profile_.password);
            erase(profile_.token);
            erase(pending_password_);
            redactor_.clear();
            finished_.store(true, std::memory_order_release);
        });
        return true;
    } catch (...) {
        return false;
    }
}
bool Session::stopped() const {
    return cancel_requested_.load(std::memory_order_acquire);
}
bool Session::send(char command) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    return command_ != INVALID_SOCKET && ::send(command_, &command, 1, 0) == 1;
}
void Session::cancel() {
    if (cancel_requested_.exchange(true))
        return;
    SetEvent(cancel_event_.get());
    send(OC_CMD_CANCEL);
}
void Session::network_changed() {
    if (!stopped() && tunnel_loop_.load() && !pause_pending_.exchange(true)) {
        if (!send(OC_CMD_PAUSE))
            pause_pending_.store(false);
    }
}
void Session::suspend(bool value) {
    suspended_.store(value);
    if (value)
        ResetEvent(resumed_event_.get());
    else
        SetEvent(resumed_event_.get());
    network_changed();
}
bool Session::wait_until_resumed() {
    if (suspended_.load()) {
        state(State::Suspended);
        HANDLE handles[] = {cancel_event_.get(), resumed_event_.get()};
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
            return false;
        if (!stopped())
            state(tun_ready_ ? State::Reconnecting : State::Connecting);
    }
    return !stopped();
}
void Session::request_statistics() {
    if (!stopped() && tunnel_loop_.load() && !statistics_pending_.exchange(true))
        if (!send(OC_CMD_STATS))
            statistics_pending_.store(false);
}
void Session::state(State value, bool terminal, std::wstring message) {
    if (stopped() && !terminal && value != State::Disconnecting)
        return;
    state_ = value;
    ++generation_;
    Event event;
    event.state = value;
    event.terminal = terminal;
    event.text = std::move(message);
    emit(std::move(event));
}
void Session::emit(Event event) {
    synchronize_log_level();
    event.session_id = id_;
    event.generation = generation_;
    event.elapsed_ms = GetTickCount64() - started_;
    event.attempt = attempt_;
    event.retry_seconds = retry_seconds_;
    event.error = error_category_;
    event.error_code = error_code_;
    if (event.kind == Event::Kind::State && !event.text.empty())
        event.text = wide(redactor_.clean(utf8(event.text)));
    sink_(std::move(event));
}
bool Session::fail(ErrorCategory category, int code) {
    error_category_ = category;
    error_code_ = code;
    return false;
}
void Session::synchronize_log_level() {
    // Every caller runs on the worker. The UI only publishes an atomic level
    // and wakes the command pipe, never accesses the live core directly.
    if (vpn_)
        openconnect_set_loglevel(vpn_, log_level_.load());
}
void Session::log(int level, std::string message) {
    if (level > log_level_.load())
        return;
    // Redact the complete message before splitting, including secrets that
    // cross a chunk boundary. Queue entries have a fixed maximum size.
    auto text = wide(redactor_.clean(std::move(message)));
    for (size_t begin = 0; begin < text.size(); begin += 8192) {
        Event event;
        event.kind = Event::Kind::Log;
        event.text = text.substr(begin, 8192);
        emit(std::move(event));
    }
}
void Session::progress_callback(void *context, int level, const char *format, ...) {
    auto *self = static_cast<Session *>(context);
    self->synchronize_log_level();
    if (strcmp(format, "Got HTTP response: %s\n") == 0) {
        va_list response;
        va_start(response, format);
        const char *line = va_arg(response, const char *);
        unsigned status = 0;
        if (line && sscanf(line, "HTTP/%*u.%*u %u", &status) == 1)
            self->http_status_ = status;
        va_end(response);
    }
    // This is the core's format string, not untrusted server text. Script
    // failures must never be retried as transient network failures.
    if (strstr(format, "Script '%s' failed for %s")) {
        va_list failure;
        va_start(failure, format);
        (void)va_arg(failure, const char *);
        const char *reason = va_arg(failure, const char *);
        int code = va_arg(failure, int);
        va_end(failure);
        self->tun_failed_ = true;
        if (self->cleaning_ || (reason && strcmp(reason, "disconnect") == 0))
            self->cleanup_failed_ = true;
        self->fail(ErrorCategory::Adapter, code);
    }
    if (self->state_ == State::Connected &&
        (strstr(format, "remaining timeout") || strstr(format, "SSL connection failure"))) {
        try {
            self->state(State::Reconnecting);
        } catch (...) {
        }
    }
    if (level > self->log_level_.load())
        return;
    char buffer[8192]{};
    va_list args;
    va_start(args, format);
    int size = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    try {
        self->log(level, size < 0 || static_cast<size_t>(size) >= sizeof(buffer)
                             ? "[oversized log entry omitted]"
                             : buffer);
    } catch (...) {
    }
    SecureZeroMemory(buffer, sizeof(buffer));
}
void Session::statistics_callback(void *context, const oc_stats *stats) {
    auto *self = static_cast<Session *>(context);
    self->synchronize_log_level();
    self->statistics_pending_.store(false);
    if (!stats || self->stopped())
        return;
    try {
        Event event;
        event.kind = Event::Kind::Statistics;
        event.statistics.downloaded = stats->rx_bytes;
        event.statistics.uploaded = stats->tx_bytes;
        const oc_ip_info *info = nullptr;
        if (openconnect_get_ip_info(self->vpn_, &info, nullptr, nullptr) == 0 && info) {
            event.statistics.ipv4 = safe(info->addr);
            event.statistics.ipv6 = safe(info->addr6);
            for (const char *dns : info->dns)
                if (dns) {
                    if (!event.statistics.dns.empty())
                        event.statistics.dns += L", ";
                    event.statistics.dns += wide(dns);
                }
        }
        event.statistics.tls_cipher = safe(openconnect_get_cstp_cipher(self->vpn_));
        event.statistics.dtls_cipher = safe(openconnect_get_dtls_cipher(self->vpn_));
        self->emit(std::move(event));
    } catch (...) {
    }
}
void Session::setup_tun_callback(void *context) {
    auto *self = static_cast<Session *>(context);
    try {
        if (!self->wait_until_resumed())
            return;
        self->state(State::Configuring);
        if (!self->update_network_policy()) {
            self->tun_failed_ = true;
            self->fail(ErrorCategory::Adapter);
            self->send(OC_CMD_CANCEL);
            return;
        }
        auto script = self->profile_.script.empty() ? executable_directory() / L"vpnc-script-win.js"
                                                    : std::filesystem::path(self->profile_.script);
        std::string interface_name = self->profile_.interface_name.empty()
                                         ? "Lansyra-" + self->profile_.id.substr(0, 12)
                                         : utf8(self->profile_.interface_name);
        int result =
            openconnect_setup_tun_device(self->vpn_, utf8(script.wstring()).c_str(), interface_name.c_str());
        self->read_script_log();
        if (result != 0) {
            self->tun_failed_ = true;
            self->fail(ErrorCategory::Adapter, result);
            self->error_ = tr(self->language_, L"无法创建或配置 VPN 网卡。请检查管理员权限和日志。",
                              L"Could not create or configure the VPN adapter. Check administrator "
                              L"privileges and the log.");
            self->send(OC_CMD_CANCEL);
        } else {
            self->tun_ready_ = true;
            if (!self->stopped()) {
                self->state(State::Connected);
                self->request_statistics();
            }
        }
    } catch (...) {
        self->tun_failed_ = true;
        self->fail(ErrorCategory::Adapter);
        self->send(OC_CMD_CANCEL);
    }
}
void Session::reconnected_callback(void *context) {
    auto *self = static_cast<Session *>(context);
    try {
        self->read_script_log();
        if (!self->update_network_policy()) {
            self->tun_failed_ = true;
            self->fail(ErrorCategory::Adapter);
            self->send(OC_CMD_CANCEL);
            return;
        }
        if (!self->stopped() && self->tun_ready_ && !self->tun_failed_)
            self->state(State::Connected);
    } catch (...) {
    }
}
bool Session::update_network_policy() {
    const oc_ip_info *info = nullptr;
    if (openconnect_get_ip_info(vpn_, &info, nullptr, nullptr) || !info)
        return false;
    std::vector<std::string> included;
    for (auto *route = info->split_includes; route; route = route->next) {
        if (included.size() >= 4096) {
            error_ = tr(language_, L"服务器下发的路由过多。", L"The gateway supplied too many routes.");
            return false;
        }
        included.emplace_back(route->route ? route->route : "");
    }
    const auto coverage = ipv4_coverage(included);
    if (coverage == Ipv4Coverage::Invalid) {
        error_ = tr(language_, L"服务器下发的 IPv4 路由无效。", L"The gateway supplied invalid IPv4 routes.");
        return false;
    }
    const bool ipv6 = (info->addr6 && *info->addr6) || (info->netmask6 && *info->netmask6);
    return ipv6_guard_.require(!ipv6 && coverage == Ipv4Coverage::Full, error_);
}
bool Session::prepare_script_log() {
    script_environment_lock_ = std::unique_lock<std::mutex>(script_environment_mutex, std::try_to_lock);
    if (!script_environment_lock_.owns_lock()) {
        error_ =
            tr(language_, L"当前实例已有一个 VPN 连接。", L"This instance already has a VPN connection.");
        return false;
    }
    wchar_t temporary[32768]{};
    DWORD length = GetTempPathW(32768, temporary);
    if (!length || length >= 32768) {
        error_ = system_error(GetLastError());
        return false;
    }
    auto id = random_id();
    if (id.empty())
        return false;
    script_log_ = std::filesystem::path(temporary) / (L"Lansyra-" + wide(id) + L".log");
    if (!write_atomic(script_log_, "", error_))
        return false;
    DWORD needed = GetEnvironmentVariableW(L"VPN_SCRIPT_LOG", nullptr, 0);
    if (needed) {
        std::vector<wchar_t> previous(needed);
        GetEnvironmentVariableW(L"VPN_SCRIPT_LOG", previous.data(), needed);
        previous_script_log_ = previous.data();
    }
    script_environment_set_ = SetEnvironmentVariableW(L"VPN_SCRIPT_LOG", script_log_.c_str()) != FALSE;
    if (!script_environment_set_)
        error_ = system_error(GetLastError());
    return script_environment_set_;
}
void Session::read_script_log() {
    if (script_log_.empty())
        return;
    std::string raw;
    std::wstring message;
    if (!read_file(script_log_, raw, message, 3 * 1024 * 1024) || raw.size() <= script_log_read_)
        return;
    size_t offset = script_log_read_;
    script_log_read_ = raw.size() - raw.size() % 2;
    if (!offset && raw.rfind("\xff\xfe", 0) == 0)
        offset = 2;
    if (offset >= script_log_read_)
        return;
    std::wstring text((script_log_read_ - offset) / 2, L'\0');
    memcpy(text.data(), raw.data() + offset, script_log_read_ - offset);
    // Each callback runs after the script has exited. Drain instead of
    // rereading an ever-growing session file on every reconnect.
    Handle file(CreateFileW(script_log_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, TRUNCATE_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file)
        script_log_read_ = 0;
    log(PRG_INFO, utf8(text));
}
void Session::cleanup() noexcept {
    tunnel_loop_.store(false);
    pause_pending_.store(false);
    if (vpn_ && tun_ready_ && !mainloop_finished_) {
        // PAUSE retains the adapter. Re-enter with CANCEL so the core runs its
        // disconnect script and closes Wintun before vpninfo_free releases data.
        cleaning_ = true;
        send(OC_CMD_CANCEL);
        openconnect_mainloop(vpn_, 0, RECONNECT_INTERVAL_MIN);
        mainloop_finished_ = true;
        cleaning_ = false;
    }
    statistics_pending_.store(false);
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_ = INVALID_SOCKET;
    }
    if (vpn_) {
        cleaning_ = true;
        openconnect_clear_cookie(vpn_);
        openconnect_vpninfo_free(vpn_);
        vpn_ = nullptr;
        cleaning_ = false;
    }
    try {
        read_script_log();
    } catch (...) {
    }
    if (script_environment_set_) {
        SetEnvironmentVariableW(L"VPN_SCRIPT_LOG",
                                previous_script_log_.empty() ? nullptr : previous_script_log_.c_str());
        script_environment_set_ = false;
    }
    ipv6_guard_.reset();
    if (!script_log_.empty()) {
        try {
            const auto routes = script_log_.wstring() + L".routes";
            if (GetFileAttributesW(routes.c_str()) != INVALID_FILE_ATTRIBUTES) {
                cleanup_failed_ = true;
                log(PRG_ERR,
                    utf8(L"VPN 路由尚未清理，恢复记录已保留： / VPN route recovery retained: " + routes));
            } else
                DeleteFileW(script_log_.c_str());
        } catch (...) {
        }
        script_log_.clear();
    }
    if (script_environment_lock_.owns_lock())
        script_environment_lock_.unlock();
    previous_script_log_.clear();
    script_log_read_ = 0;
    tun_ready_ = false;
    erase(pending_password_);
    erase(pending_username_);
    erase(pending_group_);
}
void Session::run() {
    if (stopped()) {
        fail(ErrorCategory::Canceled);
        state(State::Idle, true);
        return;
    }
    // The UI keeps opaque blobs only. Decrypt the selected connection on its
    // worker instead of decrypting every profile on each menu/list refresh.
    ProfileStore::unlock(profile_);
    redactor_.remember(profile_.password);
    redactor_.remember(profile_.token);
    if (profile_.secret_unavailable)
        log(PRG_INFO,
            utf8(tr(language_, L"保存的凭据不属于当前 Windows 用户，请重新输入。",
                    L"Saved credentials are unavailable for this Windows user. Enter them again.")));
    Handle session_lock(
        CreateMutexW(nullptr, FALSE, (L"Local\\Lansyra.Session." + wide(profile_.id)).c_str()));
    DWORD acquired = session_lock ? WaitForSingleObject(session_lock.get(), 0) : WAIT_FAILED;
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
        fail(acquired == WAIT_TIMEOUT ? ErrorCategory::Busy : ErrorCategory::Internal,
             acquired == WAIT_TIMEOUT ? 0 : static_cast<int>(GetLastError()));
        state(State::Failed, true,
              tr(language_, L"这个配置已在另一个客户端实例中连接。",
                 L"This profile is already connected in another client instance."));
        return;
    }
    struct Unlock {
        Session *session;
        HANDLE handle;
        ~Unlock() {
            // The lease outlives every core, script, route and credential cleanup,
            // including exception unwinding. Only this worker releases it.
            session->cleanup();
            ReleaseMutex(handle);
        }
    } unlock{this, session_lock.get()};
    bool ok = false;
    for (attempt_ = 1; !stopped();) {
        error_.clear();
        error_category_ = ErrorCategory::None;
        error_code_ = 0;
        http_status_ = 0;
        forms_ = retry_seconds_ = 0;
        group_selected_ = used_saved_username_ = used_saved_password_ = last_empty_ = false;
        authentication_rejected_ = tun_failed_ = cleanup_failed_ = mainloop_finished_ = false;
        ok = wait_until_resumed() && run_attempt();
        if (tun_ready_)
            state(State::Disconnecting);
        cleanup();
        if (cleanup_failed_) {
            ok = fail(ErrorCategory::Adapter);
            error_ =
                tr(language_, L"VPN 网络清理失败，请查看日志。", L"VPN network cleanup failed; see the log.");
            break;
        }
        if (ok || stopped())
            break;
        if (error_category_ == ErrorCategory::None)
            fail(ErrorCategory::Internal);
        unsigned delay = options_.retry_failed ? retry_delay(error_category_, attempt_) : 0;
        if (!delay)
            break;
        for (retry_seconds_ = delay; retry_seconds_ && !stopped(); --retry_seconds_) {
            state(State::RetryWait, false,
                  retry_seconds_ == delay ? error_text(error_category_, language_) : L"");
            if (WaitForSingleObject(cancel_event_.get(), 1000) != WAIT_TIMEOUT)
                break;
        }
        if (stopped())
            break;
        ++attempt_;
    }
    retry_seconds_ = 0;
    if (cleanup_failed_)
        state(State::Failed, true, error_);
    else if (stopped()) {
        fail(ErrorCategory::Canceled);
        state(State::Idle, true);
    } else if (ok) {
        fail(ErrorCategory::None);
        state(State::Idle, true);
    } else
        state(State::Failed, true, error_.empty() ? error_text(error_category_, language_) : error_);
}
bool Session::run_attempt() {
    std::wstring normalized;
    if (!validate_profile(profile_, error_) || !normalize_gateway(profile_.gateway, normalized, &origin_))
        return fail(ErrorCategory::Configuration);
    if (!options_.authentication_only) {
        if (!administrator()) {
            error_ = tr(language_, L"建立 VPN 网卡需要管理员权限。",
                        L"Administrator privileges are required to create the VPN adapter.");
            return fail(ErrorCategory::Permission);
        }
        if (GetFileAttributesW((executable_directory() / L"wintun.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
            error_ =
                tr(language_, L"便携包缺少 wintun.dll。", L"The portable package is missing wintun.dll.");
            return fail(ErrorCategory::Configuration);
        }
        auto script = profile_.script.empty() ? executable_directory() / L"vpnc-script-win.js"
                                              : std::filesystem::path(profile_.script);
        if (!std::filesystem::is_regular_file(script)) {
            error_ = tr(language_, L"找不到 VPN 网络配置脚本。",
                        L"The VPN network configuration script could not be found.");
            return fail(ErrorCategory::Configuration);
        }
        if (!prepare_script_log())
            return fail(ErrorCategory::Adapter);
    }
    state(State::Connecting);
    const std::string user_agent = std::string("Lansyra/") + Version;
    vpn_ = openconnect_vpninfo_new(user_agent.c_str(), certificate_callback, nullptr, authentication_callback,
                                   progress_callback, this);
    if (!vpn_) {
        error_ = tr(language_, L"无法创建 VPN 会话。", L"Could not create the VPN session.");
        return fail(ErrorCategory::Internal);
    }
    // Match the user's selected verbosity at the protocol core as well as in
    // the callback filter. This avoids formatting and dispatching trace output
    // that the UI will discard anyway.
    openconnect_set_loglevel(vpn_, log_level_.load());
    openconnect_set_reported_os(vpn_, "win");
    openconnect_set_system_trust(vpn_, 0);
    if (openconnect_set_protocol(vpn_, profile_.protocol.c_str()) ||
        openconnect_parse_url(vpn_, utf8(profile_.gateway).c_str())) {
        error_ = tr(language_, L"网关或 VPN 协议无效。", L"Invalid gateway or VPN protocol.");
        return fail(ErrorCategory::Configuration);
    }
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_ = openconnect_setup_cmd_pipe(vpn_);
        if (command_ == INVALID_SOCKET)
            return fail(ErrorCategory::Internal, WSAGetLastError());
        u_long nonblocking = 1;
        if (ioctlsocket(command_, FIONBIO, &nonblocking))
            return fail(ErrorCategory::Internal, WSAGetLastError());
    }
    if (!wait_until_resumed())
        return false;
    openconnect_set_stats_handler(vpn_, statistics_callback);
    openconnect_set_reconnected_handler(vpn_, reconnected_callback);
    if (!options_.authentication_only)
        openconnect_set_setup_tun_handler(vpn_, setup_tun_callback);
    if (profile_.disable_udp)
        openconnect_disable_dtls(vpn_);
    if (!profile_.certificate_file.empty()) {
        auto certificate = utf8(profile_.certificate_file), key = utf8(profile_.key_file);
        if (openconnect_set_client_cert(vpn_, certificate.c_str(), key.empty() ? nullptr : key.c_str()) < 0) {
            error_ =
                tr(language_, L"无法使用所选用户证书。", L"Could not use the selected client certificate.");
            return fail(ErrorCategory::Configuration);
        }
    }
    if (profile_.token_type >= 0 && !profile_.token.empty()) {
        if (profile_.token_type == OC_TOKEN_MODE_HOTP)
            openconnect_set_token_callbacks(vpn_, this, lock_token_callback, nullptr);
        if (openconnect_set_token_mode(vpn_, static_cast<oc_token_mode_t>(profile_.token_type),
                                       profile_.token.c_str()) < 0) {
            error_ =
                tr(language_, L"OTP 令牌格式无效或不受支持。", L"The OTP token is invalid or unsupported.");
            return fail(ErrorCategory::Configuration);
        }
    }
    if (profile_.use_proxy) {
        auto proxy = system_proxy(profile_.gateway);
        if (!proxy.empty() && openconnect_set_http_proxy(vpn_, proxy.c_str()) < 0) {
            error_ = tr(language_, L"无法使用系统代理。", L"Could not use the system proxy.");
            return fail(ErrorCategory::Configuration);
        }
    }
    int result = openconnect_obtain_cookie(vpn_);
    if (result != 0 || stopped()) {
        if (error_category_ == ErrorCategory::None) {
            auto category = connection_error(result, ErrorCategory::Network);
            if (authentication_rejected_ || http_status_ == 401 || http_status_ == 403 || http_status_ == 407)
                category = ErrorCategory::Authentication;
            else if (http_status_ >= 400 && http_status_ < 500 && http_status_ != 408 && http_status_ != 429)
                category = ErrorCategory::Configuration;
            fail(category, result);
        }
        return false;
    }
    if (const char *cookie = openconnect_get_cookie(vpn_))
        redactor_.remember(cookie);
    if (current_origin() == origin_) {
        persist([&](Profile &p) {
            if (!pending_username_.empty())
                p.username = pending_username_;
            if (!pending_group_.empty())
                p.group = pending_group_;
            if (p.batch_mode && !pending_password_.empty()) {
                p.password = pending_password_;
                p.password_blob.clear();
            }
        });
        if (!pending_username_.empty())
            profile_.username = pending_username_;
        if (!pending_group_.empty())
            profile_.group = pending_group_;
        if (profile_.batch_mode && !pending_password_.empty()) {
            erase(profile_.password);
            profile_.password = pending_password_;
        }
    }
    if (options_.authentication_only)
        return true;
    if (!wait_until_resumed())
        return false;
    result = openconnect_make_cstp_connection(vpn_);
    if (result < 0 || tun_failed_ || stopped()) {
        if (error_category_ == ErrorCategory::None)
            fail(connection_error(result, ErrorCategory::Network), result);
        return false;
    }
    if (!profile_.disable_udp && openconnect_setup_dtls(vpn_, profile_.dtls_period) < 0)
        log(PRG_INFO, utf8(tr(language_, L"UDP 暂不可用，继续使用 TLS 连接。",
                              L"UDP is unavailable; continuing with the TLS connection.")));
    if (!tun_ready_)
        state(State::Configuring);
    while (!stopped()) {
        if (!wait_until_resumed())
            break;
        tunnel_loop_.store(true);
        // Close the check/enter race with a suspend request from the UI.
        if (suspended_.load())
            network_changed();
        result = openconnect_mainloop(vpn_, profile_.reconnect_timeout, RECONNECT_INTERVAL_MIN);
        mainloop_finished_ = result < 0;
        tunnel_loop_.store(false);
        pause_pending_.store(false);
        if (tun_failed_)
            return fail(ErrorCategory::Adapter, result);
        if (result < 0) {
            if (stopped())
                return true;
            return fail(
                connection_error(result, result == -EPIPE ? ErrorCategory::Server : ErrorCategory::Network),
                result);
        }
        state(State::Reconnecting);
    }
    return true;
}
} // namespace vpn
