#include "hotp_counter.h"
#include <algorithm>
#include <charconv>
#include <climits>
#include <cctype>

namespace vpn {
bool next_hotp_counter(const std::string &token, std::string &next) {
    next.clear();
    auto starts = [&](const char *prefix) {
        size_t at = 0;
        for (; prefix[at]; ++at)
            if (at == token.size() || std::tolower(static_cast<unsigned char>(token[at])) != prefix[at])
                return false;
        return true;
    };
    size_t begin = std::string::npos, end = token.size();
    if (starts("otpauth://hotp/")) {
        const auto query = token.find('?');
        if (query == std::string::npos)
            return false;
        for (size_t at = query + 1; at < token.size();) {
            const auto stop = token.find('&', at);
            const auto last = stop == std::string::npos ? token.size() : stop;
            if (token.compare(at, 8, "counter=") == 0) {
                if (begin != std::string::npos)
                    return false;
                begin = at + 8;
                end = last;
            }
            if (stop == std::string::npos)
                break;
            at = stop + 1;
        }
    } else if (starts("base32:") || starts("0x")) {
        const auto comma = token.rfind(',');
        if (comma != std::string::npos)
            begin = comma + 1;
    }
    if (begin == std::string::npos || begin >= end ||
        !std::all_of(token.begin() + begin, token.begin() + end, [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    unsigned counter = 0;
    const auto parsed = std::from_chars(token.data() + begin, token.data() + end, counter);
    if (parsed.ec != std::errc() || parsed.ptr != token.data() + end || counter >= INT_MAX)
        return false;
    next = token.substr(0, begin) + std::to_string(counter + 1) + token.substr(end);
    return true;
}
} // namespace vpn
