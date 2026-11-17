#include "route_coverage.h"
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>

namespace vpn {
namespace {
bool decimal(std::string_view text, uint32_t maximum, uint32_t &value) {
    if (text.empty()) return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || value > maximum / 10) return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > maximum) return false;
    }
    return true;
}
bool address(std::string_view text, uint32_t &value) {
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        auto end = text.find('.');
        if ((i == 3) != (end == std::string_view::npos)) return false;
        uint32_t octet;
        if (!decimal(text.substr(0, end), 255, octet)) return false;
        value = (value << 8) | octet;
        if (i != 3) text.remove_prefix(end + 1);
    }
    return true;
}
bool interval(const std::string &route, std::pair<uint64_t, uint64_t> &result) {
    std::string_view text(route);
    auto slash = text.find('/');
    if (slash == std::string_view::npos) return false;
    uint32_t ip, mask;
    if (!address(text.substr(0, slash), ip)) return false;
    auto suffix = text.substr(slash + 1);
    if (suffix.find('.') != std::string_view::npos) {
        if (!address(suffix, mask)) return false;
        const uint64_t inverse = static_cast<uint32_t>(~mask);
        if (inverse & (inverse + 1)) return false;
    } else {
        uint32_t bits;
        if (!decimal(suffix, 32, bits)) return false;
        mask = bits == 0 ? 0 : UINT32_MAX << (32 - bits);
    }
    result.first = ip & mask;
    result.second = result.first + static_cast<uint64_t>(static_cast<uint32_t>(~mask)) + 1;
    return true;
}
}
bool ipv4_full_tunnel(const std::vector<std::string> &routes) {
    if (routes.empty()) return true;
    if (routes.size() > 4096) return false;
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    ranges.reserve(routes.size());
    for (const auto &route : routes) {
        std::pair<uint64_t, uint64_t> range;
        if (!interval(route, range)) return false;
        ranges.push_back(range);
    }
    std::sort(ranges.begin(), ranges.end());
    uint64_t covered = 0;
    for (const auto &range : ranges) {
        if (range.first > covered) return false;
        covered = std::max(covered, range.second);
    }
    return covered == (uint64_t{1} << 32);
}
}
