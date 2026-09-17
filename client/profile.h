#pragma once
#include "platform.h"
#include <functional>

namespace bulijie {
struct Profile {
    Profile() = default;
    Profile(const Profile &) = default;
    Profile(Profile &&) noexcept = default;
    Profile &operator=(const Profile &) = default;
    Profile &operator=(Profile &&) noexcept = default;
    ~Profile() {
        erase(password);
        erase(token);
    }
    std::string id;
    std::wstring name;
    std::wstring gateway;
    std::string protocol = "anyconnect";
    std::string username, group, password;
    std::wstring ca_file, certificate_file, key_file;
    std::string server_pin;
    bool fixed_pin = false;
    int token_type = -1;
    std::string token;
    std::wstring interface_name, script;
    int reconnect_timeout = 300;
    int dtls_period = 25;
    int log_level = -1;
    bool minimize_on_connect = false;
    bool batch_mode = false;
    bool disable_udp = false;
    bool use_proxy = false;
    bool secret_unavailable = false;
    std::string password_blob, token_blob;
};
struct Preferences {
    Language language = Language::Chinese;
    bool minimize_to_tray = true;
    bool minimize_instead_of_close = false;
    bool start_minimized = false;
    bool single_instance = true;
    int log_level = 1;
    std::string selected;
};
bool validate_profile(Profile &profile, std::wstring &error);
class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path directory = {});
    const std::filesystem::path &directory() const {
        return directory_;
    }
    bool load(std::vector<Profile> &profiles, Preferences &preferences, std::wstring &error) const;
    bool save(Profile &profile, std::wstring &error);
    bool remove(const std::string &id, std::wstring &error);
    bool save_preferences(const Preferences &preferences, std::wstring &error);
    bool update_secrets(const std::string &id, const std::wstring &gateway,
                        const std::function<void(Profile &)> &change, std::wstring &error);
    bool import_connection(const std::filesystem::path &path, Profile &profile, std::wstring &error);
    bool export_connection(const std::filesystem::path &path, const Profile &profile,
                           std::wstring &error) const;

private:
    bool update(const std::function<bool(std::vector<Profile> &, Preferences &, std::wstring &)> &change,
                std::wstring &error);
    bool write(const std::vector<Profile> &profiles, const Preferences &preferences,
               std::wstring &error) const;
    std::filesystem::path directory_;
    std::wstring mutex_name_;
};
} // namespace bulijie
