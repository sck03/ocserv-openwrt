#include "network_policy.h"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2 || std::string(argv[1]) != "--allow-network-changes" || !vpn::administrator())
        return 2;
    vpn::Ipv6Guard policy;
    std::wstring error;
    if (!policy.require(true, error) || !policy.require(true, error)) {
        std::cerr << vpn::utf8(error) << '\n';
        return 1;
    }
    policy.reset();
    if (!policy.require(false, error) || !policy.require(true, error))
        return 1;
    policy.reset();
    std::cout << "{\"passed\":4,\"boundary\":\"Dynamic IPv6 WFP filters on an isolated administrator runner; no persistent rules\"}\n";
    return 0;
}
