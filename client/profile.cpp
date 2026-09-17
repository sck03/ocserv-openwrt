#include "profile.h"
#include "vendor/json.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace bulijie {
using Json = nlohmann::json;
namespace {
bool identifier(const std::string &value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string context(const Profile &p, const char *kind) {
    std::wstring normalized, origin;
    normalize_gateway(p.gateway, normalized, &origin);
    return "BulijieVPN/0.5/" + p.id + "/" + kind + "/" + utf8(origin);
}
std::wstring text(const Json &value, const char *key, size_t maximum = 32768) {
    auto item = value.find(key);
    if (item == value.end())
        return {};
    if (!item->is_string())
        throw std::runtime_error("Invalid string field");
    auto result = item->get<std::string>();
    if (result.size() > maximum || result.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid string length");
    return wide(result);
}
bool same_name(const std::wstring &a, const std::wstring &b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
Json encode_profile(const Profile &p, std::wstring &error) {
    std::string password, token;
    if (p.batch_mode) {
        if (p.password.empty() && p.secret_unavailable)
            password = p.password_blob;
        else if (!protect_secret(p.password, context(p, "password"), password, error))
            return {};
    }
    if (p.token.empty() && p.secret_unavailable)
        token = p.token_blob;
    else if (!protect_secret(p.token, context(p, "token"), token, error))
        return {};
    return {{"id", p.id},
            {"name", utf8(p.name)},
            {"gateway", utf8(p.gateway)},
            {"protocol", p.protocol},
            {"username", p.username},
            {"group", p.group},
            {"password_dpapi", password},
            {"token_dpapi", token},
            {"ca_file", utf8(p.ca_file)},
            {"certificate_file", utf8(p.certificate_file)},
            {"key_file", utf8(p.key_file)},
            {"server_pin", p.server_pin},
            {"fixed_pin", p.fixed_pin},
            {"token_type", p.token_type},
            {"interface_name", utf8(p.interface_name)},
            {"script", utf8(p.script)},
            {"reconnect_timeout", p.reconnect_timeout},
            {"dtls_period", p.dtls_period},
            {"log_level", p.log_level},
            {"minimize_on_connect", p.minimize_on_connect},
            {"batch_mode", p.batch_mode},
            {"disable_udp", p.disable_udp},
            {"use_proxy", p.use_proxy}};
}
Profile decode_profile(const Json &j, const std::filesystem::path &directory) {
    if (!j.is_object())
        throw std::runtime_error("Invalid profile");
    Profile p;
    p.id = utf8(text(j, "id", 32));
    p.name = text(j, "name", 512);
    p.gateway = text(j, "gateway", 8192);
    p.protocol = j.value("protocol", std::string("anyconnect"));
    p.username = utf8(text(j, "username", 4096));
    p.group = utf8(text(j, "group", 4096));
    p.ca_file = text(j, "ca_file");
    p.certificate_file = text(j, "certificate_file");
    p.key_file = text(j, "key_file");
    p.server_pin = utf8(text(j, "server_pin", 128));
    p.fixed_pin = j.value("fixed_pin", false);
    p.token_type = j.value("token_type", -1);
    p.interface_name = text(j, "interface_name", 512);
    p.script = text(j, "script");
    for (auto *field : {&p.ca_file, &p.certificate_file, &p.key_file, &p.script}) {
        if (field->empty() || field->rfind(L"system:", 0) == 0 || field->rfind(L"pkcs11:", 0) == 0)
            continue;
        if (std::filesystem::path(*field).is_relative())
            *field = (directory / *field).lexically_normal().wstring();
    }
    p.reconnect_timeout = j.value("reconnect_timeout", 300);
    p.dtls_period = j.value("dtls_period", 25);
    p.log_level = j.value("log_level", -1);
    p.minimize_on_connect = j.value("minimize_on_connect", false);
    p.batch_mode = j.value("batch_mode", false);
    p.disable_udp = j.value("disable_udp", false);
    p.use_proxy = j.value("use_proxy", false);
    p.password_blob = utf8(text(j, "password_dpapi", 65536));
    p.token_blob = utf8(text(j, "token_dpapi", 65536));
    p.secret_unavailable = !unprotect_secret(p.password_blob, context(p, "password"), p.password);
    if (!unprotect_secret(p.token_blob, context(p, "token"), p.token))
        p.secret_unavailable = true;
    std::wstring error;
    if (!identifier(p.id) || !validate_profile(p, error))
        throw std::runtime_error("Invalid profile data");
    return p;
}
} // namespace
bool validate_profile(Profile &p, std::wstring &error) {
    p.name = trim(p.name);
    if (p.name.empty() || p.name.size() > 128 || p.name.find_first_of(L"\r\n\t") != std::wstring::npos) {
        error = L"请填写 1–128 个字符的配置名称。 / Enter a profile name of 1–128 characters.";
        return false;
    }
    std::wstring normalized;
    if (!normalize_gateway(p.gateway, normalized)) {
        error = L"请输入有效的 HTTPS 网关地址。 / Enter a valid HTTPS gateway.";
        return false;
    }
    p.gateway = normalized;
    if (p.protocol.empty() || p.protocol.size() > 32 ||
        !std::all_of(p.protocol.begin(), p.protocol.end(),
                     [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; })) {
        error = L"VPN 协议无效。 / Invalid VPN protocol.";
        return false;
    }
    if (!valid_pin(p.server_pin)) {
        error = L"请填写完整的 pin-sha256 指纹。 / Enter a complete pin-sha256 fingerprint.";
        return false;
    }
    if (p.reconnect_timeout < 0 || p.reconnect_timeout > 86400 || p.dtls_period < 0 || p.dtls_period > 3600 ||
        p.log_level < -1 || p.log_level > 3 ||
        (p.token_type != -1 && p.token_type != 1 && p.token_type != 2 && p.token_type != 3)) {
        error = L"连接时间或认证选项无效。 / Invalid connection or authentication option.";
        return false;
    }
    for (const auto *value : {&p.username, &p.group, &p.password, &p.token}) {
        if (value->size() > 8192 || value->find('\0') != std::string::npos) {
            error = L"认证字段过长或包含无效字符。 / Invalid authentication field.";
            return false;
        }
    }
    if (p.interface_name.size() > 128 ||
        p.interface_name.find_first_of(L"\r\n\"&|<>^") != std::wstring::npos ||
        p.script.find_first_of(L"\r\n\"") != std::wstring::npos) {
        error = L"接口名称或脚本路径无效。 / Invalid interface name or script path.";
        return false;
    }
    for (const auto *field :
         {&p.name, &p.gateway, &p.interface_name, &p.script, &p.ca_file, &p.certificate_file, &p.key_file}) {
        if (field->size() > 32760 || field->find(L'\0') != std::wstring::npos) {
            error = L"配置包含无效字符。 / Invalid configuration characters.";
            return false;
        }
    }
    if (p.fixed_pin && p.server_pin.empty())
        p.fixed_pin = false;
    return true;
}
ProfileStore::ProfileStore(std::filesystem::path directory)
    : directory_(directory.empty() ? executable_directory() / L"data" : std::move(directory)) {
    directory_ = std::filesystem::absolute(directory_).lexically_normal();
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t c : directory_.wstring()) {
        hash ^= static_cast<uint64_t>(std::towlower(c));
        hash *= 1099511628211ULL;
    }
    mutex_name_ = L"Local\\BulijieVPN.ProfileStore." + std::to_wstring(hash);
}
bool ProfileStore::load(std::vector<Profile> &profiles, Preferences &preferences, std::wstring &error) const {
    profiles.clear();
    preferences = {};
    auto path = directory_ / L"profiles.json";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        return true;
    if (!std::filesystem::exists(directory_))
        return true;
    std::string raw;
    if (!read_file(path, raw, error))
        return false;
    try {
        Json data = Json::parse(raw);
        if (!data.is_object() || data.value("schema", 0) != 1 || !data.contains("profiles") ||
            !data["profiles"].is_array() || data["profiles"].size() > 512)
            throw std::runtime_error("Unsupported configuration");
        const Json &options = data.value("settings", Json::object());
        preferences.language =
            options.value("language", std::string("zh-CN")) == "en" ? Language::English : Language::Chinese;
        preferences.minimize_to_tray = options.value("minimize_to_tray", true);
        preferences.minimize_instead_of_close = options.value("minimize_instead_of_close", false);
        preferences.start_minimized = options.value("start_minimized", false);
        preferences.single_instance = options.value("single_instance", true);
        preferences.log_level = options.value("log_level", 1);
        preferences.selected = options.value("selected", std::string());
        if (preferences.log_level < 0 || preferences.log_level > 3)
            throw std::runtime_error("Invalid log level");
        for (const auto &entry : data["profiles"]) {
            auto profile = decode_profile(entry, directory_);
            for (const auto &existing : profiles)
                if (existing.id == profile.id || same_name(existing.name, profile.name))
                    throw std::runtime_error("Duplicate profile");
            profiles.push_back(std::move(profile));
        }
        return true;
    } catch (...) {
        error =
            L"配置文件无效，未覆盖原文件。 / Invalid profile file; the original has not been overwritten.";
        return false;
    }
}
bool ProfileStore::write(const std::vector<Profile> &profiles, const Preferences &preferences,
                         std::wstring &error) const {
    Json data{{"schema", 1},
              {"settings",
               {{"language", preferences.language == Language::Chinese ? "zh-CN" : "en"},
                {"minimize_to_tray", preferences.minimize_to_tray},
                {"minimize_instead_of_close", preferences.minimize_instead_of_close},
                {"start_minimized", preferences.start_minimized},
                {"single_instance", preferences.single_instance},
                {"log_level", preferences.log_level},
                {"selected", preferences.selected}}},
              {"profiles", Json::array()}};
    for (const auto &profile : profiles) {
        Profile portable = profile;
        for (auto *field :
             {&portable.ca_file, &portable.certificate_file, &portable.key_file, &portable.script}) {
            if (field->empty() || field->rfind(L"system:", 0) == 0 || field->rfind(L"pkcs11:", 0) == 0)
                continue;
            auto relative = std::filesystem::path(*field).lexically_relative(directory_);
            if (!relative.empty() && *relative.begin() != L"..")
                *field = relative.generic_wstring();
        }
        Json item = encode_profile(portable, error);
        if (item.is_null())
            return false;
        data["profiles"].push_back(std::move(item));
    }
    return write_atomic(directory_ / L"profiles.json", data.dump(2) + '\n', error);
}
bool ProfileStore::update(
    const std::function<bool(std::vector<Profile> &, Preferences &, std::wstring &)> &change,
    std::wstring &error) {
    Handle mutex(CreateMutexW(nullptr, FALSE, mutex_name_.c_str()));
    if (!mutex) {
        error = system_error(GetLastError());
        return false;
    }
    DWORD wait = WaitForSingleObject(mutex.get(), 5000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        error = L"另一个实例正在保存配置。 / Another instance is saving profiles.";
        return false;
    }
    struct Unlock {
        HANDLE handle;
        ~Unlock() {
            ReleaseMutex(handle);
        }
    } unlock{mutex.get()};
    std::vector<Profile> profiles;
    Preferences preferences;
    if (!load(profiles, preferences, error) || !change(profiles, preferences, error))
        return false;
    return write(profiles, preferences, error);
}
bool ProfileStore::save(Profile &profile, std::wstring &error) {
    if (!validate_profile(profile, error))
        return false;
    if (profile.id.empty())
        profile.id = random_id();
    if (!identifier(profile.id)) {
        error = L"配置标识无效。 / Invalid profile identifier.";
        return false;
    }
    return update(
        [&](auto &profiles, auto &preferences, auto &message) {
            for (auto &existing : profiles) {
                if (existing.id != profile.id && same_name(existing.name, profile.name)) {
                    message = L"配置名称已存在。 / Profile name already exists.";
                    return false;
                }
            }
            for (auto &existing : profiles)
                if (existing.id == profile.id) {
                    std::wstring a, b, unused;
                    normalize_gateway(existing.gateway, unused, &a);
                    normalize_gateway(profile.gateway, unused, &b);
                    if (a != b) {
                        erase(profile.password);
                        erase(profile.token);
                        profile.password_blob.clear();
                        profile.token_blob.clear();
                        profile.username.clear();
                        profile.group.clear();
                        profile.server_pin.clear();
                        profile.fixed_pin = false;
                        profile.secret_unavailable = false;
                    }
                    existing = profile;
                    preferences.selected = profile.id;
                    return true;
                }
            if (profiles.size() >= 512) {
                message = L"配置数量已达上限。 / Too many profiles.";
                return false;
            }
            profiles.push_back(profile);
            preferences.selected = profile.id;
            return true;
        },
        error);
}
bool ProfileStore::remove(const std::string &id, std::wstring &error) {
    return update(
        [&](auto &profiles, auto &preferences, auto &message) {
            auto it =
                std::find_if(profiles.begin(), profiles.end(), [&](const auto &p) { return p.id == id; });
            if (it == profiles.end()) {
                message = L"配置已不存在。 / Profile no longer exists.";
                return false;
            }
            profiles.erase(it);
            if (preferences.selected == id)
                preferences.selected = profiles.empty() ? "" : profiles.front().id;
            return true;
        },
        error);
}
bool ProfileStore::save_preferences(const Preferences &preferences, std::wstring &error) {
    return update(
        [&](auto &, auto &current, auto &) {
            current = preferences;
            return true;
        },
        error);
}
bool ProfileStore::update_secrets(const std::string &id, const std::wstring &gateway,
                                  const std::function<void(Profile &)> &change, std::wstring &error) {
    return update(
        [&](auto &profiles, auto &, auto &message) {
            std::wstring expected, unused;
            normalize_gateway(gateway, unused, &expected);
            for (auto &p : profiles)
                if (p.id == id) {
                    std::wstring current;
                    normalize_gateway(p.gateway, unused, &current);
                    if (current != expected) {
                        message = L"服务器地址已更改，未保存旧会话的凭据。 / Gateway changed; credentials "
                                  L"were not saved.";
                        return false;
                    }
                    change(p);
                    return validate_profile(p, message);
                }
            message = L"配置已删除，未重新创建。 / Profile was removed; it was not recreated.";
            return false;
        },
        error);
}
bool ProfileStore::import_connection(const std::filesystem::path &path, Profile &profile,
                                     std::wstring &error) {
    std::string raw;
    if (!read_file(path, raw, error, 2 * 1024 * 1024))
        return false;
    if (raw.rfind("\xef\xbb\xbf", 0) == 0)
        raw.erase(0, 3);
    if (raw.rfind("\xff\xfe", 0) == 0) {
        if (raw.size() % 2) {
            error = L"配置文件编码无效。 / Invalid profile encoding.";
            return false;
        }
        std::wstring decoded((raw.size() - 2) / 2, L'\0');
        memcpy(decoded.data(), raw.data() + 2, raw.size() - 2);
        raw = utf8(decoded);
    }
    if (raw.find('\0') != std::string::npos || utf8(wide(raw)) != raw) {
        error = L"配置文件编码无效。 / Invalid profile encoding.";
        return false;
    }
    Profile candidate;
    candidate.name = path.stem().wstring();
    std::istringstream lines(raw);
    std::string line;
    bool vpn = false;
    std::string ca;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line == "[VPN]") {
            vpn = true;
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            vpn = false;
            continue;
        }
        if (!vpn || line.empty() || line.front() == ';' || line.front() == '#')
            continue;
        auto split = line.find('=');
        if (split == std::string::npos)
            continue;
        auto key = line.substr(0, split), value = line.substr(split + 1);
        if (key == "Server")
            candidate.gateway = wide(value);
        else if (key == "Name")
            candidate.name = wide(value);
        else if (key == "Protocol")
            candidate.protocol = value;
        else if (key == "ServerPin") {
            candidate.server_pin = value;
            candidate.fixed_pin = !value.empty();
        } else if (key == "AuthGroup")
            candidate.group = value;
        else if (key == "CABase64")
            ca = value;
        else if (key == "PreferUDP") {
            if (value != "0" && value != "1") {
                error = L"UDP 选项无效。 / Invalid UDP option.";
                return false;
            }
            candidate.disable_udp = value == "0";
        } else if (key == "ReconnectSeconds") {
            try {
                size_t end = 0;
                candidate.reconnect_timeout = std::stoi(value, &end);
                if (end != value.size())
                    throw std::runtime_error("Trailing characters");
            } catch (...) {
                error = L"无效的重连时间。 / Invalid reconnect timeout.";
                return false;
            }
        }
    }
    if (!validate_profile(candidate, error))
        return false;
    candidate.id = random_id();
    if (!identifier(candidate.id)) {
        error = L"无法创建配置标识。 / Could not create a profile identifier.";
        return false;
    }
    std::filesystem::path imported_certificate;
    if (!ca.empty()) {
        std::string certificate;
        if (!unbase64(ca, certificate) || !valid_public_certificates(certificate, error)) {
            if (error.empty())
                error = L"CA 证书无效。 / Invalid CA certificate.";
            return false;
        }
        auto destination = directory_ / L"certificates" / (wide(candidate.id) + L"-ca.pem");
        if (!write_atomic(destination, certificate, error))
            return false;
        imported_certificate = destination;
        candidate.ca_file = destination.wstring();
    }
    if (!save(candidate, error)) {
        if (!imported_certificate.empty())
            DeleteFileW(imported_certificate.c_str());
        return false;
    }
    profile = std::move(candidate);
    return true;
}
bool ProfileStore::export_connection(const std::filesystem::path &path, const Profile &profile,
                                     std::wstring &error) const {
    Profile p = profile;
    if (!validate_profile(p, error))
        return false;
    std::string result = "[VPN]\nName=" + utf8(p.name) + "\nServer=" + utf8(p.gateway) +
                         "\nProtocol=" + p.protocol + "\nPreferUDP=" + (p.disable_udp ? "0" : "1") +
                         "\nReconnectSeconds=" + std::to_string(p.reconnect_timeout) + "\n";
    if (!p.group.empty() && p.group.find_first_of("\r\n") == std::string::npos)
        result += "AuthGroup=" + p.group + "\n";
    if (!p.server_pin.empty())
        result += "ServerPin=" + p.server_pin + "\n";
    else if (!p.ca_file.empty()) {
        std::string ca;
        if (!read_file(p.ca_file, ca, error, 1024 * 1024))
            return false;
        if (!valid_public_certificates(ca, error))
            return false;
        result += "CABase64=" + base64(ca) + "\n";
    }
    return write_atomic(path, result, error);
}
} // namespace bulijie
