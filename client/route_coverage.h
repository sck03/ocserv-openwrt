#pragma once
#include <string>
#include <vector>

namespace vpn {
// OpenConnect split-includes may contain both address families. No IPv4
// entries means a default IPv4 route; IPv6 entries are handled separately.
enum class Ipv4Coverage { Invalid, Split, Full };
Ipv4Coverage ipv4_coverage(const std::vector<std::string> &routes);
}
