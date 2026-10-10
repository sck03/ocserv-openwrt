// Native adaptation of the callback lifecycle in OpenConnect GUI v1.6.2.
// Upstream: Copyright (C) 2014 Red Hat, GPL-2.0-or-later.
#include "session.h"
#include <winhttp.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace vpn {
namespace {
std::mutex script_environment_mutex;
std::atomic<uint64_t> next_session{0};
bool is_field(const char *value, const char *expected) {
    return value && _stricmp(value, expected) == 0;
}
std::wstring safe(const char *value) {
    return value ? wide(value) : std::wstring();
}
std::string system_proxy(const std::wstring &url) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config))
        return {};
    std::wstring selected = config.lpszProxy ? config.lpszProxy : L"";
    if (config.fAutoDetect || config.lpszAutoConfigUrl) {
        HINTERNET session = WinHttpOpen(L"LinkoraVPN", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
        if (session) {
            WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
            WINHTTP_AUTOPROXY_OPTIONS options{};
            options.dwFlags =
                config.lpszAutoConfigUrl ? WINHTTP_AUTOPROXY_CONFIG_URL : WINHTTP_AUTOPROXY_AUTO_DETECT;
            options.lpszAutoConfigUrl = config.lpszAutoConfigUrl;
            options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            options.fAutoLogonIfChallenged = FALSE;
            WINHTTP_PROXY_INFO proxy{};
            if (WinHttpGetProxyForUrl(session, url.c_str(), &options, &proxy)) {
                selected = proxy.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && proxy.lpszProxy
                               ? proxy.lpszProxy
                               : L"";
                if (proxy.lpszProxy)
                    GlobalFree(proxy.lpszProxy);
                if (proxy.lpszProxyBypass)
                    GlobalFree(proxy.lpszProxyBypass);
            }
            WinHttpCloseHandle(session);
        }
    }
    if (config.lpszAutoConfigUrl)
        GlobalFree(config.lpszAutoConfigUrl);
    if (config.lpszProxy)
        GlobalFree(config.lpszProxy);
    if (config.lpszProxyBypass)
        GlobalFree(config.lpszProxyBypass);
    std::wstring fallback;
    for (size_t begin = 0; begin < selected.size();) {
        size_t end = selected.find(L';', begin);
        auto item = trim(selected.substr(begin, end == std::wstring::npos ? end : end - begin));
        if (item.rfind(L"https=", 0) == 0) {
            fallback = item.substr(6);
            break;
        }
        if (item.rfind(L"http=", 0) == 0)
            fallback = item.substr(5);
        else if (item.find(L'=') == std::wstring::npos && fallback.empty())
            fallback = item;
        if (end == std::wstring::npos)
            break;
        begin = end + 1;
    }
    if (fallback.empty())
        return {};
    if (fallback.find(L"://") == std::wstring::npos)
        fallback = L"http://" + fallback;
    return utf8(fallback);
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
void Session::log(int level, std::wstring message) {
    if (level > log_level_.load())
        return;
    // Redact the complete message before splitting, including secrets that
    // cross a chunk boundary. Queue entries have a fixed maximum size.
    auto text = wide(redactor_.clean(utf8(message)));
    for (size_t begin = 0; begin < text.size(); begin += 8192) {
        Event event;
        event.kind = Event::Kind::Log;
        event.text = text.substr(begin, 8192);
        emit(std::move(event));
    }
}
bool Session::ask(const std::shared_ptr<Prompt> &prompt) {
    if (!wait_until_resumed() || !prompt->answered)
        return false;
    Event event;
    event.kind = Event::Kind::Prompt;
    event.prompt = prompt;
    emit(std::move(event));
    HANDLE handles[] = {cancel_event_.get(), prompt->answered.get()};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (result != WAIT_OBJECT_0 + 1 || !prompt->done.load(std::memory_order_acquire) || !prompt->accepted) {
        prompt->answer(false);
        cancel();
        return false;
    }
    return wait_until_resumed();
}
std::wstring Session::current_origin() const {
    if (!vpn_)
        return {};
    std::wstring host = safe(openconnect_get_dnsname(vpn_));
    if (host.find(L':') != std::wstring::npos && host.front() != L'[')
        host = L"[" + host + L"]";
    std::wstring normalized, origin;
    normalize_gateway(L"https://" + host + L":" + std::to_wstring(openconnect_get_port(vpn_)), normalized,
                      &origin);
    return origin;
}
void Session::persist(const std::function<void(Profile &)> &change) {
    if (stopped() || !store_ || profile_.id.empty())
        return;
    std::wstring error;
    if (!store_->update_secrets(profile_.id, profile_.gateway, change, error))
        log(PRG_ERR, error);
    else {
        Event event;
        event.kind = Event::Kind::ProfilesChanged;
        emit(std::move(event));
    }
}
int Session::authentication_callback(void *context, oc_auth_form *form) {
    auto *self = static_cast<Session *>(context);
    try {
        return self->authenticate(form);
    } catch (...) {
        self->error_ = tr(self->language_, L"无法处理服务器认证表单。",
                          L"Could not process the server authentication form.");
        self->fail(ErrorCategory::Authentication);
        return OC_FORM_RESULT_ERR;
    }
}
int Session::authenticate(oc_auth_form *form) {
    if (!wait_until_resumed())
        return OC_FORM_RESULT_CANCELLED;
    if (!form || ++forms_ > 64) {
        fail(ErrorCategory::Authentication);
        return OC_FORM_RESULT_ERR;
    }
    unsigned count = 0;
    for (auto *option = form->opts; option; option = option->next) {
        if (++count > 256) {
            fail(ErrorCategory::Authentication);
            return OC_FORM_RESULT_ERR;
        }
        if (option->type == OC_FORM_OPT_HIDDEN && option->_value)
            redactor_.remember(option->_value);
    }
    state(State::Authenticating);
    if (form->banner)
        log(PRG_INFO, safe(form->banner));
    if (form->message)
        log(PRG_INFO, safe(form->message));
    if (form->error)
        log(PRG_ERR, safe(form->error));
    const bool same_server = current_origin() == origin_;
    const bool failure = form->error && *form->error;
    if (failure) {
        authentication_rejected_ = true;
        used_saved_password_ = true;
        used_saved_username_ = true;
        erase(pending_password_);
    }
    auto prompt_for = [&](Prompt::Kind kind, const char *name, const char *label) {
        auto prompt = std::make_shared<Prompt>();
        prompt->kind = kind;
        prompt->field = name ? name : "";
        prompt->label = safe(label);
        prompt->server = current_origin();
        prompt->banner = safe(form->banner);
        prompt->message = safe(form->message);
        prompt->error = safe(form->error);
        if (kind == Prompt::Kind::Password)
            prompt->title = tr(language_, L"输入密码或验证码", L"Password input");
        else if (kind == Prompt::Kind::Selection)
            prompt->title = tr(language_, L"选择认证选项", L"Form selection");
        else
            prompt->title = tr(language_, L"输入认证信息", L"Username input");
        return prompt;
    };
    if (form->authgroup_opt && !(form->authgroup_opt->form.flags & OC_FORM_OPT_IGNORE)) {
        auto *group = form->authgroup_opt;
        if (group->nr_choices < 1 || group->nr_choices > 256)
            return OC_FORM_RESULT_CANCELLED;
        std::string selected;
        if (group->nr_choices == 1)
            selected = group->choices[0]->name;
        else if (same_server && !failure) {
            const auto &cached = group_selected_ ? pending_group_ : profile_.group;
            for (int i = 0; i < group->nr_choices; ++i)
                if (cached == group->choices[i]->name)
                    selected = cached;
        }
        if (selected.empty()) {
            auto prompt = prompt_for(Prompt::Kind::Selection, group->form.name, group->form.label);
            prompt->title = tr(language_, L"选择认证分组", L"Auth group selection");
            for (int i = 0; i < group->nr_choices; ++i)
                prompt->choices.push_back({safe(group->choices[i]->label), group->choices[i]->name});
            if (!ask(prompt))
                return OC_FORM_RESULT_CANCELLED;
            selected = prompt->response;
        }
        redactor_.remember(selected);
        if (openconnect_set_option_value(&group->form, selected.c_str()) < 0)
            return OC_FORM_RESULT_CANCELLED;
        bool changed = !group_selected_ || selected != pending_group_;
        pending_group_ = selected;
        if (changed) {
            group_selected_ = true;
            return OC_FORM_RESULT_NEWGROUP;
        }
    }
    bool empty = true;
    unsigned fields = 0;
    for (oc_form_opt *option = form->opts; option; option = option->next) {
        if (++fields > 256)
            return OC_FORM_RESULT_CANCELLED;
        if (option->flags & OC_FORM_OPT_IGNORE)
            continue;
        if (form->authgroup_opt && option == &form->authgroup_opt->form)
            continue;
        if (option->type == OC_FORM_OPT_HIDDEN || option->type == OC_FORM_OPT_TOKEN)
            continue;
        const bool username = is_field(option->name, "username");
        std::wstring label = safe(option->label);
        std::transform(label.begin(), label.end(), label.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        bool one_time = false;
        for (const wchar_t *marker : {L"otp", L"token", L"passcode", L"verification", L"验证码", L"动态"})
            if (label.find(marker) != std::wstring::npos)
                one_time = true;
        const bool password =
            !one_time && (is_field(option->name, "password") || is_field(option->name, "credential"));
        std::string value;
        if (option->type == OC_FORM_OPT_TEXT && username && same_server && !used_saved_username_ &&
            !profile_.username.empty()) {
            value = profile_.username;
            used_saved_username_ = true;
        } else if (option->type == OC_FORM_OPT_PASSWORD && password && same_server && !used_saved_password_ &&
                   profile_.batch_mode && !profile_.password.empty()) {
            value = profile_.password;
            used_saved_password_ = true;
        } else {
            Prompt::Kind kind = option->type == OC_FORM_OPT_PASSWORD ? Prompt::Kind::Password
                                : option->type == OC_FORM_OPT_SELECT ? Prompt::Kind::Selection
                                                                     : Prompt::Kind::Text;
            if (option->type != OC_FORM_OPT_TEXT && option->type != OC_FORM_OPT_PASSWORD &&
                option->type != OC_FORM_OPT_SELECT)
                return OC_FORM_RESULT_CANCELLED;
            auto prompt = prompt_for(kind, option->name, option->label);
            if (option->type == OC_FORM_OPT_SELECT) {
                auto *selection = reinterpret_cast<oc_form_opt_select *>(option);
                if (selection->nr_choices < 1 || selection->nr_choices > 256)
                    return OC_FORM_RESULT_CANCELLED;
                for (int i = 0; i < selection->nr_choices; ++i)
                    prompt->choices.push_back(
                        {safe(selection->choices[i]->label), selection->choices[i]->name});
            } else if (username && same_server)
                prompt->initial = pending_username_.empty() ? profile_.username : pending_username_;
            if (!ask(prompt))
                return OC_FORM_RESULT_CANCELLED;
            value = prompt->response;
        }
        redactor_.remember(value);
        if (value.size() > 8192 || value.find('\0') != std::string::npos ||
            openconnect_set_option_value(option, value.c_str()) < 0) {
            erase(value);
            fail(ErrorCategory::Authentication);
            return OC_FORM_RESULT_CANCELLED;
        }
        if (same_server && username) {
            pending_username_ = value;
            used_saved_username_ = true;
        }
        if (same_server && password) {
            if (pending_password_.empty())
                pending_password_ = value;
            used_saved_password_ = true;
        }
        erase(value);
        empty = false;
    }
    if (last_empty_ && empty) {
        error_ = tr(language_, L"服务器重复返回空认证表单。",
                    L"The server repeatedly returned an empty authentication form.");
        return OC_FORM_RESULT_ERR;
    }
    if (empty && (form->banner || form->message) && !is_field(form->auth_id, "success")) {
        auto prompt = prompt_for(Prompt::Kind::Notice, nullptr, nullptr);
        prompt->title = tr(language_, L"服务器通知", L"Server notice");
        if (!ask(prompt))
            return OC_FORM_RESULT_CANCELLED;
    }
    last_empty_ = empty;
    return OC_FORM_RESULT_OK;
}
int Session::certificate_callback(void *context, const char *reason) {
    auto *self = static_cast<Session *>(context);
    try {
        return self->validate_certificate(reason);
    } catch (...) {
        self->error_ =
            tr(self->language_, L"服务器证书验证失败。", L"Server certificate verification failed.");
        self->fail(ErrorCategory::Certificate);
        return -1;
    }
}
int Session::validate_certificate(const char *reason) {
    if (stopped())
        return -1;
    // Retain the specific category if a TLS callback rejects the peer.
    auto reject = [&] {
        fail(ErrorCategory::Certificate);
        return -1;
    };
    DWORD date_error = 0;
    if (!peer_certificate_in_date(vpn_, date_error)) {
        error_ = tr(language_, L"服务器证书已过期、尚未生效或无效：",
                    L"The server certificate is expired, not yet valid or invalid: ") +
                 system_error(date_error);
        return reject();
    }
    const char *fingerprint = openconnect_get_peer_cert_hash(vpn_);
    if (!fingerprint || !valid_pin(fingerprint)) {
        error_ = tr(language_, L"无法读取服务器指纹。", L"Could not read the server fingerprint.");
        return reject();
    }
    const bool same = current_origin() == origin_;
    if (same && !profile_.server_pin.empty()) {
        if (openconnect_check_peer_cert_hash(vpn_, profile_.server_pin.c_str()) == 0)
            return 0;
        if (profile_.fixed_pin) {
            error_ = tr(language_, L"服务器证书与指定指纹不匹配。",
                        L"The server certificate does not match the specified fingerprint.");
            return reject();
        }
    }
    DWORD code = 0;
    bool trusted = verify_peer_with_windows(vpn_, safe(openconnect_get_dnsname(vpn_)),
                                            same ? profile_.ca_file : L"", code);
    bool changed = same && !profile_.server_pin.empty();
    if (trusted && !changed)
        return 0;
    if (!trusted && same && !profile_.ca_file.empty()) {
        error_ = tr(language_, L"服务器证书未通过指定 CA 验证：",
                    L"The server certificate failed the configured CA check: ") +
                 system_error(code);
        return reject();
    }
    auto prompt = std::make_shared<Prompt>();
    prompt->kind = Prompt::Kind::Certificate;
    prompt->title = tr(language_, L"确认服务器证书", L"Server certificate verification");
    prompt->server = current_origin();
    prompt->pin = fingerprint;
    prompt->previous_pin = same ? profile_.server_pin : "";
    prompt->message = changed ? tr(language_, L"服务器公钥已更改，请重新核对。",
                                   L"The server public key has changed. Verify it again.")
                              : system_error(code);
    if (prompt->message.empty())
        prompt->message = safe(reason);
    char *details = openconnect_get_peer_cert_details(vpn_);
    if (details) {
        prompt->details = wide(details);
        openconnect_free_cert_info(vpn_, details);
    }
    if (!ask(prompt))
        return -1;
    if (same) {
        profile_.server_pin = prompt->pin;
        profile_.fixed_pin = false;
        persist([&](Profile &p) {
            p.server_pin = prompt->pin;
            p.fixed_pin = false;
        });
    }
    return 0;
}
void Session::progress_callback(void *context, int level, const char *format, ...) {
    auto *self = static_cast<Session *>(context);
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
                             ? L"[oversized log entry omitted]"
                             : wide(buffer));
    } catch (...) {
    }
    SecureZeroMemory(buffer, sizeof(buffer));
}
void Session::statistics_callback(void *context, const oc_stats *stats) {
    auto *self = static_cast<Session *>(context);
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
        auto script = self->profile_.script.empty() ? executable_directory() / L"vpnc-script-win.js"
                                                    : std::filesystem::path(self->profile_.script);
        std::string interface_name = self->profile_.interface_name.empty()
                                         ? "LinkoraVPN-" + self->profile_.id.substr(0, 12)
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
        if (!self->stopped() && self->tun_ready_ && !self->tun_failed_)
            self->state(State::Connected);
    } catch (...) {
    }
}
int Session::lock_token_callback(void *context) {
    auto *self = static_cast<Session *>(context);
    if (self->current_origin() != self->origin_)
        return -EPERM;
    return openconnect_set_token_mode(self->vpn_, static_cast<oc_token_mode_t>(self->profile_.token_type),
                                      self->profile_.token.c_str());
}
int Session::unlock_token_callback(void *context, const char *token) {
    auto *self = static_cast<Session *>(context);
    try {
        if (token) {
            self->redactor_.remember(token);
            erase(self->profile_.token);
            self->profile_.token = token;
            self->persist([&](Profile &p) {
                p.token = token;
                p.token_blob.clear();
            });
        }
        return 0;
    } catch (...) {
        return -1;
    }
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
    script_log_ = std::filesystem::path(temporary) / (L"LinkoraVPN-" + wide(id) + L".log");
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
    log(PRG_INFO, text);
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
    if (!script_log_.empty()) {
        try {
            DeleteFileW((script_log_.wstring() + L".routes").c_str());
        } catch (...) {
        }
        DeleteFileW(script_log_.c_str());
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
    Handle session_lock(
        CreateMutexW(nullptr, FALSE, (L"Local\\LinkoraVPN.Session." + wide(profile_.id)).c_str()));
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
    const std::string user_agent = std::string("LinkoraVPN/") + Version;
    vpn_ = openconnect_vpninfo_new(user_agent.c_str(), certificate_callback, nullptr, authentication_callback,
                                   progress_callback, this);
    if (!vpn_) {
        error_ = tr(language_, L"无法创建 VPN 会话。", L"Could not create the VPN session.");
        return fail(ErrorCategory::Internal);
    }
    openconnect_set_loglevel(vpn_, PRG_TRACE);
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
        openconnect_set_token_callbacks(vpn_, this, lock_token_callback, unlock_token_callback);
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
        log(PRG_INFO, tr(language_, L"UDP 暂不可用，继续使用 TLS 连接。",
                         L"UDP is unavailable; continuing with the TLS connection."));
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
