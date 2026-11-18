#include "profile_settings.h"
#include "../client/connection_file.h"
#include <cassert>
#include <iostream>
#include <sys/socket.h>

int main() {
    auto *original = lansyra::profile_candidate(nullptr, "Before", "https://vpn.example.com", "fixture", "");
    auto *ip = nm_connection_get_setting_ip4_config(original);
    g_object_set(ip, NM_SETTING_IP_CONFIG_NEVER_DEFAULT, TRUE, NM_SETTING_IP_CONFIG_DNS_PRIORITY, -100, nullptr);
    nm_setting_ip_config_add_dns(ip, "10.77.0.1");
    auto *route = nm_ip_route_new(AF_INET, "192.168.19.0", 24, nullptr, 42, nullptr);
    assert(route && nm_setting_ip_config_add_route(ip, route));
    nm_ip_route_unref(route);
    auto *vpn = nm_connection_get_setting_vpn(original);
    nm_setting_vpn_add_data_item(vpn, "proxy", "http://proxy.example.com:8080");
    nm_setting_vpn_add_secret(vpn, "cookie", "ephemeral");
    auto *candidate = lansyra::profile_candidate(original, "After", "https://vpn.example.com", "fixture", "");
    auto *updated = nm_connection_get_setting_ip4_config(candidate);
    assert(nm_setting_ip_config_get_never_default(updated));
    assert(nm_setting_ip_config_get_dns_priority(updated) == -100);
    assert(nm_setting_ip_config_get_num_routes(updated) == 1 && nm_setting_ip_config_get_num_dns(updated) == 1);
    assert(g_strcmp0(nm_setting_vpn_get_data_item(nm_connection_get_setting_vpn(candidate), "proxy"), "http://proxy.example.com:8080") == 0);
    assert(!nm_setting_vpn_get_secret(nm_connection_get_setting_vpn(candidate), "cookie"));
    assert(g_strcmp0(nm_connection_get_id(original), "Before") == 0 && nm_setting_vpn_get_secret(vpn, "cookie"));
    assert(g_strcmp0(nm_connection_get_uuid(original), nm_connection_get_uuid(candidate)) == 0);
    g_object_unref(candidate); g_object_unref(original);
    auto publicProfile = lansyra::ConnectionFile::parse("[VPN]\nVersion=1\nServer=https://vpn.example.com\nTrust=prompt\n");
    assert(publicProfile.server == "https://vpn.example.com");
    std::cout << "Linux editing preserves routes, DNS, proxy and identity without mutating originals\n";
}
