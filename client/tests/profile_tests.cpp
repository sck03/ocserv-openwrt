#include "profile.h"
#include "vendor/json.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

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
        check(!normalize_gateway(L"vpn.example.com/" + std::wstring(4080, L'a'), normalized),
              "normalization expanded a gateway beyond its persistent limit");
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
        check(read_file(store.directory() / L"profiles.json", raw, error),
              "could not inspect stored credentials");
        auto encrypted_before = nlohmann::json::parse(raw)["profiles"][0]["password_dpapi"];
        check(store.save_preferences(preferences, error) && store.load(profiles, preferences, error) &&
                  profiles.size() == 2 && preferences.language == Language::English &&
                  preferences.retry_failed && preferences.auto_connect == second.id,
              "preferences preserve profiles");
        check(read_file(store.directory() / L"profiles.json", raw, error) &&
                  nlohmann::json::parse(raw)["profiles"][0]["password_dpapi"] == encrypted_before,
              "a preference update re-encrypted unchanged credentials");
        FILETIME marker{12345, 30000000};
        {
            Handle file(CreateFileW((store.directory() / L"profiles.json").c_str(), FILE_WRITE_ATTRIBUTES,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
            check(file && SetFileTime(file.get(), nullptr, nullptr, &marker),
                  "could not mark unchanged fixture");
        }
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        check(store.save_preferences(preferences, error) &&
                  GetFileAttributesExW((store.directory() / L"profiles.json").c_str(), GetFileExInfoStandard,
                                       &attributes) &&
                  CompareFileTime(&marker, &attributes.ftLastWriteTime) == 0,
              "identical preferences rewrote the file");
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
        ProfileStore concurrent(root / L"concurrent-import");
        Profile base;
        base.name = L"Existing"; base.gateway = L"https://base.example.com";
        check(concurrent.save(base, error), "concurrent import fixture");
        auto ca_import = root / L"public-ca.vpn";
        check(write_atomic(ca_import, "[VPN]\nName=Imported CA\nServer=https://fixture.example.com\nCABase64=LS0tLS1CRUdJTiBDRVJUSUZJQ0FURS0tLS0tCk1JSUNxekNDQVpPZ0F3SUJBZ0lCQVRBTkJna3Foa2lHOXcwQkFRc0ZBREFaTVJjd0ZRWURWUVFEREE1eVpYWnAKWlhjdFptbDRkSFZ5WlRBZUZ3MHlOakV3TWpNeU1ERTROVFJhRncweU5qRXdNalV5TURFNE5UUmFNQmt4RnpBVgpCZ05WQkFNTURuSmxkbWxsZHkxbWFYaDBkWEpsTUlJQklqQU5CZ2txaGtpRzl3MEJBUUVGQUFPQ0FROEFNSUlCCkNnS0NBUUVBc3luZk1jU0Q0TStCTHJ5ZEJDZmxuTVRaYzFSRkZ1ZTQzR09QY3lVbmNVaGNzZFYzYk50bXhLU0gKNUVrVWxMQnVSUzJIeFNFNEFqeUdwNGZrNGdBMXF3MVZ4ZmszU3N5VTdXTkZobVd0d1M3UEFVaGF5YlBZemE1QwoybjMvS21LRXQza25aV24vTGl1RXR5NUM4c3plcTdYYXMrYi9RSzd4aUxQV3N0NXM4MUlvZWRTK1FjbnFVejZ4CjhMWmthQnZwSGd4RTN5M3JSNHdQcUhUVVZOKzF4V25TN2d5WmFqVytjc1d2V2xBOGJZQ3ZEWU5acytucVcwRzkKRkFNcEpQSVhucUpVVnlVaVBNZ2hBYWxiZDlVVFFRWVBleXBYNnpyMWFHR0o4ZmxzbEtmcDNEdXF3SXBPL2E5dwpuVG1Zc0xsemNQRzRPdVV5enJYamlKWVVKb1lyeXdJREFRQUJNQTBHQ1NxR1NJYjNEUUVCQ3dVQUE0SUJBUUIwCk1xZ1BoYS9PUy9sSnRXWjF5aVpHQ1pmK3BoSlVnelQ3L21QeEFOaFFwK1BKTS9rSGc4L2JtSENMbThVM0s3a3IKQVFrTVJyYWpCT2xGQkNRNmxjczlYWXBDbUpValFkczV0bEtkTW05ckcxZjArMmZGMWZ0N1lSU2lFMGltd1ZJYQpWUi9lM1M0dU1pREptTG5OczU3a1Z4dkpNNmthMVNjWlpMZmRNWlduZ0RWOFFjQ2hsMXlkS0xSNE5IMW1LWDZECndFY2VRZU16N3RHQ0kvWkg2MFN2cnVVV0lGWVl1cjhjNm1iNUdaNW14Y1F5VXpnR0Zjbjl5Zk02V3NjY3hCbHQKd0VaN2dkQVJ2QVNCY3VPU1pueXd4aXdHTmt5TVRaczMyZmordkRvNktabjQrM3d1bUtja3JHblVHaVk2dW1hZgplbU82a1dHenRHby9MeWFSbFJnbAotLS0tLUVORCBDRVJUSUZJQ0FURS0tLS0tCg==\n", error), "public CA import fixture");
        Handle lease(CreateMutexW(nullptr, TRUE, local_object_name(L"ProfileStore", concurrent.directory()).c_str()));
        Handle entered(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Profile imported_ca;
        bool import_ok = false;
        std::wstring import_error;
        std::thread importer([&] {
            SetEvent(entered.get());
            import_ok = concurrent.import_connection(ca_import, imported_ca, import_error);
        });
        WaitForSingleObject(entered.get(), 2000);
        // Let an unlocked import stage its CA, then run the competing collector.
        Sleep(200);
        const bool saved = concurrent.save(base, error);
        ReleaseMutex(lease.get());
        importer.join();
        check(saved && import_ok && std::filesystem::exists(imported_ca.ca_file),
              "a concurrent save collected the CA of a successful import");
        check(store.load_metadata(profiles, preferences, error) && profiles.front().password.empty() &&
                  !profiles.front().password_blob.empty(), "metadata refresh decrypted stored credentials");
        ProfileStore::unlock(profiles.front());
        check(profiles.front().password == "New fixture password" && !profiles.front().secret_unavailable,
              "selected metadata credentials could not be unlocked");
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
        ProfileStore bounded(root / L"bounded");
        nlohmann::json large{{"schema", 2}, {"profiles", nlohmann::json::array()}};
        for (unsigned i = 0; i < 490; ++i) {
            auto id = random_id();
            large["profiles"].push_back({{"id", id},
                                         {"name", "Boundary " + std::to_string(i)},
                                         {"gateway", "https://vpn.example.com"},
                                         {"username", std::string(8192, 'u')},
                                         {"group", std::string(8192, 'g')}});
        }
        check(write_atomic(bounded.directory() / L"profiles.json", large.dump(), error) &&
                  bounded.load(profiles, preferences, error),
              "bounded fixture could not load");
        check(bounded.save_preferences(preferences, error), "valid aggregate configuration rejected");
        check(read_file(bounded.directory() / L"profiles.json", raw, error),
              "could not retain bounded original");
        auto expanded = profiles.front();
        expanded.ca_file = expanded.certificate_file = expanded.key_file = expanded.script =
            std::wstring(32760, L'a');
        check(!bounded.save(expanded, error), "aggregate configuration exceeded its own read limit");
        std::string retained;
        check(read_file(bounded.directory() / L"profiles.json", retained, error) && retained == raw &&
                  bounded.load(profiles, preferences, error),
              "aggregate rejection damaged the original");

        ProfileStore certificates(root / L"managed-certificates");
        Profile ca_profile;
        ca_profile.name = L"Managed CA";
        ca_profile.gateway = L"vpn.example.com";
        check(certificates.save(ca_profile, error), "CA fixture profile save");
        auto ca_path = certificates.directory() / L"certificates" / (wide(ca_profile.id) + L"-ca.pem");
        check(write_atomic(ca_path, "synthetic public CA", error), "CA fixture creation");
        ca_profile.ca_file = ca_path.wstring();
        check(certificates.save(ca_profile, error) && std::filesystem::exists(ca_path),
              "referenced CA was removed");
        auto external = certificates.directory() / L"certificates" / L"administrator.pem";
        check(write_atomic(external, "administrator-owned public CA", error), "external CA fixture creation");
        Profile shared_ca = ca_profile;
        shared_ca.id.clear();
        shared_ca.name = L"Shared CA";
        check(certificates.save(shared_ca, error) && certificates.remove(ca_profile.id, error) &&
                  std::filesystem::exists(ca_path),
              "removing a profile deleted another profile's CA");
        check(certificates.remove(shared_ca.id, error) && !std::filesystem::exists(ca_path) &&
                  std::filesystem::exists(external),
              "CA cleanup escaped program-managed orphan files");
        ProfileStore unicode(root / L"unicode-contracts");
        Profile unicode_profile;
        unicode_profile.name = L"Unicode round trip";
        unicode_profile.gateway = L"https://vpn.example.com/" + std::wstring(3000, L'路');
        unicode_profile.ca_file = (unicode.directory() / std::wstring(20000, L'证')).wstring();
        unicode_profile.certificate_file = unicode_profile.key_file = unicode_profile.script =
            unicode_profile.ca_file;
        check(unicode.save(unicode_profile, error) && unicode.load(profiles, preferences, error) &&
                  profiles.front().gateway == unicode_profile.gateway &&
                  profiles.front().ca_file == unicode_profile.ca_file &&
                  profiles.front().script == unicode_profile.script,
              "valid UTF-16 fields exceeded the JSON UTF-8 read limits");
        std::cout
            << "{\"passed\":" << passed
            << ",\"boundary\":\"Synthetic portable profiles and current-user DPAPI; no VPN connection\"}\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << "Profile regression failed: " << exception.what() << "\n";
        return 1;
    }
}
