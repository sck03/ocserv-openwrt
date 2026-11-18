#pragma once
#include <string>

namespace vpn {
bool proxy_bypasses(const std::wstring &host, unsigned port, const std::wstring &exceptions);
std::string system_proxy(const std::wstring &url);
}
