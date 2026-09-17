// Dialog layout and behavior follow OpenConnect GUI v1.6.2 (GPL-2.0-or-later).
#include "ui.h"
#include <algorithm>

namespace bulijie::ui {
namespace {
constexpr wchar_t CertificateFilter[] =
    L"Certificates / 证书 (*.pem;*.crt;*.cer;*.p12;*.pfx)\0*.pem;*.crt;*.cer;*.p12;*.pfx\0All files / "
    L"所有文件\0*.*\0\0";
constexpr wchar_t AllFiles[] = L"All files / 所有文件\0*.*\0\0";

class CertificatePicker final : public Dialog {
public:
    explicit CertificatePicker(Language language)
        : language_(language), certificates_(system_certificates()) {}
    std::optional<SystemCertificate> selected;

private:
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_CREATE) {
            control(L"STATIC",
                    tr(language_, L"选择 Windows 个人证书库中的用户证书：",
                       L"Select a client certificate from the Windows personal store:"),
                    0, -1, 16, 16, 508, 38);
            HWND choices = combo(SystemStore, 16, 60, 508);
            for (size_t i = 0; i < certificates_.size(); ++i)
                add_choice(choices, certificates_[i].label, static_cast<LPARAM>(i));
            select_choice(choices, 0);
            if (certificates_.empty()) {
                control(
                    L"STATIC",
                    tr(language_, L"未找到可用证书。请先导入带私钥的个人证书。",
                       L"No certificates found. Import a personal certificate with its private key first."),
                    0, -1, 16, 102, 508, 48);
            }
            button(tr(language_, L"选择", L"Select"), IDOK, 332, 164, 92, 27, BS_DEFPUSHBUTTON);
            button(tr(language_, L"取消", L"Cancel"), IDCANCEL, 432, 164, 92);
            EnableWindow(item(IDOK), !certificates_.empty());
            initial_focus_ = SystemStore;
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(wparam) == IDOK && !certificates_.empty()) {
            auto index = static_cast<size_t>(selected_choice(item(SystemStore)));
            if (index < certificates_.size()) {
                selected = certificates_[index];
                finish(true);
            }
            return 0;
        }
        return Dialog::message(message, wparam, lparam);
    }
    Language language_;
    std::vector<SystemCertificate> certificates_;
};

