#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <cstdint>
#include <string>
#include <vector>

namespace bridge {
constexpr wchar_t kProduct[] = L"布利杰VPN";
constexpr wchar_t kVersion[] = L"0.3.0";
enum class Language { Chinese, English };
enum class State { Idle, Connecting, Authenticating, Configuring, Connected, Reconnecting, Disconnecting, Failed };
enum class Error { None, InvalidServer, MissingCredentials, MissingDriver, Administrator, Certificate,
                   Authentication, UnsupportedAuth, Network, Tunnel, Cancelled, Internal, GroupRequired };

struct Profile {
    std::wstring server;
    std::string pin;
    std::wstring ca_file;
    std::string auth_group;
    bool lock_server = false;
    bool prefer_udp = true;
    bool protect_dns = true;
    bool block_ipv6 = true;
    int reconnect_seconds = 300;
    Language language = Language::Chinese;
};

class Handle {
public:
    explicit Handle(HANDLE h = nullptr) : value_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE h = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = h;
    }
private:
    HANDLE value_;
};

struct Prefix {
    SOCKADDR_INET address{};
    UINT8 length = 0;
};

std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
std::wstring executable_path();
std::wstring executable_dir();
std::wstring settings_path();
std::wstring system_error(DWORD code);
std::wstring trim(const std::wstring& value);
bool normalize_server(const std::wstring& input, std::wstring& output);
bool valid_pin(const std::string& pin);
bool parse_address(const std::string& input, SOCKADDR_INET& output);
bool parse_prefix(const std::string& input, Prefix& output);
bool is_admin();
bool load_profile(Profile& profile, std::wstring& error, bool use_imported_profile = true);
std::wstring state_text(State state, Language language);
std::wstring error_text(Error error, Language language);
std::wstring connection_duration(uint64_t seconds);
const wchar_t* choose(Language language, const wchar_t* chinese, const wchar_t* english);
void erase(std::string& value);
void erase(std::wstring& value);
std::string redacted(std::string text, const std::string& user, const std::string& password);
}
