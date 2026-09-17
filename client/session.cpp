// Native adaptation of the callback lifecycle in OpenConnect GUI v1.6.2.
// Upstream: Copyright (C) 2014 Red Hat, GPL-2.0-or-later.
#include "session.h"
#include <winhttp.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace bulijie {
namespace {
std::mutex script_environment_mutex;
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
        HINTERNET session = WinHttpOpen(L"BulijieVPN", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
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
Session::Session(Profile profile, ProfileStore *store, Language language, Sink sink, bool authentication_only)
    : profile_(std::move(profile)), store_(store), language_(language), sink_(std::move(sink)),
      authentication_only_(authentication_only) {
    if (profile_.log_level >= 0)
        log_level_ = profile_.log_level;
    if (!profile_.password.empty())
        secrets_.push_back(profile_.password);
}
Session::~Session() {
    cancel();
    if (worker_.joinable())
        worker_.join();
    erase(profile_.password);
    erase(profile_.token);
    erase(pending_password_);
    for (auto &value : secrets_)
        erase(value);
}
bool Session::start() {
    if (worker_.joinable() || !cancel_event_)
        return false;
    try {
        worker_ = std::thread([this] {
            try {
                run();
            } catch (...) {
                error_ = tr(language_, L"连接处理失败。", L"Connection processing failed.");
                cleanup();
                state(State::Failed, true, error_);
            }
            finished_.store(true);
        });
        return true;
    } catch (...) {
        return false;
    }
}
bool Session::stopped() const {
    return WaitForSingleObject(cancel_event_.get(), 0) == WAIT_OBJECT_0;
}
bool Session::send(char command) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    return command_ != INVALID_SOCKET && ::send(command_, &command, 1, 0) == 1;
}
void Session::cancel() {
    SetEvent(cancel_event_.get());
    send(OC_CMD_CANCEL);
}
void Session::request_statistics() {
    if (!stopped())
        send(OC_CMD_STATS);
}
void Session::state(State value, bool terminal, std::wstring message) {
    state_ = value;
    Event event;
    event.state = value;
    event.terminal = terminal;
    event.text = std::move(message);
    sink_(std::move(event));
}
void Session::log(int level, std::wstring message) {
    if (level > log_level_.load())
        return;
    Event event;
    event.kind = Event::Kind::Log;
    event.level = level;
    event.text = wide(redact(utf8(message)));
    sink_(std::move(event));
}
std::string Session::redact(std::string message) const {
    std::string lower = message;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char *key : {"authorization:", "cookie:", "set-cookie:", "<password", "<credential", "<token",
                            "<session-token", "<authcookie", "<passcode", "\"password\"", "\"cookie\"",
                            "\"token\"", "password=", "passwd=", "samlresponse="})
        if (lower.find(key) != std::string::npos)
            return "[authentication data redacted]";
    auto remove = [&](const std::string &value) {
        if (value.empty())
            return;
        size_t offset = 0;
        while ((offset = message.find(value, offset)) != std::string::npos) {
            message.replace(offset, value.size(), "[redacted]");
            offset += 10;
        }
    };
    remove(profile_.password);
    remove(profile_.token);
    remove(pending_password_);
    for (const auto &value : secrets_)
        remove(value);
    return message;
}
bool Session::ask(const std::shared_ptr<Prompt> &prompt) {
    if (stopped() || !prompt->answered)
        return false;
    Event event;
    event.kind = Event::Kind::Prompt;
    event.prompt = prompt;
    sink_(std::move(event));
    HANDLE handles[] = {cancel_event_.get(), prompt->answered.get()};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (result != WAIT_OBJECT_0 + 1 || !prompt->done.load(std::memory_order_acquire) || !prompt->accepted) {
        cancel();
        return false;
    }
    return true;
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
    if (!store_ || profile_.id.empty())
        return;
    std::wstring error;
    if (!store_->update_secrets(profile_.id, profile_.gateway, change, error))
        log(PRG_ERR, error);
    else {
        Event event;
        event.kind = Event::Kind::ProfilesChanged;
        sink_(std::move(event));
    }
}
int Session::authentication_callback(void *context, oc_auth_form *form) {
    auto *self = static_cast<Session *>(context);
    try {
        return self->authenticate(form);
    } catch (...) {
        self->error_ = tr(self->language_, L"无法处理服务器认证表单。",
                          L"Could not process the server authentication form.");
        return OC_FORM_RESULT_ERR;
    }
}
int Session::authenticate(oc_auth_form *form) {
    if (stopped() || !form || ++forms_ > 64)
        return OC_FORM_RESULT_CANCELLED;
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
            if (option->type == OC_FORM_OPT_PASSWORD && secrets_.size() < 128)
                secrets_.push_back(value);
        }
        if (value.size() > 8192 || value.find('\0') != std::string::npos ||
            openconnect_set_option_value(option, value.c_str()) < 0)
            return OC_FORM_RESULT_CANCELLED;
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
        return -1;
    }
}
int Session::validate_certificate(const char *reason) {
    if (stopped())
        return -1;
    DWORD date_error = 0;
    if (!peer_certificate_in_date(vpn_, date_error)) {
        error_ = tr(language_, L"服务器证书已过期、尚未生效或无效：",
                    L"The server certificate is expired, not yet valid or invalid: ") +
                 system_error(date_error);
        return -1;
    }
    const char *fingerprint = openconnect_get_peer_cert_hash(vpn_);
    if (!fingerprint || !valid_pin(fingerprint)) {
        error_ = tr(language_, L"无法读取服务器指纹。", L"Could not read the server fingerprint.");
        return -1;
    }
    const bool same = current_origin() == origin_;
    if (same && !profile_.server_pin.empty()) {
        if (openconnect_check_peer_cert_hash(vpn_, profile_.server_pin.c_str()) == 0)
            return 0;
        if (profile_.fixed_pin) {
            error_ = tr(language_, L"服务器证书与指定指纹不匹配。",
                        L"The server certificate does not match the specified fingerprint.");
            return -1;
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
        return -1;
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
    if (self->state_ == State::Connected &&
        (strstr(format, "remaining timeout") || strstr(format, "SSL connection failure"))) {
        try { self->state(State::Reconnecting); } catch (...) {}
    }
    if (level > self->log_level_.load())
        return;
    char buffer[8192]{};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    try {
        self->log(level, wide(buffer));
    } catch (...) {
    }
}
void Session::statistics_callback(void *context, const oc_stats *stats) {
    auto *self = static_cast<Session *>(context);
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
        self->sink_(std::move(event));
    } catch (...) {
    }
}
void Session::setup_tun_callback(void *context) {
    auto *self = static_cast<Session *>(context);
    try {
        self->state(State::Configuring);
        auto script = self->profile_.script.empty() ? executable_directory() / L"vpnc-script-win.js"
                                                    : std::filesystem::path(self->profile_.script);
        std::string interface_name = self->profile_.interface_name.empty()
                                         ? "BulijieVPN-" + self->profile_.id.substr(0, 12)
                                         : utf8(self->profile_.interface_name);
        int result = openconnect_setup_tun_device(self->vpn_, utf8(script.wstring()).c_str(),
                                                interface_name.c_str());
        self->read_script_log();
        if (result != 0) {
            self->tun_failed_ = true;
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
        error_ = tr(language_, L"当前实例已有一个 VPN 连接。", L"This instance already has a VPN connection.");
        return false;
    }
    wchar_t temporary[32768]{};
    DWORD length = GetTempPathW(32768, temporary);
    if (!length || length >= 32768) { error_ = system_error(GetLastError()); return false; }
    auto id = random_id();
    if (id.empty()) return false;
    script_log_ = std::filesystem::path(temporary) / (L"BulijieVPN-" + wide(id) + L".log");
    if (!write_atomic(script_log_, "", error_)) return false;
    DWORD needed = GetEnvironmentVariableW(L"BULIJIE_SCRIPT_LOG", nullptr, 0);
    if (needed) {
        std::vector<wchar_t> previous(needed);
        GetEnvironmentVariableW(L"BULIJIE_SCRIPT_LOG", previous.data(), needed);
        previous_script_log_ = previous.data();
    }
    script_environment_set_ = SetEnvironmentVariableW(L"BULIJIE_SCRIPT_LOG", script_log_.c_str()) != FALSE;
    if (!script_environment_set_) error_ = system_error(GetLastError());
    return script_environment_set_;
}
void Session::read_script_log() {
    if (script_log_.empty()) return;
    std::string raw;
    std::wstring message;
    if (!read_file(script_log_, raw, message, 3 * 1024 * 1024) || raw.size() <= script_log_read_) return;
    size_t offset = script_log_read_;
    script_log_read_ = raw.size() - raw.size() % 2;
    if (!offset && raw.rfind("\xff\xfe", 0) == 0) offset = 2;
    if (offset >= script_log_read_) return;
    std::wstring text((script_log_read_ - offset) / 2, L'\0');
    memcpy(text.data(), raw.data() + offset, script_log_read_ - offset);
    log(PRG_INFO, text);
}
void Session::cleanup() {
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        command_ = INVALID_SOCKET;
    }
    if (vpn_) {
        openconnect_clear_cookie(vpn_);
        openconnect_vpninfo_free(vpn_);
        vpn_ = nullptr;
    }
    read_script_log();
    if (script_environment_set_) {
        SetEnvironmentVariableW(L"BULIJIE_SCRIPT_LOG", previous_script_log_.empty() ? nullptr : previous_script_log_.c_str());
        script_environment_set_ = false;
    }
    if (!script_log_.empty()) { DeleteFileW(script_log_.c_str()); script_log_.clear(); }
    if (script_environment_lock_.owns_lock()) script_environment_lock_.unlock();
    erase(pending_password_);
    erase(profile_.password);
    for (auto &value : secrets_)
        erase(value);
    secrets_.clear();
}
void Session::run() {
    Handle session_lock(
        CreateMutexW(nullptr, FALSE, (L"Local\\BulijieVPN.Session." + wide(profile_.id)).c_str()));
    DWORD acquired = session_lock ? WaitForSingleObject(session_lock.get(), 0) : WAIT_FAILED;
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
        state(State::Failed, true,
              tr(language_, L"这个配置已在另一个客户端实例中连接。",
                 L"This profile is already connected in another client instance."));
        return;
    }
    struct Unlock {
        HANDLE handle;
        ~Unlock() {
            ReleaseMutex(handle);
        }
    } unlock{session_lock.get()};
    auto work = [&]() {
        std::wstring normalized;
        if (!validate_profile(profile_, error_) || !normalize_gateway(profile_.gateway, normalized, &origin_))
            return false;
        if (!authentication_only_) {
            if (!administrator()) {
                error_ = tr(language_, L"建立 VPN 网卡需要管理员权限。",
                            L"Administrator privileges are required to create the VPN adapter.");
                return false;
            }
            if (GetFileAttributesW((executable_directory() / L"wintun.dll").c_str()) ==
                INVALID_FILE_ATTRIBUTES) {
                error_ =
                    tr(language_, L"便携包缺少 wintun.dll。", L"The portable package is missing wintun.dll.");
                return false;
            }
            auto script = profile_.script.empty() ? executable_directory() / L"vpnc-script-win.js"
                                                  : std::filesystem::path(profile_.script);
            if (!std::filesystem::is_regular_file(script)) {
                error_ = tr(language_, L"找不到 VPN 网络配置脚本。",
                            L"The VPN network configuration script could not be found.");
                return false;
            }
            if (!prepare_script_log()) return false;
        }
        state(State::Connecting);
        const std::string user_agent = std::string("BulijieVPN/") + Version;
        vpn_ = openconnect_vpninfo_new(user_agent.c_str(), certificate_callback, nullptr,
                                       authentication_callback, progress_callback, this);
        if (!vpn_) {
            error_ = tr(language_, L"无法创建 VPN 会话。", L"Could not create the VPN session.");
            return false;
        }
        openconnect_set_loglevel(vpn_, PRG_TRACE);
        openconnect_set_reported_os(vpn_, "win");
        openconnect_set_system_trust(vpn_, 0);
        if (openconnect_set_protocol(vpn_, profile_.protocol.c_str()) ||
            openconnect_parse_url(vpn_, utf8(profile_.gateway).c_str())) {
            error_ = tr(language_, L"网关或 VPN 协议无效。", L"Invalid gateway or VPN protocol.");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(command_mutex_);
            command_ = openconnect_setup_cmd_pipe(vpn_);
            if (command_ == INVALID_SOCKET)
                return false;
        }
        u_long nonblocking = 1;
        ioctlsocket(command_, FIONBIO, &nonblocking);
        if (stopped())
            return false;
        openconnect_set_stats_handler(vpn_, statistics_callback);
        openconnect_set_reconnected_handler(vpn_, reconnected_callback);
        if (!authentication_only_)
            openconnect_set_setup_tun_handler(vpn_, setup_tun_callback);
        if (profile_.disable_udp)
            openconnect_disable_dtls(vpn_);
        if (!profile_.certificate_file.empty()) {
            auto certificate = utf8(profile_.certificate_file), key = utf8(profile_.key_file);
            if (openconnect_set_client_cert(vpn_, certificate.c_str(), key.empty() ? nullptr : key.c_str()) <
                0) {
                error_ = tr(language_, L"无法使用所选用户证书。",
                            L"Could not use the selected client certificate.");
                return false;
            }
        }
        if (profile_.token_type >= 0 && !profile_.token.empty()) {
            openconnect_set_token_callbacks(vpn_, this, lock_token_callback, unlock_token_callback);
            if (openconnect_set_token_mode(vpn_, static_cast<oc_token_mode_t>(profile_.token_type),
                                           profile_.token.c_str()) < 0) {
                error_ = tr(language_, L"OTP 令牌格式无效或不受支持。",
                            L"The OTP token is invalid or unsupported.");
                return false;
            }
        }
        if (profile_.use_proxy) {
            auto proxy = system_proxy(profile_.gateway);
            if (!proxy.empty() && openconnect_set_http_proxy(vpn_, proxy.c_str()) < 0) {
                error_ = tr(language_, L"无法使用系统代理。", L"Could not use the system proxy.");
                return false;
            }
        }
        if (openconnect_obtain_cookie(vpn_) != 0 || stopped())
            return false;
        if (current_origin() == origin_)
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
        erase(profile_.password);
        erase(pending_password_);
        if (authentication_only_)
            return true;
        if (openconnect_make_cstp_connection(vpn_) < 0 || tun_failed_ || stopped())
            return false;
        if (!profile_.disable_udp && openconnect_setup_dtls(vpn_, profile_.dtls_period) < 0)
            log(PRG_INFO, tr(language_, L"UDP 暂不可用，继续使用 TLS 连接。",
                             L"UDP is unavailable; continuing with the TLS connection."));
        if (!tun_ready_)
            state(State::Configuring);
        while (!stopped()) {
            int result = openconnect_mainloop(vpn_, profile_.reconnect_timeout, RECONNECT_INTERVAL_MIN);
            if (tun_failed_)
                return false;
            if (result < 0)
                return stopped();
        }
        return true;
    };
    bool ok = work();
    bool canceled = stopped();
    if (tun_ready_)
        state(State::Disconnecting);
    cleanup();
    if (ok || canceled)
        state(State::Idle, true);
    else
        state(State::Failed, true,
              error_.empty() ? tr(language_, L"连接失败，请查看日志中的服务器返回信息。",
                                  L"Connection failed. Check the server response in the log.")
                             : error_);
}
} // namespace bulijie
