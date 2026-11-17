#include "profile.h"
#include "store_lease.h"
#include "vendor/json.hpp"
#include <algorithm>

namespace vpn {
using Json = nlohmann::json;
namespace {
constexpr size_t max_preferences_bytes = 16384;
bool valid_id(const std::string &id) {
    return id.empty() || (id.size() == 32 && std::all_of(id.begin(), id.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
}
bool valid(const Preferences &p) {
    return p.log_level >= 0 && p.log_level <= 3 && valid_id(p.selected) && valid_id(p.auto_connect);
}
}
bool ProfileStore::read_preferences(Preferences &preferences, std::wstring &error) const {
    preferences = {};
    const auto path = directory_ / L"preferences.json";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return true;
    std::string raw;
    if (!read_file(path, raw, error, max_preferences_bytes)) return false;
    try {
        const auto options = Json::parse(raw);
        if (!options.is_object() || options.value("schema", 0) != 1) throw std::runtime_error("Invalid preferences");
        preferences.language = options.value("language", std::string("zh-CN")) == "en" ? Language::English : Language::Chinese;
        preferences.minimize_to_tray = options.value("minimize_to_tray", true);
        preferences.minimize_instead_of_close = options.value("minimize_instead_of_close", false);
        preferences.start_minimized = options.value("start_minimized", false);
        preferences.single_instance = options.value("single_instance", true);
        preferences.retry_failed = options.value("retry_failed", false);
        preferences.auto_connect = options.value("auto_connect", std::string());
        preferences.log_level = options.value("log_level", 1);
        preferences.selected = options.value("selected", std::string());
        if (!valid(preferences)) throw std::runtime_error("Invalid preferences");
        return true;
    } catch (...) {
        error = L"偏好文件无效，原文件已保留。 / Invalid preferences; original file preserved.";
        return false;
    }
}
bool ProfileStore::save_preferences(const Preferences &preferences, std::wstring &error) {
    StoreLease lease(mutex_name_ + L".preferences", error);
    if (!lease) return false;
    Preferences current;
    if (!valid(preferences) || !read_preferences(current, error)) {
        if (error.empty()) error = L"偏好无效。 / Invalid preferences.";
        return false;
    }
    Json data{{"schema", 1},
        {"language", preferences.language == Language::Chinese ? "zh-CN" : "en"},
        {"minimize_to_tray", preferences.minimize_to_tray},
        {"minimize_instead_of_close", preferences.minimize_instead_of_close},
        {"start_minimized", preferences.start_minimized},
        {"single_instance", preferences.single_instance},
        {"retry_failed", preferences.retry_failed},
        {"auto_connect", preferences.auto_connect}, {"selected", preferences.selected},
        {"log_level", preferences.log_level}};
    const auto serialized = data.dump(2) + '\n';
    const auto path = directory_ / L"preferences.json";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string previous;
        if (!read_file(path, previous, error, max_preferences_bytes)) return false;
        if (previous == serialized) return true;
    }
    return write_atomic(path, serialized, error);
}
} // namespace vpn
