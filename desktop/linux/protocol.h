#pragma once
#include <glib.h>
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <strings.h>

namespace linkora {
inline constexpr char Service[] = "org.freedesktop.NetworkManager.openconnect";
inline constexpr char ProductID[] = "io.github.sck03.linkoravpn";
inline constexpr char Marker[] = "io.github.sck03.linkoravpn.managed";
inline bool field(std::string_view text) {
    return text.size() <= 8192 && text.find_first_of("\r\n\0", 0, 3) == std::string_view::npos;
}
inline std::string gateway(std::string text) {
    if (text.empty() || !field(text) || std::any_of(text.begin(), text.end(), [](unsigned char c) {
            return g_ascii_isspace(c) || c < 32 || c == '\\';
        }))
        return {};
    if (text.find("://") == std::string::npos)
        text = "https://" + text;
    GUri *uri = g_uri_parse(text.c_str(), G_URI_FLAGS_NONE, nullptr);
    if (!uri)
        return {};
    const char *host = g_uri_get_host(uri);
    int port = g_uri_get_port(uri);
    bool valid = !g_strcmp0(g_uri_get_scheme(uri), "https") && host && *host && !g_uri_get_userinfo(uri) &&
                 !g_uri_get_fragment(uri) && (port == -1 || (port > 0 && port <= 65535));
    if (valid && std::string(host).find_first_not_of("0123456789.") == std::string::npos) {
        gchar **octets = g_strsplit(host, ".", -1);
        valid = g_strv_length(octets) == 4;
        for (unsigned i = 0; valid && octets[i]; ++i)
            valid = *octets[i] && !(octets[i][0] == '0' && octets[i][1]) && strlen(octets[i]) <= 3 &&
                    atoi(octets[i]) <= 255;
        g_strfreev(octets);
    }
    g_uri_unref(uri);
    return valid ? text : std::string();
}
// The OpenConnect authentication helper returns key/value lines followed by an
// empty line. Keep only the ephemeral secrets required by the system VPN service.
class AuthReply {
public:
    ~AuthReply() {
        clear();
    }
    bool append(const char *data, size_t size) {
        if (complete || bytes_ + size > 256 * 1024)
            return false;
        bytes_ += size;
        pending_.append(data, size);
        size_t end;
        while ((end = pending_.find('\n')) != std::string::npos) {
            std::string line = pending_.substr(0, end);
            std::fill_n(pending_.data(), end, '\0');
            pending_.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!field(line))
                return false;
            if (key_.empty()) {
                if (line.empty()) {
                    complete = true;
                    return true;
                }
                key_ = line;
            } else {
                if (key_ == "cookie" || key_ == "gateway" || key_ == "gwcert" || key_ == "resolve")
                    values[key_] = line;
                key_.clear();
            }
            std::fill(line.begin(), line.end(), '\0');
        }
        return pending_.size() <= 8192;
    }
    bool valid() const {
        auto cookie = values.find("cookie"), server = values.find("gateway"), pin = values.find("gwcert");
        return complete && cookie != values.end() && !cookie->second.empty() && server != values.end() &&
               !server->second.empty() && pin != values.end() && !pin->second.empty();
    }
    void clear() {
        auto wipe = [](std::string &value) {
            if (!value.empty())
                explicit_bzero(value.data(), value.size());
            value.clear();
        };
        wipe(pending_);
        wipe(key_);
        for (auto &entry : values)
            wipe(entry.second);
        values.clear();
    }
    std::map<std::string, std::string> values;
    bool complete = false;

private:
    std::string pending_, key_;
    size_t bytes_ = 0;
};
struct Generation {
    uint64_t value = 0;
    bool wanted = false;
    uint64_t begin() {
        wanted = true;
        return ++value;
    }
    void cancel() {
        wanted = false;
        ++value;
    }
    bool accepts(uint64_t token) const {
        return wanted && token == value;
    }
};
inline unsigned retry_delay(const std::string &category, unsigned attempt) {
    return (category == "network" || category == "timeout") && attempt && attempt <= 3 ? 1u << attempt : 0;
}
} // namespace linkora
