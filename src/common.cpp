#include "common.h"
#include "settings.h"
#include <shlobj.h>
#include <algorithm>
#include <climits>
#include <cctype>
#include <cwctype>

namespace bridge {
std::string utf8(const std::wstring& text) {
    if (text.empty() || text.size() > INT_MAX) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string result(static_cast<size_t>(n), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n, nullptr, nullptr)) return {};
    return result;
}
std::wstring wide(const std::string& text) {
    if (text.empty() || text.size() > INT_MAX) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return L"[invalid UTF-8]";
    std::wstring result(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n);
    return result;
}
std::wstring executable_path() {
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return n && n < path.size() ? std::wstring(path.data(), n) : std::wstring();
}
std::wstring executable_dir() {
    auto path = executable_path();
    auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}
std::wstring settings_path() {
    wchar_t path[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, path))) return {};
    std::wstring directory = std::wstring(path) + L"\\BridgeVPN";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    return directory + L"\\settings.ini";
}
std::wstring system_error(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = n && buffer ? trim(std::wstring(buffer, n)) : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return result;
}
std::wstring trim(const std::wstring& value) {
    auto first = value.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
static bool decimal(const std::string& value, unsigned limit, unsigned& result) {
    if (value.empty() || value.size() > 5) return false;
    result = 0;
    for (char ch : value) {
        if (ch < '0' || ch > '9') return false;
        result = result * 10 + static_cast<unsigned>(ch - '0');
        if (result > limit) return false;
    }
    return true;
}
bool normalize_server(const std::wstring& input, std::wstring& output) {
    output.clear();
    auto value = trim(input);
    if (value.empty() || value.size() > 1024) return false;
    for (wchar_t ch : value)
        if (ch <= 32 || ch == 127 || ch == L'\\' || ch == L'"' || ch == L'\'' || ch == L'@' || ch == L'#' || ch == L'?') return false;
    if (value.find(L"://") == std::wstring::npos) value = L"https://" + value;
    if (_wcsnicmp(value.c_str(), L"https://", 8) != 0) return false;
    value.replace(0, 8, L"https://");
    auto slash = value.find(L'/', 8);
    auto authority = value.substr(8, slash == std::wstring::npos ? slash : slash - 8);
    if (authority.empty()) return false;
    std::wstring host, port;
    if (authority.front() == L'[') {
        auto close = authority.find(L']');
        if (close == std::wstring::npos || close == 1) return false;
        host = authority.substr(1, close - 1);
        SOCKADDR_INET addr{};
        if (!parse_address(utf8(host), addr) || addr.si_family != AF_INET6) return false;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != L':') return false;
            port = authority.substr(close + 2);
            if (port.empty()) return false;
        }
    } else {
        auto colon = authority.find(L':');
        host = authority.substr(0, colon);
        if (colon != std::wstring::npos) {
            port = authority.substr(colon + 1);
            if (port.empty()) return false;
        }
        if (host.empty() || host.size() > 253) return false;
        size_t label = 0;
        for (wchar_t ch : host) {
            if (ch == L'.') { if (!label || label > 63) return false; label = 0; }
            else {
                if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-')) return false;
                ++label;
            }
        }
        if (label > 63 || host.front() == L'-' || host.back() == L'-') return false;
    }
    if (!port.empty()) {
        unsigned n;
        if (!decimal(utf8(port), 65535, n) || n == 0) return false;
    }
    output = value;
    return true;
}
std::wstring server_origin(const std::wstring& server) {
    std::wstring normalized;
    if (!normalize_server(server, normalized)) return {};
    auto end = normalized.find(L'/', 8);
    auto authority = normalized.substr(8, end == std::wstring::npos ? end : end - 8);
    size_t colon = authority.front() == L'[' ? authority.find(L':', authority.find(L']')) : authority.find(L':');
    auto host = authority.substr(0, colon);
    if (host.front() == L'[') {
        IN6_ADDR address{}; wchar_t canonical[INET6_ADDRSTRLEN]{};
        auto literal = host.substr(1, host.size() - 2);
        if (InetPtonW(AF_INET6, literal.c_str(), &address) != 1 || !InetNtopW(AF_INET6, &address, canonical, INET6_ADDRSTRLEN)) return {};
        host = L"[" + std::wstring(canonical) + L"]";
    }
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    unsigned port = 443;
    if (colon != std::wstring::npos && !decimal(utf8(authority.substr(colon + 1)), 65535, port)) return {};
    return L"https://" + host + L":" + std::to_wstring(port);
}
bool valid_pin(const std::string& pin) {
    if (pin.empty()) return true;
    // Require the complete SHA-256 public-key pin; never accept prefix matching or SHA-1.
    const std::string lead = "pin-sha256:";
    if (pin.compare(0, lead.size(), lead) != 0 || pin.size() != lead.size() + 44 || pin.back() != '=') return false;
    for (size_t i = lead.size(); i + 1 < pin.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(pin[i]);
        if (!std::isalnum(ch) && ch != '+' && ch != '/') return false;
    }
    return true;
}
bool parse_address(const std::string& input, SOCKADDR_INET& output) {
    output = {};
    if (input.empty() || input.size() > 64 || input.find_first_of(" \t\r\n%/[]") != std::string::npos) return false;
    if (InetPtonA(AF_INET, input.c_str(), &output.Ipv4.sin_addr) == 1) { output.si_family = AF_INET; return true; }
    output = {};
    if (InetPtonA(AF_INET6, input.c_str(), &output.Ipv6.sin6_addr) == 1) { output.si_family = AF_INET6; return true; }
    output = {};
    return false;
}
bool parse_prefix(const std::string& input, Prefix& output) {
    output = {};
    if (input.size() > 100) return false;
    auto slash = input.find('/');
    auto ip = input.substr(0, slash);
    if (!parse_address(ip, output.address)) return false;
    unsigned bits = output.address.si_family == AF_INET ? 32u : 128u;
    if (slash != std::string::npos) {
        auto mask = input.substr(slash + 1);
        if (output.address.si_family == AF_INET && mask.find('.') != std::string::npos) {
            SOCKADDR_INET addr{};
            if (!parse_address(mask, addr) || addr.si_family != AF_INET) return false;
            ULONG m = ntohl(addr.Ipv4.sin_addr.s_addr);
            bits = 0;
            bool zero = false;
            for (int b = 31; b >= 0; --b) {
                if ((m >> b) & 1u) { if (zero) return false; ++bits; }
                else zero = true;
            }
        } else if (!decimal(mask, bits, bits)) return false;
    }
    output.length = static_cast<UINT8>(bits);
    auto* bytes = output.address.si_family == AF_INET ? reinterpret_cast<BYTE*>(&output.address.Ipv4.sin_addr) : output.address.Ipv6.sin6_addr.u.Byte;
    const unsigned count = output.address.si_family == AF_INET ? 4u : 16u;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned offset = i * 8;
        if (offset >= bits) bytes[i] = 0;
        else if (offset + 8 > bits) bytes[i] &= static_cast<BYTE>(0xffu << (8 - (bits - offset)));
    }
    return true;
}
bool is_admin() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = nullptr;
    BOOL result = FALSE;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) {
        CheckTokenMembership(nullptr, sid, &result);
        FreeSid(sid);
    }
    return result != FALSE;
}
static std::wstring ini(const wchar_t* section, const wchar_t* key, const wchar_t* default_value, const std::wstring& file) {
    wchar_t value[4096]{};
    GetPrivateProfileStringW(section, key, default_value, value, 4096, file.c_str());
    return trim(value);
}
bool load_profile(Profile& profile, std::wstring& error, bool use_imported_profile) {
    std::wstring path = executable_dir() + L"\\BridgeVPN.ini";
    auto imported = use_imported_profile ? user_profile_path() : L"";
    if (!imported.empty() && GetFileAttributesW(imported.c_str()) != INVALID_FILE_ATTRIBUTES) path = imported;
    profile.server = ini(L"VPN", L"Server", L"", path);
    profile.pin = utf8(ini(L"VPN", L"ServerPin", L"", path));
    profile.ca_file = ini(L"VPN", L"CAFile", L"", path);
    if (!profile.ca_file.empty() && profile.ca_file.find(L':') == std::wstring::npos && profile.ca_file.front() != L'\\')
        profile.ca_file = path.substr(0,path.find_last_of(L'\\')) + L"\\" + profile.ca_file;
    profile.auth_group = utf8(ini(L"VPN", L"AuthGroup", L"", path));
    // Older imported profiles wrote LockServer=1. Addresses are always editable now.
    profile.remembered_pin = false;
    profile.prefer_udp = GetPrivateProfileIntW(L"VPN", L"PreferUDP", 1, path.c_str()) != 0;
    profile.protect_dns = GetPrivateProfileIntW(L"VPN", L"ProtectDNS", 1, path.c_str()) != 0;
    profile.block_ipv6 = GetPrivateProfileIntW(L"VPN", L"BlockUntunneledIPv6", 1, path.c_str()) != 0;
    profile.reconnect_seconds = static_cast<int>(GetPrivateProfileIntW(L"VPN", L"ReconnectSeconds", 300, path.c_str()));
    profile.reconnect_seconds = std::clamp(profile.reconnect_seconds, 0, 300);
    auto language = ini(L"UI", L"Language", L"auto", path);
    profile.language = language == L"en" || (language == L"auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_CHINESE)
        ? Language::English : Language::Chinese;
    if (!valid_pin(profile.pin)) { error = L"ServerPin must be a complete pin-sha256: value (44 Base64 characters)."; return false; }
    if (!profile.pin.empty() && !profile.ca_file.empty()) { error = L"Configure either ServerPin or CAFile, not both."; return false; }
    if (!profile.ca_file.empty() && GetFileAttributesW(profile.ca_file.c_str()) == INVALID_FILE_ATTRIBUTES) { error = L"CAFile does not exist."; return false; }
    return true;
}
const wchar_t* choose(Language language, const wchar_t* chinese, const wchar_t* english) { return language == Language::Chinese ? chinese : english; }
std::wstring connection_duration(uint64_t seconds) {
    wchar_t buffer[64]{};
    swprintf(buffer, 64, L"%02llu:%02u:%02u", static_cast<unsigned long long>(seconds / 3600),
             static_cast<unsigned>((seconds / 60) % 60), static_cast<unsigned>(seconds % 60));
    return buffer;
}
std::wstring state_text(State state, Language language) {
    switch (state) {
    case State::Idle: return choose(language, L"未连接", L"Disconnected");
    case State::Connecting: return choose(language, L"正在连接服务器…", L"Connecting to server…");
    case State::Authenticating: return choose(language, L"正在验证账号…", L"Authenticating…");
    case State::Configuring: return choose(language, L"正在配置安全连接…", L"Configuring the tunnel…");
    case State::Connected: return choose(language, L"已连接", L"Connected");
    case State::Reconnecting: return choose(language, L"连接中断，正在重连…", L"Connection interrupted. Reconnecting…");
    case State::Disconnecting: return choose(language, L"正在断开并恢复网络…", L"Disconnecting and restoring networking…");
    case State::Failed: return choose(language, L"连接失败", L"Connection failed");
    }
    return {};
}
std::wstring error_text(Error error, Language language) {
    switch (error) {
    case Error::None: return {};
    case Error::InvalidServer: return choose(language, L"请输入有效的 HTTPS 服务器地址。", L"Enter a valid HTTPS server address.");
    case Error::MissingCredentials: return choose(language, L"请输入账号和密码。", L"Enter your username and password.");
    case Error::MissingDriver: return choose(language, L"缺少同目录下的官方 wintun.dll，请完整解压程序包。", L"The official wintun.dll is missing. Extract the complete package.");
    case Error::Administrator: return choose(language, L"连接 VPN 需要管理员权限。", L"An administrator account is required to connect.");
    case Error::Certificate: return choose(language, L"服务器证书验证失败，请让管理员核对证书、地址与系统时间。", L"Server certificate verification failed. Ask your administrator to check the certificate, address, and system time.");
    case Error::Authentication: return choose(language, L"登录失败。请检查账号密码、账号状态及同时在线限制。", L"Login failed. Check credentials, account status, and the concurrent connection limit.");
    case Error::UnsupportedAuth: return choose(language, L"服务器要求额外认证；此版本支持 ocserv 账号密码认证。", L"The server requires additional authentication. This version supports ocserv username/password authentication.");
    case Error::GroupRequired: return choose(language, L"服务器提供多个登录组，请让管理员设置 AuthGroup。", L"The server offers multiple groups. Ask your administrator to set AuthGroup.");
    case Error::Network: return choose(language, L"无法建立或维持连接，请检查服务器、端口与网络。", L"The connection could not be established or maintained. Check the server, port, and network.");
    case Error::Tunnel: return choose(language, L"虚拟网卡、路由或 DNS 配置失败，已回滚本次更改。", L"Adapter, route, or DNS configuration failed. This connection's changes have been rolled back.");
    case Error::Cancelled: return choose(language, L"连接已取消。", L"Connection cancelled.");
    case Error::Internal: return choose(language, L"程序无法完成操作，请查看诊断信息。", L"The operation could not be completed. Review the diagnostic details.");
    }
    return {};
}
void erase(std::string& value) { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); value.clear(); }
void erase(std::wstring& value) { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); value.clear(); }
std::string redacted(std::string text, const std::string& user, const std::string& password) {
    auto lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    for (const char* secret : {"cookie:", "set-cookie", "webvpn=", "webvpncontext=", "authorization:", "password="})
        if (lower.find(secret) != std::string::npos) return "[sensitive protocol detail omitted]";
    for (const auto* secret : {&password, &user}) {
        if (secret->empty()) continue;
        size_t pos = 0;
        while ((pos = text.find(*secret, pos)) != std::string::npos) { text.replace(pos, secret->size(), "[redacted]"); pos += 10; }
    }
    if (text.size() > 2000) text.resize(2000);
    for (char& c : text) if (static_cast<unsigned char>(c) < 32 && c != '\n' && c != '\r' && c != '\t') c = ' ';
    return text;
}
}
