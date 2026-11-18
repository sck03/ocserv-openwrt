#include "system_proxy.h"
#include <stdexcept>
#include <iostream>

int main() {
    using vpn::proxy_bypasses;
    if (!proxy_bypasses(L"router", 4443, L"<local>")) throw std::runtime_error("local hostname");
    if (proxy_bypasses(L"vpn.example.com", 4443, L"<local>")) throw std::runtime_error("remote hostname");
    if (!proxy_bypasses(L"192.168.19.253", 4443, L"<local>;192.168.*")) throw std::runtime_error("LAN wildcard");
    if (!proxy_bypasses(L"VPN.Example.COM", 4443, L"*.example.com:4443")) throw std::runtime_error("port pattern");
    if (proxy_bypasses(L"vpn.example.com", 443, L"*.example.com:4443")) throw std::runtime_error("wrong port");
    if (!proxy_bypasses(L"2001:db8::1", 443, L"[2001:db8::1]")) throw std::runtime_error("IPv6");
    if (proxy_bypasses(L"evil-example.com", 443, L"*.example.com")) throw std::runtime_error("domain boundary");
    std::cout << "7 system proxy checks passed\n";
}
