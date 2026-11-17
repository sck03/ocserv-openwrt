// Native adaptation of OpenConnect GUI v1.6.2 (GPL-2.0-or-later).
#include "session.h"
#include <algorithm>
#include <cstring>

namespace vpn {
namespace {
bool is_field(const char *value, const char *expected) {
    return value && _stricmp(value, expected) == 0;
}
std::wstring safe(const char *value) {
    return value ? wide(value) : std::wstring();
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
        log(PRG_ERR, utf8(error));
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
        log(PRG_INFO, form->banner);
    if (form->message)
        log(PRG_INFO, form->message);
    if (form->error)
        log(PRG_ERR, form->error);
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
    for (oc_form_opt *option = form->opts; option; option = option->next) {
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
} // namespace vpn