class ProfileEditor final : public Dialog {
public:
    ProfileEditor(ProfileStore &store, Profile profile, Language language, bool advanced)
        : result(std::move(profile)), store_(store), language_(language), advanced_(advanced),
          protocols_(supported_protocols()) {}
    Profile result;
    bool customize = false;

private:
    const wchar_t *t(const wchar_t *zh, const wchar_t *en) const {
        return tr(language_, zh, en);
    }
    void caption(const wchar_t *zh, const wchar_t *en, int x, int y, int width = 122) {
        control(L"STATIC", t(zh, en), 0, -1, x, y + 3, width, 22);
    }
    void create_fields() {
        caption(L"名称", L"Name", 16, 16);
        edit(result.name, ProfileName, 146, 16, 454);
        caption(L"网关", L"Gateway", 16, 50);
        edit(result.gateway, ProfileGateway, 146, 50, 454);
        SendMessageW(item(ProfileGateway), EM_SETCUEBANNER, FALSE,
                     reinterpret_cast<LPARAM>(L"https://my_server[:443]/[usergroup]"));
        caption(L"VPN 协议", L"VPN protocol", 16, 84);
        HWND protocol = combo(ProfileProtocol, 146, 84, 454);
        for (size_t i = 0; i < protocols_.size(); ++i)
            add_choice(protocol, protocols_[i].label, static_cast<LPARAM>(i));
        auto selected = std::find_if(protocols_.begin(), protocols_.end(),
                                     [&](const auto &p) { return p.name == result.protocol; });
        if (selected == protocols_.end()) {
            protocols_.push_back({result.protocol, wide(result.protocol), L""});
            add_choice(protocol, wide(result.protocol), static_cast<LPARAM>(protocols_.size() - 1));
            select_choice(protocol, static_cast<LPARAM>(protocols_.size() - 1));
        } else
            select_choice(protocol, std::distance(protocols_.begin(), selected));
        initial_focus_ = ProfileName;
        if (!advanced_) {
            control(L"STATIC",
                    t(L"稍后可在“编辑配置”中设置证书、令牌和连接选项。",
                      L"You can edit certificates, tokens and connection options later."),
                    0, -1, 16, 124, 584, 36);
            button(t(L"自定义…", L"Customize…"), Customize, 16, 180, 114);
            button(t(L"保存", L"Save"), IDOK, 406, 180, 92, 27, BS_DEFPUSHBUTTON);
            button(t(L"取消", L"Cancel"), IDCANCEL, 508, 180, 92);
            return;
        }
        caption(L"用户名", L"Username", 16, 118);
        edit(wide(result.username), ProfileUsername, 146, 118, 168);
        caption(L"分组", L"Group", 330, 118, 74);
        edit(wide(result.group), ProfileGroup, 410, 118, 190);
        caption(L"CA 证书", L"CA certificate", 16, 152);
        edit(result.ca_file, ProfileCA, 146, 152, 340);
        button(L"…", BrowseCA, 492, 152, 34);
        button(t(L"清除", L"Clear"), ClearCA, 532, 152, 68);
        caption(L"服务器指纹", L"Server certificate", 16, 186);
        edit(wide(result.server_pin), ProfilePin, 146, 186, 380);
        button(t(L"清除", L"Clear"), ClearPin, 532, 186, 68);
        caption(L"OTP 令牌", L"OTP token", 16, 220);
        HWND tokens = combo(TokenType, 146, 220, 134);
        add_choice(tokens, t(L"不使用", L"None"), -1);
        if (openconnect_has_oath_support()) {
            add_choice(tokens, L"HOTP (RFC4226)", OC_TOKEN_MODE_HOTP);
            add_choice(tokens, L"TOTP (RFC6238)", OC_TOKEN_MODE_TOTP);
        }
        if (openconnect_has_stoken_support())
            add_choice(tokens, L"STOKEN (RSA)", OC_TOKEN_MODE_STOKEN);
        select_choice(tokens, result.token_type);
        edit(wide(result.token), TokenValue, 288, 220, 264, 25, ES_PASSWORD);
        button(L"…", ImportToken, 560, 220, 40);
        caption(L"接口名称", L"Interface name", 16, 254);
        edit(result.interface_name, InterfaceName, 146, 254, 454);
        SendMessageW(item(InterfaceName), EM_SETCUEBANNER, FALSE,
                     reinterpret_cast<LPARAM>(t(L"留空使用默认名称", L"Leave blank for the default name")));
        caption(L"vpnc 脚本", L"vpnc script", 16, 288);
        edit(result.script, Script, 146, 288, 406);
        button(L"…", BrowseScript, 560, 288, 40);
        control(L"BUTTON", t(L"用户证书", L"Client certificate"), BS_GROUPBOX, -1, 16, 326, 584, 130);
        caption(L"证书文件", L"Certificate file", 28, 350, 108);
        edit(result.certificate_file, Certificate, 146, 350, 340);
        button(L"…", BrowseCertificate, 492, 350, 34);
        button(t(L"清除", L"Clear"), ClearCertificate, 532, 350, 56);
        caption(L"私钥文件", L"Private key", 28, 384, 108);
        edit(result.key_file, PrivateKey, 146, 384, 340);
        button(L"…", BrowseKey, 492, 384, 34);
        button(t(L"清除", L"Clear"), ClearKey, 532, 384, 56);
        button(t(L"从 Windows 证书库选择…", L"Select from Windows store…"), SystemStore, 146, 419, 268);
        button(t(L"重置", L"Reset"), ResetStore, 426, 419, 76);
        caption(L"重连超时（秒）", L"Reconnect (sec)", 16, 471);
        edit(std::to_wstring(result.reconnect_timeout), ReconnectTimeout, 146, 468, 76, 25, ES_NUMBER);
        caption(L"DTLS 间隔（秒）", L"DTLS period (sec)", 238, 471, 144);
        edit(std::to_wstring(result.dtls_period), DTLSPeriod, 386, 468, 76, 25, ES_NUMBER);
        HWND logs = combo(LogLevel, 474, 468, 126);
        add_choice(logs, t(L"默认日志级别", L"(App default)"), -1);
        add_choice(logs, t(L"错误", L"Error"), 0);
        add_choice(logs, t(L"信息", L"Info"), 1);
        add_choice(logs, t(L"调试", L"Debug"), 2);
        add_choice(logs, t(L"跟踪", L"Trace"), 3);
        select_choice(logs, result.log_level);
        button(t(L"连接后最小化", L"Minimize on connect"), MinimizeOnConnect, 16, 508, 224, 23,
               BS_AUTOCHECKBOX);
        button(t(L"批处理模式（记住密码）", L"Batch mode (remember password)"), BatchMode, 270, 508, 330, 23,
               BS_AUTOCHECKBOX);
        button(t(L"禁用 UDP", L"Disable UDP"), DisableUDP, 16, 536, 224, 23, BS_AUTOCHECKBOX);
        button(t(L"使用系统代理", L"Use system proxy"), UseProxy, 270, 536, 330, 23, BS_AUTOCHECKBOX);
        check(MinimizeOnConnect, result.minimize_on_connect);
        check(BatchMode, result.batch_mode);
        check(DisableUDP, result.disable_udp);
        check(UseProxy, result.use_proxy);
        button(t(L"保存", L"Save"), IDOK, 406, 582, 92, 27, BS_DEFPUSHBUTTON);
        button(t(L"取消", L"Cancel"), IDCANCEL, 508, 582, 92);
    }
    bool read_fields(bool validate) {
        result.name = value(ProfileName);
        result.gateway = value(ProfileGateway);
        auto protocol = static_cast<size_t>(selected_choice(item(ProfileProtocol)));
        if (protocol < protocols_.size())
            result.protocol = protocols_[protocol].name;
        if (advanced_) {
            result.username = utf8(value(ProfileUsername));
            result.group = utf8(value(ProfileGroup));
            result.ca_file = trim(value(ProfileCA));
            auto pin = utf8(trim(value(ProfilePin)));
            if (pin != result.server_pin) {
                result.server_pin = pin;
                result.fixed_pin = !pin.empty();
            }
            result.token_type = static_cast<int>(selected_choice(item(TokenType), -1));
            auto token = utf8(value(TokenValue));
            if (token != result.token) {
                result.token = token;
                result.token_blob.clear();
            }
            if (result.token_type < 0) {
                erase(result.token);
                result.token_blob.clear();
            }
            result.interface_name = trim(value(InterfaceName));
            result.script = trim(value(Script));
            result.certificate_file = trim(value(Certificate));
            result.key_file = trim(value(PrivateKey));
            result.log_level = static_cast<int>(selected_choice(item(LogLevel), -1));
            auto number = [&](int id, int &target) {
                auto text = trim(value(id));
                if (text.empty() ||
                    !std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                    return false;
                try {
                    target = std::stoi(text);
                    return true;
                } catch (...) {
                    return false;
                }
            };
            if (!number(ReconnectTimeout, result.reconnect_timeout) ||
                !number(DTLSPeriod, result.dtls_period)) {
                error(window_,
                      t(L"请填写有效的重连时间和 DTLS 间隔。", L"Enter valid reconnect and DTLS periods."));
                return false;
            }
            result.minimize_on_connect = checked(MinimizeOnConnect);
            result.batch_mode = checked(BatchMode);
            result.disable_udp = checked(DisableUDP);
            result.use_proxy = checked(UseProxy);
            if (!result.batch_mode) {
                erase(result.password);
                result.password_blob.clear();
            }
        }
        if (!validate)
            return true;
        std::wstring message;
        if (!validate_profile(result, message)) {
            error(window_, message);
            return false;
        }
        if (!result.ca_file.empty()) {
            std::wstring fingerprint;
            if (!certificate_file_info(result.ca_file, fingerprint, message)) {
                error(window_, message);
                return false;
            }
        }
        for (const auto *path : {&result.certificate_file, &result.key_file, &result.script}) {
            if (path->empty() || path->rfind(L"system:", 0) == 0 || path->rfind(L"pkcs11:", 0) == 0)
                continue;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(*path, ec)) {
                error(window_, std::wstring(t(L"找不到文件：\n", L"File not found:\n")) + *path);
                return false;
            }
        }
        return true;
    }
    void browse(int id) {
        int target = id == BrowseCA            ? ProfileCA
                     : id == BrowseCertificate ? Certificate
                     : id == BrowseKey         ? PrivateKey
                     : id == BrowseScript      ? Script
                                               : TokenValue;
        auto path = choose_file(window_, false,
                                id == BrowseCA || id == BrowseCertificate ? CertificateFilter : AllFiles);
        if (!path)
            return;
        std::wstring message;
        if (id == ImportToken) {
            std::string raw;
            if (!read_file(*path, raw, message, 8192)) {
                error(window_, message);
                return;
            }
            label(target, trim(wide(raw)));
            erase(raw);
            return;
        }
        if (id == BrowseCA) {
            std::wstring fingerprint;
            if (!certificate_file_info(*path, fingerprint, message)) {
                error(window_, message);
                return;
            }
        }
        label(target, path->wstring());
    }
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_CREATE) {
            create_fields();
            return 0;
        }
        if (message == WM_COMMAND) {
            int id = LOWORD(wparam);
            if (id == IDOK) {
                if (!read_fields(true))
                    return 0;
                std::wstring reason;
                if (!store_.save(result, reason)) {
                    error(window_, reason);
                    return 0;
                }
                finish(true);
                return 0;
            }
            if (id == Customize) {
                if (read_fields(false)) {
                    customize = true;
                    finish(true);
                }
                return 0;
            }
            if (id == BrowseCA || id == BrowseCertificate || id == BrowseKey || id == BrowseScript ||
                id == ImportToken) {
                browse(id);
                return 0;
            }
            if (id == ClearCA || id == ClearPin || id == ClearCertificate || id == ClearKey) {
                label(id == ClearCA            ? ProfileCA
                      : id == ClearPin         ? ProfilePin
                      : id == ClearCertificate ? Certificate
                                               : PrivateKey,
                      L"");
                return 0;
            }
            if (id == ResetStore) {
                label(Certificate, L"");
                label(PrivateKey, L"");
                return 0;
            }
            if (id == SystemStore) {
                CertificatePicker picker(language_);
                if (picker.run(window_, L"BulijieVPN.Certificates",
                               t(L"Windows 用户证书", L"Windows client certificates"), 540, 206) &&
                    picker.selected) {
                    label(Certificate, picker.selected->certificate);
                    label(PrivateKey, picker.selected->key);
                }
                return 0;
            }
        }
        return Dialog::message(message, wparam, lparam);
    }
    ProfileStore &store_;
    Language language_;
    bool advanced_;
    std::vector<Protocol> protocols_;
};

