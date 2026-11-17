#include "route_coverage.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>

int main() {
    const std::vector<std::vector<std::string>> full = {
        {}, {"0.0.0.0/0"}, {"0.0.0.0/0.0.0.0"},
        {"0.0.0.0/2", "64.0.0.0/2", "128.0.0.0/2", "192.0.0.0/2"},
        {"192.0.0.0/2", "0.0.0.0/1", "128.0.0.0/2", "10.0.0.0/8"},
        {"0.0.0.0/128.0.0.0", "128.0.0.0/128.0.0.0"}};
    const std::vector<std::vector<std::string>> split = {
        {"0.0.0.0/1"}, {"10.0.0.0/8"},
        {"0.0.0.0/2", "128.0.0.0/2", "192.0.0.0/2"},
        {"0.0.0.0/0", "invalid"}, {"0.0.0.0/255.0.255.0"},
        {"0.0.0.0/33"}, {"256.0.0.0/0"}, {"0.0.0.0/-1"}};
    for (const auto &routes : full)
        if (!vpn::ipv4_full_tunnel(routes)) throw std::runtime_error("complete cover rejected");
    for (const auto &routes : split)
        if (vpn::ipv4_full_tunnel(routes)) throw std::runtime_error("incomplete/invalid cover accepted");
    // Exercise arbitrary equivalent partitions and one missing final address.
    for (unsigned bits = 1; bits <= 12; ++bits) {
        std::vector<std::string> routes;
        for (uint64_t i = 0; i < (uint64_t{1} << bits); ++i) {
            const uint64_t ip = i << (32 - bits);
            routes.push_back(std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." +
                             std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255) + "/" + std::to_string(bits));
        }
        if (!vpn::ipv4_full_tunnel(routes)) throw std::runtime_error("partition rejected");
        routes.pop_back();
        if (vpn::ipv4_full_tunnel(routes)) throw std::runtime_error("gap accepted");
    }
    std::cout << "38 route coverage checks passed\n";
}
