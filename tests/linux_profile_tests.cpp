#include "profile_settings.h"
#include "../client/connection_file.h"
#include <cassert>
#include <iostream>
#include <sys/socket.h>
#include <glib/gstdio.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    g_autofree char *temporary = g_dir_make_tmp("lansyra-profile-tests-XXXXXX", nullptr);
    assert(temporary && g_setenv("XDG_CONFIG_HOME", temporary, TRUE));
    const std::string corpus = std::string(argv[1]) + "/";
    for (const char *name : {"pinned", "prompt", "ambiguous"}) {
        const auto path = corpus + name + ".vpn.fixture";
        assert(g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR));
        bool accepted = false;
        try {
            delete lansyra::read_public_profile(path);
            accepted = true;
        } catch (const std::runtime_error &) {
        }
        assert(accepted == (std::string(name) == "prompt"));
    }
    auto *original = lansyra::profile_candidate(nullptr, "Before", "https://vpn.example.com", "fixture", "");
    auto *ip = nm_connection_get_setting_ip4_config(original);
    g_object_set(ip, NM_SETTING_IP_CONFIG_NEVER_DEFAULT, TRUE, NM_SETTING_IP_CONFIG_DNS_PRIORITY, -100,
                 nullptr);
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
    assert(nm_setting_ip_config_get_num_routes(updated) == 1 &&
           nm_setting_ip_config_get_num_dns(updated) == 1);
    assert(g_strcmp0(nm_setting_vpn_get_data_item(nm_connection_get_setting_vpn(candidate), "proxy"),
                     "http://proxy.example.com:8080") == 0);
    assert(!nm_setting_vpn_get_secret(nm_connection_get_setting_vpn(candidate), "cookie"));
    assert(g_strcmp0(nm_connection_get_id(original), "Before") == 0 &&
           nm_setting_vpn_get_secret(vpn, "cookie"));
    assert(g_strcmp0(nm_connection_get_uuid(original), nm_connection_get_uuid(candidate)) == 0);
    auto *strict =
        lansyra::profile_candidate(candidate, "CA", "https://vpn.example.com", "", "/tmp/test-ca.pem");
    auto *strictVpn = nm_connection_get_setting_vpn(strict);
    assert(g_strcmp0(nm_setting_vpn_get_data_item(strictVpn, "prevent_invalid_cert"), "yes") == 0);
    auto *prompt = lansyra::profile_candidate(strict, "Prompt", "https://vpn.example.com", "", "");
    auto *promptVpn = nm_connection_get_setting_vpn(prompt);
    assert(!nm_setting_vpn_get_data_item(promptVpn, "cacert"));
    assert(!nm_setting_vpn_get_data_item(promptVpn, "prevent_invalid_cert"));
    assert(nm_setting_vpn_get_data_item(strictVpn, "cacert"));
    const std::string directory = std::string(temporary) + "/lansyra/certificates";
    assert(g_mkdir_with_parents(directory.c_str(), 0700) == 0);
    const auto oldCA = directory + "/00000000-0000-4000-8000-000000000001.pem";
    const auto newCA = directory + "/00000000-0000-4000-8000-000000000002.pem";
    const auto manualCA = directory + "/manual.pem";
    for (const auto &path : {oldCA, newCA, manualCA})
        assert(g_file_set_contents(path.c_str(), "fixture", -1, nullptr));
    nm_setting_vpn_add_data_item(vpn, "cacert", oldCA.c_str());
    auto *other = lansyra::profile_candidate(nullptr, "Shared CA", "https://vpn.example.com", "", oldCA);
    auto *connections = g_ptr_array_new();
    g_ptr_array_add(connections, original);
    g_ptr_array_add(connections, other);
    const std::string id = nm_connection_get_uuid(original);
    lansyra::prune_profile_certificates(connections, id, newCA);
    assert(g_file_test(oldCA.c_str(), G_FILE_TEST_EXISTS) && g_file_test(newCA.c_str(), G_FILE_TEST_EXISTS));
    g_ptr_array_remove(connections, other);
    lansyra::prune_profile_certificates(connections, id, newCA);
    assert(!g_file_test(oldCA.c_str(), G_FILE_TEST_EXISTS) && g_file_test(newCA.c_str(), G_FILE_TEST_EXISTS));
    lansyra::prune_profile_certificates(connections, id);
    assert(!g_file_test(newCA.c_str(), G_FILE_TEST_EXISTS) &&
           g_file_test(manualCA.c_str(), G_FILE_TEST_EXISTS));
    g_ptr_array_unref(connections);
    g_object_unref(other);
    assert(g_unlink(manualCA.c_str()) == 0 && g_rmdir(directory.c_str()) == 0);
    assert(g_rmdir((std::string(temporary) + "/lansyra").c_str()) == 0 && g_rmdir(temporary) == 0);
    g_object_unref(prompt);
    g_object_unref(strict);
    g_object_unref(candidate);
    g_object_unref(original);
    auto publicProfile =
        lansyra::ConnectionFile::parse("[VPN]\nVersion=1\nServer=https://vpn.example.com\nTrust=prompt\n");
    assert(publicProfile.server == "https://vpn.example.com");
    std::cout << "Linux editing preserves routes, DNS, proxy and identity without mutating originals\n";
}
