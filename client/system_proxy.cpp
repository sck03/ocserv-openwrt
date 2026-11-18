#include "system_proxy.h"
#include "platform.h"
#include <winhttp.h>
#include <cwctype>

namespace vpn {
namespace {
std::wstring lower(std::wstring value) {
    for (auto &c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}
bool matches(const std::wstring &value, const std::wstring &pattern) {
    size_t input = 0, rule = 0, star = std::wstring::npos, retry = 0;
    while (input < value.size()) {
        if (rule < pattern.size() && (pattern[rule] == L'?' || pattern[rule] == value[input])) { ++input; ++rule; }
        else if (rule < pattern.size() && pattern[rule] == L'*') { star = rule++; retry = input; }
        else if (star != std::wstring::npos) { rule = star + 1; input = ++retry; }
        else return false;
    }
    while (rule < pattern.size() && pattern[rule] == L'*') ++rule;
    return rule == pattern.size();
}
}
bool proxy_bypasses(const std::wstring &hostname, unsigned port, const std::wstring &exceptions) {
    auto host = lower(hostname);
    if (host.size() > 2 && host.front() == L'[' && host.back() == L']') host = host.substr(1, host.size() - 2);
    const auto endpoint = (host.find(L':') == std::wstring::npos ? host : L"[" + host + L"]") + L":" + std::to_wstring(port);
    for (size_t first = 0; first < exceptions.size();) {
        const auto last = exceptions.find_first_of(L"; \t\r\n", first);
        auto rule = lower(exceptions.substr(first, last == std::wstring::npos ? last : last - first));
        if (rule == L"<local>") {
            if (host.find_first_of(L".:") == std::wstring::npos) return true;
        } else if (!rule.empty()) {
            if (rule.rfind(L"https://", 0) == 0) rule.erase(0, 8);
            if (matches(host, rule) || matches(endpoint, rule) || matches(L"[" + host + L"]", rule)) return true;
        }
        if (last == std::wstring::npos) break;
        first = last + 1;
    }
    return false;
}

std::string system_proxy(const std::wstring &url) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config)) return {};
    struct Release {
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG &config;
        ~Release() { GlobalFree(config.lpszProxy); GlobalFree(config.lpszProxyBypass); GlobalFree(config.lpszAutoConfigUrl); }
    } release{config};
    std::wstring selected = config.lpszProxy ? config.lpszProxy : L"";
    std::wstring exceptions = config.lpszProxyBypass ? config.lpszProxyBypass : L"";
    if (config.fAutoDetect || config.lpszAutoConfigUrl) {
        HINTERNET session = WinHttpOpen(L"Lansyra", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
        if (session) {
            WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
            WINHTTP_AUTOPROXY_OPTIONS options{};
            options.dwFlags = config.lpszAutoConfigUrl ? WINHTTP_AUTOPROXY_CONFIG_URL : WINHTTP_AUTOPROXY_AUTO_DETECT;
            options.lpszAutoConfigUrl = config.lpszAutoConfigUrl;
            options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            WINHTTP_PROXY_INFO proxy{};
            if (WinHttpGetProxyForUrl(session, url.c_str(), &options, &proxy)) {
                selected = proxy.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && proxy.lpszProxy ? proxy.lpszProxy : L"";
                exceptions = proxy.lpszProxyBypass ? proxy.lpszProxyBypass : L"";
                GlobalFree(proxy.lpszProxy); GlobalFree(proxy.lpszProxyBypass);
            }
            WinHttpCloseHandle(session);
        }
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    if (WinHttpCrackUrl(url.c_str(), 0, 0, &parts) &&
        proxy_bypasses(std::wstring(parts.lpszHostName, parts.dwHostNameLength), parts.nPort, exceptions)) return {};
    std::wstring fallback;
    for (size_t first = 0; first < selected.size();) {
        const auto last = selected.find(L';', first);
        const auto item = trim(selected.substr(first, last == std::wstring::npos ? last : last - first));
        if (item.rfind(L"https=", 0) == 0) { fallback = item.substr(6); break; }
        if (item.rfind(L"http=", 0) == 0) fallback = item.substr(5);
        else if (item.find(L'=') == std::wstring::npos && fallback.empty()) fallback = item;
        if (last == std::wstring::npos) break;
        first = last + 1;
    }
    if (!fallback.empty() && fallback.find(L"://") == std::wstring::npos) fallback = L"http://" + fallback;
    return utf8(fallback);
}
}
