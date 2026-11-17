#pragma once
#include "platform.h"
#include "route_coverage.h"

namespace vpn {
// Dynamic WFP policy belongs to the worker's session. Closing the engine (also
// on process termination) removes it without changing persistent firewall rules.
class Ipv6Guard {
public:
    ~Ipv6Guard();
    Ipv6Guard() = default;
    Ipv6Guard(const Ipv6Guard &) = delete;
    Ipv6Guard &operator=(const Ipv6Guard &) = delete;
    bool require(bool enabled, std::wstring &error);
    void reset();

private:
    HANDLE engine_ = nullptr;
};
} // namespace vpn
