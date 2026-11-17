#pragma once
#include <string>
#include <vector>

namespace vpn {
// Empty split-includes means the server selected a default route.
bool ipv4_full_tunnel(const std::vector<std::string> &routes);
}
