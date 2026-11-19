#pragma once
#include <string>

namespace vpn {
// OpenConnect's HOTP counter is signed 32-bit. Never wrap or infer a counter.
bool next_hotp_counter(const std::string &token, std::string &next);
} // namespace vpn
