#pragma once
#include "platform.h"
#include <functional>
#include <utility>

namespace vpn {
// Keep credential ownership separate so adding an ordinary Profile field does
// not require maintaining a hand-written list of copy/move operations.
struct ProfileSecrets {
    ProfileSecrets() = default;
    ProfileSecrets(const ProfileSecrets &) = default;
    ProfileSecrets(ProfileSecrets &&other) noexcept {
        *this = std::move(other);
    }
    ProfileSecrets &operator=(const ProfileSecrets &other) {
        if (this != &other) {
            clear();
            password = other.password;
            token = other.token;
            password_blob = other.password_blob;
            token_blob = other.token_blob;
        }
        return *this;
    }
    ProfileSecrets &operator=(ProfileSecrets &&other) noexcept {
        if (this != &other) {
            move_secret(password, other.password);
            move_secret(token, other.token);
            move_secret(password_blob, other.password_blob);
            move_secret(token_blob, other.token_blob);
        }
        return *this;
    }
    ~ProfileSecrets() {
        clear();
    }
    std::string password, token, password_blob, token_blob;

private:
    void clear() noexcept {
        erase(password);
        erase(token);
        erase(password_blob);
        erase(token_blob);
    }
    static void move_secret(std::string &target, std::string &source) noexcept {
        erase(target);
        char *const original = source.data();
        const auto size = source.size();
        target = std::move(source);
        // A short-string move can leave the previous bytes in the source's
        // inline storage even though its size is already zero.
        if (source.data() == original && size)
            SecureZeroMemory(source.data(), size);
        source.clear();
    }
};
struct Profile : ProfileSecrets {
    std::string id;
    std::wstring name;
    std::wstring gateway;
    std::string protocol = "anyconnect";
    std::string username, group;
    std::wstring ca_file, certificate_file, key_file;
    std::string server_pin;
    bool fixed_pin = false;
    int token_type = -1;
    std::wstring interface_name, script;
    int reconnect_timeout = 300;
    int dtls_period = 25;
    int log_level = -1;
    bool minimize_on_connect = false;
    bool batch_mode = false;
    bool disable_udp = false;
    bool use_proxy = false;
    bool secret_unavailable = false;
};
struct Preferences {
    Language language = Language::Chinese;
    bool minimize_to_tray = true;
    bool minimize_instead_of_close = false;
    bool start_minimized = false;
    bool single_instance = true;
    bool retry_failed = false;
    int log_level = 1;
    std::string selected, auto_connect;
};
bool validate_profile(Profile &profile, std::wstring &error);
class ProfileStore {
public:
    static constexpr size_t MaxFileBytes = 8 * 1024 * 1024;
    explicit ProfileStore(std::filesystem::path directory = {});
    const std::filesystem::path &directory() const {
        return directory_;
    }
    bool load(std::vector<Profile> &profiles, Preferences &preferences, std::wstring &error) const;
    bool load_metadata(std::vector<Profile> &profiles, Preferences &preferences, std::wstring &error) const;
    static void unlock(Profile &profile);
    bool save(Profile &profile, std::wstring &error);
    bool remove(const std::string &id, std::wstring &error);
    bool save_preferences(const Preferences &preferences, std::wstring &error);
    bool update_secrets(const std::string &id, const std::wstring &gateway,
                        const std::function<void(Profile &)> &change, std::wstring &error);
    bool import_connection(const std::filesystem::path &path, Profile &profile, std::wstring &error);
    bool export_connection(const std::filesystem::path &path, const Profile &profile,
                           std::wstring &error) const;

private:
    bool read(std::vector<Profile> &profiles, Preferences &preferences, std::wstring &error,
              bool decrypt) const;
    void prune_certificates(const std::vector<Profile> &profiles) const;
    bool update(const std::function<bool(std::vector<Profile> &, Preferences &, std::wstring &)> &change,
                std::wstring &error, bool prune = false);
    bool write(const std::vector<Profile> &profiles, const Preferences &preferences,
               std::wstring &error) const;
    std::filesystem::path directory_;
    std::wstring mutex_name_;
};
} // namespace vpn
