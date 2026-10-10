#include "profile.h"
#include "vendor/json.hpp"
#include <iostream>
#include <stdexcept>

using namespace vpn;
namespace {
int passed = 0;
void check(bool result, const char *message) {
    if (!result)
        throw std::runtime_error(message);
    ++passed;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "A dedicated fixture directory is required\n";
        return 2;
    }
    const auto root = std::filesystem::absolute(argv[1]) / wide(random_id());
    std::filesystem::create_directories(root);
    try {
        std::wstring error, normalized, origin;
        check(normalize_gateway(L"192.168.19.253:4443", normalized, &origin) &&
                  normalized == L"https://192.168.19.253:4443",
              "plain gateway normalization");
        check(normalize_gateway(L"https://vpn.example.com/group", normalized, &origin) &&
                  origin == L"https://vpn.example.com:443",
              "gateway group path");
        check(!normalize_gateway(L"http://vpn.example.com", normalized) &&
                  !normalize_gateway(L"https://user:pass@vpn.example.com", normalized),
              "insecure gateway rejection");
        for (const auto &entry : std::vector<std::pair<std::wstring, std::wstring>>{
                 {L"VPN.Example.COM:4443", L"https://vpn.example.com:4443"},
                 {L"https://vpn.example.com:443", L"https://vpn.example.com"},
                 {L"例子.测试:4443", L"https://xn--fsqu00a.xn--0zwm56d:4443"},
                 {L"[2001:db8::1]:4443", L"https://[2001:db8::1]:4443"},
                 {L"https://[::1]/group", L"https://[::1]/group"},
                 {L"vpn_host.lan", L"https://vpn_host.lan"},
                 {L"vpn.example.com.", L"https://vpn.example.com."},
                 {L"vpn.example.com/group?key=value", L"https://vpn.example.com/group?key=value"}}) {
            check(normalize_gateway(entry.first, normalized) && normalized == entry.second,
                  "domain, IDN and IPv6 gateway normalization");
        }
        for (const auto &invalid : std::vector<std::wstring>{
                 L"vpn..example.com", L".vpn.example.com", L"-vpn.example.com", L"vpn-.example.com",
                 L"999.1.1.1", L"192.168.01.1", L"vpn.example.com:", L"vpn.example.com:65536",
                 L"https://@vpn.example.com", L"https://[2001:db8:::1]", L"https://[fe80::1%12]",
                 std::wstring(64, L'a') + L".example.com"}) {
            check(!normalize_gateway(invalid, normalized), "malformed gateway rejected before connection");
        }
        check(wide(utf8(L"中文配置")) == L"中文配置", "Unicode conversion");
        std::string encoded, decoded;
        check(protect_secret("Synthetic fixture password", "fixture-A", encoded, error) &&
                  unprotect_secret(encoded, "fixture-A", decoded) && decoded == "Synthetic fixture password",
              "DPAPI round trip");
        check(!unprotect_secret(encoded, "fixture-B", decoded), "DPAPI context isolation");
        ProfileStore store(root / L"中文便携数据");
        std::vector<Profile> profiles;
        Preferences preferences;
        check(store.load(profiles, preferences, error) && profiles.empty() &&
                  preferences.language == Language::Chinese && !preferences.retry_failed &&
                  preferences.auto_connect.empty(),
              "fresh portable store defaults to Chinese");
        Profile first;
        first.name = L"测试网关";
        first.gateway = L"127.0.0.1:4443";
        first.username = "fixture-user";
        first.password = "Synthetic fixture password";
        first.batch_mode = true;
        first.token = "base32:TESTFIXTURE";
        first.token_type = 2;
        check(store.save(first, error) && first.id.size() == 32, "new profile save");
        std::string raw;
        check(read_file(store.directory() / L"profiles.json", raw, error) &&
                  raw.find(first.password) == std::string::npos && raw.find(first.token) == std::string::npos,
              "profile secrets are not plaintext");
        check(store.load(profiles, preferences, error) && profiles.size() == 1 &&
                  profiles[0].password == first.password && profiles[0].name == first.name,
              "portable profile load");
        Profile second;
        second.name = L"Second gateway";
        second.gateway = L"vpn.example.com/group";
        check(store.save(second, error) && store.load(profiles, preferences, error) && profiles.size() == 2,
              "multiple profiles");
        Profile duplicate = second;
        duplicate.id.clear();
        duplicate.name = L"SECOND GATEWAY";
        check(!store.save(duplicate, error), "duplicate display names rejected");
        preferences.language = Language::English;
        preferences.start_minimized = true;
        preferences.auto_connect = second.id;
        preferences.retry_failed = true;
        check(store.save_preferences(preferences, error) && store.load(profiles, preferences, error) &&
                  profiles.size() == 2 && preferences.language == Language::English &&
                  preferences.retry_failed && preferences.auto_connect == second.id,
              "preferences preserve profiles");
        auto invalid_preferences = preferences;
        invalid_preferences.auto_connect = std::string(32, '0');
        check(!store.save_preferences(invalid_preferences, error) &&
                  store.load(profiles, preferences, error) && preferences.auto_connect == second.id,
              "invalid automatic profile overwrote valid preferences");
        check(store.update_secrets(
                  first.id, first.gateway, [](Profile &p) { p.password = "New fixture password"; }, error),
              "session credentials update");
        check(!store.update_secrets(
                  first.id, L"other.example.com", [](Profile &p) { p.password = "Wrong origin password"; },
                  error),
              "old origin cannot update credentials");
        check(store.export_connection(root / L"fixture.vpn", first, error), "public profile export");
        check(read_file(root / L"fixture.vpn", raw, error) && raw.find("fixture-user") == std::string::npos &&
                  raw.find("password") == std::string::npos,
              "export excludes credentials");
        ProfileStore imported(root / L"imported");
        Profile imported_profile;
        check(imported.import_connection(root / L"fixture.vpn", imported_profile, error) &&
                  imported_profile.gateway == L"https://127.0.0.1:4443",
              "public vpn import");
        check(store.remove(second.id, error) && store.load(profiles, preferences, error) &&
                  profiles.size() == 1 && preferences.auto_connect.empty(),
              "profile deletion");
        check(!store.update_secrets(
                  second.id, second.gateway, [](Profile &p) { p.password = "do-not-recreate"; }, error),
              "late session cannot recreate a removed profile");
        first = profiles.front();
        first.username = std::string(8192, 'u');
        first.group = std::string(8192, 'g');
        check(store.save(first, error) && store.load(profiles, preferences, error) &&
                  profiles.front().username == first.username && profiles.front().group == first.group,
              "maximum accepted authentication fields survive reopening the store");
        first.username.push_back('u');
        check(!store.save(first, error), "oversized authentication fields cannot corrupt the store");
        first = profiles.front();
        first.gateway = L"different.example.com";
        check(store.save(first, error) && first.password.empty(), "changed gateway clears saved password");
        auto path = store.directory() / L"profiles.json";
        check(write_atomic(path, "{invalid fixture JSON", error), "write corrupt fixture");
        check(!store.load(profiles, preferences, error) && !store.save_preferences(preferences, error),
              "corrupt stores are not overwritten");
        check(read_file(path, raw, error) && raw == "{invalid fixture JSON", "corrupt original retained");
        std::cout
            << "{\"passed\":" << passed
            << ",\"boundary\":\"Synthetic portable profiles and current-user DPAPI; no VPN connection\"}\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Profile regression failed: " << exception.what() << "\n";
        return 1;
    }
}
