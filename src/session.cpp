#include "session.h"
#include <wincrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <new>
#include <cstddef>

namespace bridge {
static HCERTSTORE load_ca_store(const std::wstring& filename, DWORD& error) {
    Handle file(CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) { error = GetLastError(); return nullptr; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) { error = ERROR_INVALID_DATA; return nullptr; }
    std::string contents(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.get(), contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr) || read != contents.size()) { error = ERROR_READ_FAULT; return nullptr; }
    HCERTSTORE roots = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    if (!roots) { error = GetLastError(); return nullptr; }
    size_t cursor = 0;
    unsigned count = 0;
    const std::string begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
    while ((cursor = contents.find(begin, cursor)) != std::string::npos) {
        size_t close = contents.find(end, cursor + begin.size());
        if (close == std::string::npos || ++count > 64) { error = ERROR_INVALID_DATA; CertCloseStore(roots,0); return nullptr; }
        DWORD length = static_cast<DWORD>(close + end.size() - cursor), bytes = 0;
        const char* block = contents.data() + cursor;
        if (!CryptStringToBinaryA(block, length, CRYPT_STRING_BASE64HEADER, nullptr, &bytes, nullptr, nullptr)) { error = GetLastError(); CertCloseStore(roots,0); return nullptr; }
        std::vector<BYTE> encoded(bytes);
        if (!CryptStringToBinaryA(block, length, CRYPT_STRING_BASE64HEADER, encoded.data(), &bytes, nullptr, nullptr) ||
            !CertAddEncodedCertificateToStore(roots, X509_ASN_ENCODING, encoded.data(), bytes, CERT_STORE_ADD_USE_EXISTING, nullptr)) {
            error = GetLastError(); CertCloseStore(roots,0); return nullptr;
        }
        cursor = close + end.size();
    }
    if (!count) { error = ERROR_INVALID_DATA; CertCloseStore(roots,0); return nullptr; }
    return roots;
}
int verify_windows_certificate(openconnect_info* vpn, const std::wstring& hostname, const std::wstring& ca_file, DWORD& error) {
    error = CERT_E_UNTRUSTEDROOT;
    unsigned char* der = nullptr;
    const int size = openconnect_get_peer_cert_DER(vpn, &der);
    if (size <= 0 || !der) return -1;
    PCCERT_CONTEXT leaf = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der, static_cast<DWORD>(size));
    openconnect_free_cert_info(vpn, der);
    if (!leaf) { error = GetLastError(); return -1; }
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    if (!store) { error = GetLastError(); CertFreeCertificateContext(leaf); return -1; }
    oc_cert* certificates = nullptr;
    int count = openconnect_get_peer_cert_chain(vpn, &certificates);
    bool valid_chain = count >= 0 && count <= 32;
    if (valid_chain) {
        for (int i = 0; i < count; ++i) {
            if (certificates[i].der_len <= 0 || !CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                certificates[i].der_data, static_cast<DWORD>(certificates[i].der_len), CERT_STORE_ADD_USE_EXISTING, nullptr)) { valid_chain = false; break; }
        }
    }
    if (certificates) openconnect_free_peer_cert_chain(vpn, certificates);
    bool accepted = false;
    HCERTSTORE roots = nullptr;
    HCERTCHAINENGINE engine = nullptr;
    if (!ca_file.empty()) {
        roots = load_ca_store(ca_file, error);
        if (roots) {
            CERT_CHAIN_ENGINE_CONFIG config{};
            config.cbSize = sizeof(config);
            config.hExclusiveRoot = roots;
            config.dwUrlRetrievalTimeout = 5000;
            config.dwFlags = CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
            if (!CertCreateCertificateChainEngine(&config, &engine)) { error = GetLastError(); valid_chain = false; }
        } else valid_chain = false;
    }
    if (valid_chain) {
        CERT_CHAIN_PARA parameters{};
        // Windows 7 ends this structure before the Windows 8 strong-sign fields.
        parameters.cbSize = static_cast<DWORD>(offsetof(CERT_CHAIN_PARA, pStrongSignPara));
        LPSTR usage = const_cast<char*>(szOID_PKIX_KP_SERVER_AUTH);
        parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        parameters.RequestedUsage.Usage.cUsageIdentifier = 1;
        parameters.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
        parameters.dwUrlRetrievalTimeout = 5000;
        PCCERT_CHAIN_CONTEXT chain = nullptr;
        DWORD flags = ca_file.empty() ? CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT : CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
        if (CertGetCertificateChain(engine, leaf, nullptr, store, &parameters, flags, nullptr, &chain)) {
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
            ssl.cbSize = sizeof(ssl);
            ssl.dwAuthType = AUTHTYPE_SERVER;
            ssl.pwszServerName = const_cast<wchar_t*>(hostname.c_str());
            CERT_CHAIN_POLICY_PARA policy{};
            policy.cbSize = sizeof(policy);
            policy.pvExtraPolicyPara = &ssl;
            CERT_CHAIN_POLICY_STATUS status{};
            status.cbSize = sizeof(status);
            if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) {
                error = status.dwError;
                accepted = error == ERROR_SUCCESS;
            } else error = GetLastError();
            CertFreeCertificateChain(chain);
        } else error = GetLastError();
    }
    if (engine) CertFreeCertificateChainEngine(engine);
    if (roots) CertCloseStore(roots, 0);
    CertCloseStore(store, 0);
    CertFreeCertificateContext(leaf);
    return accepted ? 0 : -1;
}
Session::Session(ConnectOptions options, Sink sink) : options_(std::move(options)), sink_(std::move(sink)),
    cancel_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    InitializeCriticalSection(&command_lock_);
}
Session::~Session() {
    cancel();
    if (thread_) WaitForSingleObject(thread_.get(), INFINITE);
    erase(options_.password);
    erase(options_.username);
    DeleteCriticalSection(&command_lock_);
}
bool Session::start() {
    if (thread_ || !cancel_event_) return false;
    thread_.reset(CreateThread(nullptr, 0, thread_entry, this, 0, nullptr));
    return static_cast<bool>(thread_);
}
bool Session::stopping() const { return cancel_event_ && WaitForSingleObject(cancel_event_.get(), 0) == WAIT_OBJECT_0; }
bool Session::finished() const { return !thread_ || WaitForSingleObject(thread_.get(), 0) == WAIT_OBJECT_0; }
bool Session::send(char command) {
    EnterCriticalSection(&command_lock_);
    bool sent = command_ != INVALID_SOCKET && ::send(command_, &command, 1, 0) == 1;
    LeaveCriticalSection(&command_lock_);
    return sent;
}
void Session::cancel() { if (cancel_event_) SetEvent(cancel_event_.get()); send(OC_CMD_CANCEL); }
void Session::request_stats() {
    if (stopping()) return;
    bool expected = false;
    if (statistics_pending_.compare_exchange_strong(expected, true) && !send(OC_CMD_STATS))
        statistics_pending_ = false;
}
void Session::emit(State state, Error error, bool terminal, std::wstring detail) {
    state_ = state;
    Event event;
    event.state = state;
    event.error = error;
    event.terminal = terminal;
    event.detail = std::move(detail);
    sink_(std::move(event));
}
DWORD WINAPI Session::thread_entry(void* context) {
    auto& self = *static_cast<Session*>(context);
    try { self.run(); }
    catch (...) {
        self.error_ = Error::Internal;
        self.cleanup();
        try { self.emit(State::Failed, Error::Internal, true); } catch (...) {}
    }
    return 0;
}
void Session::cleanup() {
    if (!network_.reset()) {
        cleanup_failed_ = true;
        error_ = Error::Tunnel;
        detail_ += L" Network cleanup was incomplete; restart the application and check Windows networking.";
    }
    if (vpn_) {
        EnterCriticalSection(&command_lock_);
        command_ = INVALID_SOCKET;
        LeaveCriticalSection(&command_lock_);
        openconnect_clear_cookie(vpn_);
        openconnect_vpninfo_free(vpn_);
        vpn_ = nullptr;
    }
    erase(options_.password);
}
bool Session::same_origin() const {
    const char* host = openconnect_get_dnsname(vpn_);
    return host && _stricmp(host, origin_host_.c_str()) == 0 && openconnect_get_port(vpn_) == origin_port_;
}
int Session::certificate_callback(void* context, const char*) {
    auto& self = *static_cast<Session*>(context);
    try { return self.validate_certificate(); }
    catch (...) { self.error_ = Error::Certificate; return -1; }
}
int Session::validate_certificate() {
    if (stopping()) return -1;
    error_ = Error::Certificate;
    if (!same_origin()) { detail_ = L"Cross-origin VPN redirects are not allowed."; return -1; }
    if (!options_.profile.pin.empty()) {
        if (!valid_pin(options_.profile.pin) || openconnect_check_peer_cert_hash(vpn_, options_.profile.pin.c_str()) != 0) {
            detail_ = L"The server public key does not match the administrator's configured SHA-256 pin.";
            return -1;
        }
        unsigned char* der = nullptr;
        int size = openconnect_get_peer_cert_DER(vpn_, &der);
        if (size <= 0 || !der) return -1;
        auto cert = CertCreateCertificateContext(X509_ASN_ENCODING, der, static_cast<DWORD>(size));
        openconnect_free_cert_info(vpn_, der);
        if (!cert) return -1;
        bool current = CertVerifyTimeValidity(nullptr, cert->pCertInfo) == 0;
        CertFreeCertificateContext(cert);
        if (!current) { detail_ = L"The pinned server certificate is expired or not yet valid."; return -1; }
        error_ = Error::None;
        return 0;
    }
    DWORD status = 0;
    int result = verify_windows_certificate(vpn_, wide(origin_host_), options_.profile.ca_file, status);
    if (result == 0) error_ = Error::None;
    else detail_ = L"Windows certificate validation: " + system_error(status) + L" (" + std::to_wstring(status) + L")";
    return result;
}
int Session::auth_callback(void* context, oc_auth_form* form) {
    auto& self = *static_cast<Session*>(context);
    try { return self.authenticate(form); }
    catch (...) { self.error_ = Error::Internal; return OC_FORM_RESULT_ERR; }
}
int Session::authenticate(oc_auth_form* form) {
    if (stopping()) return OC_FORM_RESULT_CANCELLED;
    if (!form || !same_origin()) { error_ = Error::Authentication; return OC_FORM_RESULT_ERR; }
    if (++auth_forms_ > 8 || (form->error && *form->error && password_submissions_)) {
        error_ = Error::Authentication;
        return OC_FORM_RESULT_ERR;
    }
    emit(State::Authenticating);
    unsigned fields = 0;
    bool supplied = false;
    for (oc_form_opt* opt = form->opts; opt; opt = opt->next) {
        if (++fields > 32) { error_ = Error::UnsupportedAuth; return OC_FORM_RESULT_ERR; }
        if (opt->flags & OC_FORM_OPT_IGNORE || opt->type == OC_FORM_OPT_HIDDEN) continue;
        const char* name = opt->name ? opt->name : "";
        if (opt->type == OC_FORM_OPT_TEXT && (!_stricmp(name, "username") || !_stricmp(name, "user") || !_stricmp(name, "login"))) {
            if (openconnect_set_option_value(opt, options_.username.c_str()) != 0) return OC_FORM_RESULT_ERR;
            supplied = true;
        } else if (opt->type == OC_FORM_OPT_PASSWORD && (!_stricmp(name, "password") || !_stricmp(name, "passwd"))) {
            if (++password_submissions_ > 1) { error_ = Error::Authentication; return OC_FORM_RESULT_ERR; }
            if (openconnect_set_option_value(opt, options_.password.c_str()) != 0) return OC_FORM_RESULT_ERR;
            supplied = true;
        } else if (opt->type == OC_FORM_OPT_SELECT) {
            auto* select = reinterpret_cast<oc_form_opt_select*>(opt);
            if (select->nr_choices < 1 || select->nr_choices > 128) return OC_FORM_RESULT_ERR;
            const char* value = nullptr;
            for (int i = 0; i < select->nr_choices; ++i) {
                if (!select->choices[i] || !select->choices[i]->name) continue;
                if (options_.profile.auth_group == select->choices[i]->name || (options_.profile.auth_group.empty() && select->nr_choices == 1)) value = select->choices[i]->name;
            }
            if (!value) { error_ = Error::GroupRequired; return OC_FORM_RESULT_ERR; }
            if (openconnect_set_option_value(opt, value) != 0) return OC_FORM_RESULT_ERR;
            supplied = true;
        } else {
            error_ = Error::UnsupportedAuth;
            return OC_FORM_RESULT_ERR;
        }
    }
    if (!supplied && form->opts) { error_ = Error::UnsupportedAuth; return OC_FORM_RESULT_ERR; }
    return OC_FORM_RESULT_OK;
}
void Session::progress_callback(void* context, int level, const char* format, ...) {
    auto& self = *static_cast<Session*>(context);
    try {
        char buffer[4096]{};
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (level <= PRG_ERR) self.last_protocol_error_ = wide(redacted(buffer, self.options_.username, self.options_.password));
        if (self.connected_once_ && !self.stopping() && strstr(buffer, "SSL negotiation with")) self.emit(State::Reconnecting);
        SecureZeroMemory(buffer, sizeof(buffer));
    } catch (...) {}
}
void Session::stats_callback(void* context, const oc_stats* stats) {
    auto& self = *static_cast<Session*>(context);
    self.statistics_pending_ = false;
    try {
        if (!stats || self.stopping()) return;
        if (!self.network_.refresh_bypass_routes()) { self.error_ = Error::Tunnel; self.send(OC_CMD_CANCEL); return; }
        Event event;
        event.state = self.state_;
        event.statistics = true;
        event.rx = stats->rx_bytes;
        event.tx = stats->tx_bytes;
        const oc_ip_info* info = nullptr;
        if (openconnect_get_ip_info(self.vpn_, &info, nullptr, nullptr) == 0 && info)
            event.address = info->addr ? wide(info->addr) : (info->addr6 ? wide(info->addr6) : L"");
        event.transport = openconnect_get_dtls_cipher(self.vpn_) ? L"UDP / DTLS" : L"TCP / TLS";
        self.sink_(std::move(event));
    } catch (...) {}
}
void Session::reconnected_callback(void* context) {
    auto& self = *static_cast<Session*>(context);
    try {
        const oc_ip_info* info = nullptr;
        if (self.stopping()) return;
        if (openconnect_get_ip_info(self.vpn_, &info, nullptr, nullptr) < 0 || !info ||
            !self.network_.apply(openconnect_get_ifname(self.vpn_), *info, self.options_.profile, self.cancel_event_.get(), self.detail_)) {
            self.error_ = Error::Tunnel;
            self.send(OC_CMD_CANCEL);
        } else self.emit(State::Connected);
    } catch (...) { self.error_ = Error::Internal; self.send(OC_CMD_CANCEL); }
}
void Session::run() {
    auto work = [&]() -> bool {
        std::wstring server;
        if (!normalize_server(options_.profile.server, server) || !valid_pin(options_.profile.pin)) { error_ = Error::InvalidServer; return false; }
        if (options_.username.empty() || options_.password.empty()) { error_ = Error::MissingCredentials; return false; }
        if (!options_.authentication_only) {
            if (!is_admin()) { error_ = Error::Administrator; return false; }
            if (GetFileAttributesW((executable_dir() + L"\\wintun.dll").c_str()) == INVALID_FILE_ATTRIBUTES) { error_ = Error::MissingDriver; return false; }
        }
        if (stopping()) return false;
        emit(State::Connecting);
        vpn_ = openconnect_vpninfo_new("BridgeVPN", certificate_callback, nullptr, auth_callback, progress_callback, this);
        if (!vpn_) { error_ = Error::Internal; return false; }
        openconnect_set_loglevel(vpn_, PRG_INFO);
        openconnect_set_reported_os(vpn_, "win");
        openconnect_set_protocol(vpn_, "anyconnect");
        openconnect_set_system_trust(vpn_, 0);
        openconnect_set_pfs(vpn_, 1);
        openconnect_set_stats_handler(vpn_, stats_callback);
        openconnect_set_reconnected_handler(vpn_, reconnected_callback);
        // Keep OpenSSL's trust store empty so this callback validates EVERY new peer,
        // including redirects, using Windows trust or the explicitly isolated CA store.
        if (openconnect_parse_url(vpn_, utf8(server).c_str()) != 0) { error_ = Error::InvalidServer; return false; }
        origin_host_ = openconnect_get_dnsname(vpn_);
        origin_port_ = openconnect_get_port(vpn_);
        SOCKET command = openconnect_setup_cmd_pipe(vpn_);
        if (command == INVALID_SOCKET) { error_ = Error::Internal; return false; }
        EnterCriticalSection(&command_lock_);
        command_ = command;
        LeaveCriticalSection(&command_lock_);
        if (stopping()) { send(OC_CMD_CANCEL); return false; }
        if (!options_.profile.prefer_udp) openconnect_disable_dtls(vpn_);
        if (openconnect_obtain_cookie(vpn_) != 0) {
            if (error_ == Error::None) error_ = password_submissions_ ? Error::Authentication : Error::Network;
            return false;
        }
        erase(options_.password); // Reconnection uses the cookie, not the original password.
        if (stopping()) return false;
        if (options_.authentication_only) return true;
        if (openconnect_make_cstp_connection(vpn_) != 0) {
            if (error_ == Error::None) error_ = Error::Authentication;
            return false;
        }
        emit(State::Configuring);
        std::string adapter = "BridgeVPN-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        // Passing a null script uses the library's adapter code without cscript/vpnc-script.
        if (openconnect_setup_tun_device(vpn_, nullptr, adapter.c_str()) != 0) { error_ = Error::Tunnel; return false; }
        const oc_ip_info* info = nullptr;
        if (openconnect_get_ip_info(vpn_, &info, nullptr, nullptr) < 0 || !info ||
            !network_.apply(openconnect_get_ifname(vpn_), *info, options_.profile, cancel_event_.get(), detail_)) { error_ = Error::Tunnel; return false; }
        if (stopping()) return false;
        // DTLS is an optimization. The already established TLS tunnel remains usable if UDP is unavailable.
        if (options_.profile.prefer_udp && openconnect_setup_dtls(vpn_, 30) != 0) openconnect_disable_dtls(vpn_);
        connected_once_ = true;
        emit(State::Connected);
        const int rc = openconnect_mainloop(vpn_, options_.profile.reconnect_seconds, RECONNECT_INTERVAL_MIN);
        if (error_ != Error::None) return false;
        if (stopping() || rc == 0 || rc == -EINTR) return true;
        error_ = Error::Network;
        return false;
    };
    bool ok = work();
    const bool user_cancelled = stopping();
    if (connected_once_) emit(State::Disconnecting);
    if (!ok && detail_.empty()) detail_ = last_protocol_error_;
    cleanup();
    if ((ok || user_cancelled) && !cleanup_failed_) emit(State::Idle, Error::None, true);
    else emit(State::Failed, error_ == Error::None ? Error::Network : error_, true, detail_);
}
}