class AuthenticationDialog final : public Dialog {
public:
    AuthenticationDialog(std::shared_ptr<Prompt> prompt, Language language, std::function<bool()> canceled)
        : prompt_(std::move(prompt)), language_(language), canceled_(std::move(canceled)) {}

private:
    const wchar_t *t(const wchar_t *zh, const wchar_t *en) const {
        return tr(language_, zh, en);
    }
    void create_fields() {
        bool certificate = prompt_->kind == Prompt::Kind::Certificate;
        control(L"STATIC", t(L"服务器", L"Server"), 0, -1, 16, 18, 80, 22);
        control(L"EDIT", prompt_->server, ES_READONLY | ES_AUTOHSCROLL, -1, 102, 16, 502, 25,
                WS_EX_CLIENTEDGE);
        std::wstring message = prompt_->banner;
        for (const auto *part : {&prompt_->message, &prompt_->error})
            if (!part->empty()) {
                if (!message.empty())
                    message += L"\r\n";
                message += *part;
            }
        if (certificate) {
            message = std::wstring(t(L"请向管理员核对服务器地址和完整指纹，再决定是否信任此服务器。\r\n",
                                     L"Verify the server address and complete fingerprint with your "
                                     L"administrator before trusting this server.\r\n")) +
                      message;
            control(L"EDIT", message, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, -1, 16, 54,
                    588, 72, WS_EX_CLIENTEDGE);
            control(L"STATIC", t(L"当前服务器指纹", L"Current server fingerprint"), 0, -1, 16, 138, 588, 22);
            edit(wide(prompt_->pin), PromptText, 16, 163, 588, 25, ES_READONLY);
            int top = 204;
            if (!prompt_->previous_pin.empty()) {
                control(
                    L"STATIC",
                    t(L"指纹已变化，上次保存的指纹：", L"The key has changed. Previously saved fingerprint:"),
                    0, -1, 16, 200, 588, 22);
                control(L"EDIT", wide(prompt_->previous_pin), ES_READONLY | ES_AUTOHSCROLL, -1, 16, 225, 588,
                        25, WS_EX_CLIENTEDGE);
                top = 263;
            }
            edit(prompt_->details, PromptDetails, 16, top, 588, 440 - top,
                 ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
            button(t(L"复制指纹", L"Copy fingerprint"), PromptCopy, 16, 460, 138);
            button(t(L"信息准确，记住并连接", L"Accurate information — connect"), IDOK, 172, 460, 316);
            button(t(L"取消", L"Cancel"), IDCANCEL, 500, 460, 104, 27, BS_DEFPUSHBUTTON);
            initial_focus_ = IDCANCEL;
            default_button_ = IDCANCEL;
        } else {
            control(L"EDIT", message, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, -1, 16, 54,
                    588, 92, WS_EX_CLIENTEDGE);
            auto field = prompt_->label;
            if (field.empty())
                field = prompt_->kind == Prompt::Kind::Password
                            ? t(L"密码或验证码", L"Password or verification code")
                            : t(L"认证信息", L"Authentication response");
            control(L"STATIC", field, 0, -1, 16, 158, 588, 36);
            if (prompt_->kind == Prompt::Kind::Selection) {
                HWND choices = combo(PromptText, 16, 202, 588);
                for (size_t i = 0; i < prompt_->choices.size(); ++i)
                    add_choice(choices, prompt_->choices[i].label, static_cast<LPARAM>(i));
                select_choice(choices, 0);
            } else if (prompt_->kind != Prompt::Kind::Notice) {
                edit(wide(prompt_->initial), PromptText, 16, 202, 588, 27,
                     prompt_->kind == Prompt::Kind::Password ? ES_PASSWORD : 0);
                if (prompt_->kind == Prompt::Kind::Password)
                    button(t(L"显示密码", L"Show password"), PromptShowPassword, 16, 240, 230, 24,
                           BS_AUTOCHECKBOX);
            }
            button(t(L"确定", L"OK"), IDOK, 392, 288, 100, 27, BS_DEFPUSHBUTTON);
            button(t(L"取消", L"Cancel"), IDCANCEL, 504, 288, 100);
            initial_focus_ = prompt_->kind == Prompt::Kind::Notice ? IDOK : PromptText;
        }
        SetTimer(window_, 1, 100, nullptr);
    }
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_CREATE) {
            create_fields();
            return 0;
        }
        if (message == WM_TIMER && (canceled_() || prompt_->done.load())) {
            finish(false);
            return 0;
        }
        if (message == WM_COMMAND) {
            int id = LOWORD(wparam);
            if (id == PromptShowPassword) {
                SendMessageW(item(PromptText), EM_SETPASSWORDCHAR, checked(PromptShowPassword) ? 0 : 0x25cf,
                             0);
                InvalidateRect(item(PromptText), nullptr, TRUE);
                return 0;
            }
            if (id == PromptCopy) {
                copy_text(window_, wide(prompt_->pin));
                return 0;
            }
            if (id == IDOK) {
                std::string response;
                if (prompt_->kind == Prompt::Kind::Selection) {
                    auto index = static_cast<size_t>(selected_choice(item(PromptText)));
                    if (index >= prompt_->choices.size())
                        return 0;
                    response = prompt_->choices[index].value;
                } else if (prompt_->kind == Prompt::Kind::Text || prompt_->kind == Prompt::Kind::Password)
                    response = utf8(value(PromptText));
                prompt_->answer(true, std::move(response));
                if (item(PromptText))
                    SetWindowTextW(item(PromptText), L"");
                finish(true);
                return 0;
            }
        }
        return Dialog::message(message, wparam, lparam);
    }
    std::shared_ptr<Prompt> prompt_;
    Language language_;
    std::function<bool()> canceled_;
};
} // namespace

