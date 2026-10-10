#include "diagnostics.h"
#include "vendor/json.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>

namespace vpn {
const char *state_name(State state) {
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Connecting:
        return "connecting";
    case State::Authenticating:
        return "authenticating";
    case State::Configuring:
        return "configuring";
    case State::Connected:
        return "connected";
    case State::Reconnecting:
        return "reconnecting";
    case State::Suspended:
        return "suspended";
    case State::RetryWait:
        return "retry_wait";
    case State::Disconnecting:
        return "disconnecting";
    case State::Failed:
        return "failed";
    }
    return "unknown";
}
const char *error_name(ErrorCategory error) {
    switch (error) {
    case ErrorCategory::None:
        return "none";
    case ErrorCategory::Canceled:
        return "canceled";
    case ErrorCategory::Network:
        return "network";
    case ErrorCategory::Timeout:
        return "timeout";
    case ErrorCategory::Authentication:
        return "authentication";
    case ErrorCategory::Certificate:
        return "certificate";
    case ErrorCategory::Configuration:
        return "configuration";
    case ErrorCategory::Permission:
        return "permission";
    case ErrorCategory::Adapter:
        return "adapter";
    case ErrorCategory::Busy:
        return "busy";
    case ErrorCategory::Server:
        return "server";
    case ErrorCategory::Internal:
        return "internal";
    }
    return "unknown";
}
std::wstring error_text(ErrorCategory error, Language language) {
    switch (error) {
    case ErrorCategory::None:
        return {};
    case ErrorCategory::Canceled:
        return tr(language, L"连接已取消", L"Connection canceled");
    case ErrorCategory::Network:
        return tr(language, L"网络不可达，请检查网络和服务器地址",
                  L"Network unavailable; check the network and gateway");
    case ErrorCategory::Timeout:
        return tr(language, L"连接超时，请检查网络和服务器",
                  L"Connection timed out; check the network and server");
    case ErrorCategory::Authentication:
        return tr(language, L"认证失败，请检查账号或重新登录",
                  L"Authentication failed; check credentials or sign in again");
    case ErrorCategory::Certificate:
        return tr(language, L"证书验证失败，请核对证书和系统时间",
                  L"Certificate verification failed; check the certificate and clock");
    case ErrorCategory::Configuration:
        return tr(language, L"连接配置无效，请检查配置和便携包文件",
                  L"Invalid configuration; check the profile and portable package");
    case ErrorCategory::Permission:
        return tr(language, L"需要管理员权限才能连接", L"Administrator privileges are required");
    case ErrorCategory::Adapter:
        return tr(language, L"VPN 网络配置失败，请查看日志", L"VPN network setup failed; see the log");
    case ErrorCategory::Busy:
        return tr(language, L"已有会话正在使用此连接", L"This connection is already in use");
    case ErrorCategory::Server:
        return tr(language, L"服务器已结束会话，请重新连接", L"The server ended the session; connect again");
    case ErrorCategory::Internal:
        return tr(language, L"客户端处理失败，请导出诊断信息",
                  L"Client processing failed; export diagnostics");
    }
    return {};
}
ErrorCategory connection_error(int code, ErrorCategory fallback) {
    switch (code) {
    case -ETIMEDOUT:
        return ErrorCategory::Timeout;
    case -EACCES:
    case -EPERM:
        return ErrorCategory::Authentication;
    case -ENOMEM:
        return ErrorCategory::Internal;
    case -EINVAL:
        return ErrorCategory::Configuration;
    default:
        return fallback;
    }
}
namespace {
constexpr char Hidden[] = "[authentication data redacted]";
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::string url_encode(const std::string &value, bool form) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            result += static_cast<char>(c);
        else if (form && c == ' ')
            result += '+';
        else {
            result += '%';
            result += digits[c >> 4];
            result += digits[c & 15];
        }
    }
    return result;
}
bool authentication_data(const std::string &message) {
    auto probe = message;
    auto hex = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        c = static_cast<unsigned char>(std::tolower(c));
        return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    // Encoded field names must not evade structural filtering.
    for (unsigned pass = 0; pass < 2; ++pass) {
        std::string decoded;
        for (size_t i = 0; i < probe.size(); ++i) {
            size_t digits =
                probe[i] == '%' ? i + 1 : (probe.compare(i, 4, "\\u00") == 0 ? i + 4 : probe.size());
            if (digits + 1 < probe.size() && hex(probe[digits]) >= 0 && hex(probe[digits + 1]) >= 0) {
                decoded += static_cast<char>(hex(probe[digits]) * 16 + hex(probe[digits + 1]));
                i = digits + 1;
            } else
                decoded += probe[i];
        }
        erase(probe);
        probe = std::move(decoded);
    }
    std::string compact;
    for (unsigned char c : probe)
        if (!std::isspace(c) && c != '\'' && c != '"' && c != '-' && c != '_')
            compact += static_cast<char>(std::tolower(c));
    for (const char *key :
         {"password",  "passwd",        "credential",   "passcode",    "cookie",     "webvpn",   "token",
          "secret",    "authorization", "samlresponse", "samlrequest", "relaystate", "username", "authgroup",
          "grouplist", "otp",           "challenge",    "sessionid",   "apikey",     "code"}) {
        for (const char *delimiter : {":", "="})
            if (compact.find(std::string(key) + delimiter) != std::string::npos)
                return true;
        if (compact.find(std::string("<") + key) != std::string::npos)
            return true;
    }
    for (const char *marker : {"<configauth", "<auth", "<form", "<input", "<select", "<option",
                               "beginprivatekey", "beginrsaprivatekey", "beginecprivatekey"})
        if (compact.find(marker) != std::string::npos)
            return true;
    return false;
}
} // namespace
Redactor::~Redactor() {
    clear();
}
void Redactor::clear() {
    for (auto &value : values_)
        erase(value);
    values_.clear();
    characters_ = 0;
    saturated_ = false;
}
void Redactor::remember(const std::string &value) {
    if (value.empty() || saturated_)
        return;
    std::vector<std::string> variants{value, url_encode(value, false), url_encode(value, true),
                                      base64(value)};
    std::string xml;
    for (char c : value) {
        switch (c) {
        case '&':
            xml += "&amp;";
            break;
        case '<':
            xml += "&lt;";
            break;
        case '>':
            xml += "&gt;";
            break;
        case '"':
            xml += "&quot;";
            break;
        case '\'':
            xml += "&apos;";
            break;
        default:
            xml += c;
        }
    }
    variants.push_back(std::move(xml));
    auto json = nlohmann::json(value).dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
    variants.push_back(json.substr(1, json.size() - 2));
    erase(json);
    for (auto &variant : variants) {
        variant = lower(std::move(variant));
        if (std::find(values_.begin(), values_.end(), variant) == values_.end()) {
            // Fail closed when a hostile server supplies unbounded distinct fields.
            if (values_.size() >= 1024 || characters_ + variant.size() > 256 * 1024)
                saturated_ = true;
            else {
                characters_ += variant.size();
                values_.push_back(variant);
            }
        }
        erase(variant);
    }
    std::sort(values_.begin(), values_.end(),
              [](const auto &a, const auto &b) { return a.size() > b.size(); });
}
std::string Redactor::clean(std::string message) const {
    if (saturated_ || authentication_data(message))
        return Hidden;
    // URLs can carry credentials in user-info, paths, query strings and fragments.
    // Keep only the scheme and authority, and never export user-info.
    for (size_t offset = 0; (offset = message.find("://", offset)) != std::string::npos;) {
        size_t authority = offset + 3;
        size_t end = message.find_first_of(" \t\r\n\"'<>)", authority);
        if (end == std::string::npos)
            end = message.size();
        size_t suffix = message.find_first_of("/?#", authority);
        size_t authority_end = std::min(end, suffix);
        if (message.find('@', authority) < authority_end)
            return Hidden;
        if (suffix < end) {
            message.replace(suffix, end - suffix, "/[redacted]");
            offset = suffix + 11;
        } else
            offset = end;
    }
    auto folded = lower(message);
    for (const auto &value : values_) {
        // Replacing from the end avoids matching replacement text again.
        size_t offset = message.size();
        while (offset && (offset = folded.rfind(value, offset - 1)) != std::string::npos) {
            message.replace(offset, value.size(), "[redacted]");
            folded.replace(offset, value.size(), "[redacted]");
        }
    }
    for (char &c : message)
        if (static_cast<unsigned char>(c) < 32 && c != '\n' && c != '\r' && c != '\t')
            c = ' ';
    return message;
}
std::string diagnostic_report(const SessionStatus &status, const Statistics &statistics,
                              std::wstring_view log, uint64_t dropped_logs) {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char timestamp[32]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02uZ", now.wYear, now.wMonth,
                  now.wDay, now.wHour, now.wMinute, now.wSecond);
    // Explicit allowlist: never serialize Profile, Prompt, environment or raw core data.
    nlohmann::json report = {
        {"schema", 1},
        {"product", "Linkora VPN"},
        {"version", Version},
        {"created_utc", timestamp},
        {"architecture", sizeof(void *) == 8 ? "x64" : "x86"},
        {"session",
         {{"id", status.session_id},
          {"generation", status.generation},
          {"state", state_name(status.state)},
          {"error_category", error_name(status.error)},
          {"error_code", status.error_code},
          {"attempt", status.attempt},
          {"retry_seconds", status.retry_seconds},
          {"elapsed_ms", status.elapsed_ms},
          {"terminal", status.terminal}}},
        {"traffic", {{"downloaded_bytes", statistics.downloaded}, {"uploaded_bytes", statistics.uploaded}}},
        {"dropped_log_entries", dropped_logs},
        {"log", Redactor{}.clean(utf8(std::wstring(log)))}};
    return report.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + '\n';
}
} // namespace vpn