bool edit_profile(HWND owner, ProfileStore &store, Profile &profile, Language language, bool advanced) {
    Profile candidate = profile;
    for (;;) {
        ProfileEditor dialog(store, candidate, language, advanced);
        auto title = profile.id.empty() ? tr(language, L"新建配置", L"New profile")
                                        : tr(language, L"编辑配置", L"Edit profile");
        if (!dialog.run(owner, L"BulijieVPN.Profile", title, 616, advanced ? 624 : 224))
            return false;
        candidate = std::move(dialog.result);
        if (dialog.customize) {
            advanced = true;
            continue;
        }
        profile = std::move(candidate);
        return true;
    }
}
void show_prompt(HWND owner, const std::shared_ptr<Prompt> &prompt, Language language,
                 const std::function<bool()> &canceled) {
    if (canceled() || prompt->done.load()) {
        prompt->answer(false);
        return;
    }
    AuthenticationDialog dialog(prompt, language, canceled);
    dialog.run(owner, L"BulijieVPN.Authentication", prompt->title, 620,
               prompt->kind == Prompt::Kind::Certificate ? 504 : 332);
    if (!prompt->done.load())
        prompt->answer(false);
}
void show_about(HWND owner, Language language, bool license) {
    std::wstring text = std::wstring(Product) + L" " + wide(Version) + L"\nOpenConnect " +
                        wide(openconnect_get_version()) + L"\n\n";
    if (license)
        text += tr(language,
                   L"应用：GPL-3.0-or-later\n界面参考：OpenConnect GUI 1.6.2，GPL-2.0-or-later\nOpenConnect "
                   L"/ GnuTLS：LGPL-2.1-or-later\n\n完整许可证及上游来源见便携包 licenses 目录和 "
                   L"THIRD-PARTY-NOTICES.md。",
                   L"Application: GPL-3.0-or-later\nUI reference: OpenConnect GUI 1.6.2, "
                   L"GPL-2.0-or-later\nOpenConnect / GnuTLS: LGPL-2.1-or-later\n\nSee licenses/ and "
                   L"THIRD-PARTY-NOTICES.md in the portable package for complete notices.");
    else
        text += tr(language,
                   L"基于官方 OpenConnect 核心的原生 Windows 客户端。\n\n默认中文，可切换英文；无需安装 "
                   L"Qt、.NET 或 VC 运行库。\n建立 VPN 网络时需要 Windows 管理员授权。",
                   L"Native Windows client using the official OpenConnect core.\n\nChinese and English. No "
                   L"Qt, .NET or VC runtime installation required.\nCreating a VPN connection requires "
                   L"Windows administrator approval.");
    MessageBoxW(owner, text.c_str(),
                tr(language, license ? L"许可证信息" : L"关于布利杰VPN",
                   license ? L"License information" : L"About BulijieVPN"),
                MB_OK | MB_ICONINFORMATION);
}
} // namespace bulijie::ui
