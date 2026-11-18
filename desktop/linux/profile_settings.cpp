#include "profile_settings.h"
#include "protocol.h"
#include "../../client/connection_file.h"
#include <glib/gstdio.h>
#include <fstream>
#include <memory>
#include <set>

namespace lansyra {
NMConnection *profile_candidate(NMConnection *existing, const std::string &name,
    const std::string &server, const std::string &username, const std::string &ca) {
    auto *connection = existing ? nm_simple_connection_new_clone(existing) : nm_simple_connection_new();
    auto *setting = nm_connection_get_setting_connection(connection);
    if (!setting) {
        setting = NM_SETTING_CONNECTION(nm_setting_connection_new());
        g_autofree char *uuid = g_uuid_string_random();
        g_object_set(setting, NM_SETTING_CONNECTION_UUID, uuid, NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME,
            NM_SETTING_CONNECTION_AUTOCONNECT, FALSE, nullptr);
        nm_setting_connection_add_permission(setting, "user", g_get_user_name(), nullptr);
        nm_connection_add_setting(connection, NM_SETTING(setting));
    }
    g_object_set(setting, NM_SETTING_CONNECTION_ID, name.c_str(), nullptr);
    auto *vpn = nm_connection_get_setting_vpn(connection);
    if (!vpn) {
        vpn = NM_SETTING_VPN(nm_setting_vpn_new());
        nm_connection_add_setting(connection, NM_SETTING(vpn));
    }
    g_object_set(vpn, NM_SETTING_VPN_SERVICE_TYPE, Service, NM_SETTING_VPN_USER_NAME, username.c_str(), nullptr);
    nm_setting_vpn_add_data_item(vpn, "gateway", server.c_str());
    nm_setting_vpn_add_data_item(vpn, "protocol", "anyconnect");
    if (ca.empty()) {
        // Only undo our CA policy on the transition from a configured CA.
        if (nm_setting_vpn_get_data_item(vpn, "cacert"))
            nm_setting_vpn_remove_data_item(vpn, "prevent_invalid_cert");
        nm_setting_vpn_remove_data_item(vpn, "cacert");
    }
    else {
        nm_setting_vpn_add_data_item(vpn, "cacert", ca.c_str());
        nm_setting_vpn_add_data_item(vpn, "prevent_invalid_cert", "yes");
    }
    nm_connection_clear_secrets(connection);
    for (const char *key : {"cookie", "gateway", "gwcert", "resolve"})
        nm_setting_set_secret_flags(NM_SETTING(vpn), key, NM_SETTING_SECRET_FLAG_NOT_SAVED, nullptr);
    auto *user = NM_SETTING_USER(nm_connection_get_setting(connection, NM_TYPE_SETTING_USER));
    if (!user) { user = NM_SETTING_USER(nm_setting_user_new()); nm_connection_add_setting(connection, NM_SETTING(user)); }
    nm_setting_user_set_data(user, Marker, "1", nullptr);
    for (GType type : {NM_TYPE_SETTING_IP4_CONFIG, NM_TYPE_SETTING_IP6_CONFIG}) {
        if (nm_connection_get_setting(connection, type)) continue;
        auto *ip = NM_SETTING(g_object_new(type, NM_SETTING_IP_CONFIG_METHOD, "auto", NM_SETTING_IP_CONFIG_DNS_PRIORITY, -50, nullptr));
        nm_connection_add_setting(connection, ip);
    }
    return connection;
}

ImportedProfile::~ImportedProfile() { if (!retained && !ca.empty()) g_unlink(ca.c_str()); }

void prune_profile_certificates(const GPtrArray *connections, const std::string &removedID) {
    std::set<std::string> referenced;
    for (guint i = 0; i < connections->len; ++i) {
        auto *connection = NM_CONNECTION(g_ptr_array_index(connections, i));
        if (removedID == nm_connection_get_uuid(connection)) continue;
        auto *vpn = nm_connection_get_setting_vpn(connection);
        const char *ca = vpn ? nm_setting_vpn_get_data_item(vpn, "cacert") : nullptr;
        if (ca) { g_autofree char *path = g_canonicalize_filename(ca, nullptr); referenced.insert(path); }
    }
    const auto directory = std::string(g_get_user_config_dir()) + "/lansyra/certificates";
    if (g_file_test((std::string(g_get_user_config_dir()) + "/lansyra").c_str(), G_FILE_TEST_IS_SYMLINK) ||
        g_file_test(directory.c_str(), G_FILE_TEST_IS_SYMLINK)) return;
    GDir *dir = g_dir_open(directory.c_str(), 0, nullptr);
    if (!dir) return;
    while (const char *entry = g_dir_read_name(dir)) {
        const std::string name(entry), path = directory + "/" + name;
        if (name.size() == 40 && name.substr(36) == ".pem" && g_uuid_string_is_valid(name.substr(0, 36).c_str()) && !referenced.count(path))
            g_unlink(path.c_str());
    }
    g_dir_close(dir);
}

ImportedProfile *read_public_profile(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read public profile");
    std::string raw;
    char chunk[4096];
    while (file.read(chunk, sizeof(chunk)) || file.gcount()) {
        raw.append(chunk, static_cast<size_t>(file.gcount()));
        if (raw.size() > ConnectionFile::MaxBytes) throw std::runtime_error("Profile is too large");
    }
    if (!file.eof() || !g_utf8_validate(raw.data(), static_cast<gssize>(raw.size()), nullptr)) throw std::runtime_error("Invalid profile encoding");
    const auto value = ConnectionFile::parse(raw);
    if (value.protocol != "anyconnect" || value.trust == "pin")
        throw std::runtime_error("Linux import requires an AnyConnect CA or first-connection profile");
    const auto server = gateway(value.server);
    if (server.empty()) throw std::runtime_error("Invalid gateway");
    auto result = std::make_unique<ImportedProfile>();
    result->name = value.name.empty() ? "Lansyra" : value.name;
    result->server = server;
    if (!value.ca.empty()) {
        gsize size = 0;
        g_autofree guchar *bytes = g_base64_decode(value.ca.c_str(), &size);
        g_autofree char *canonical = g_base64_encode(bytes, size);
        std::string pem(reinterpret_cast<char *>(bytes), size);
        if (!size || size > 65536 || value.ca != canonical || pem.find("PRIVATE KEY") != std::string::npos)
            throw std::runtime_error("Invalid public CA");
        g_autoptr(GTlsCertificate) certificate = g_tls_certificate_new_from_pem(pem.data(), static_cast<gssize>(pem.size()), nullptr);
        if (!certificate) throw std::runtime_error("Invalid public CA");
        const auto directory = std::string(g_get_user_config_dir()) + "/lansyra/certificates";
        if (g_file_test((std::string(g_get_user_config_dir()) + "/lansyra").c_str(), G_FILE_TEST_IS_SYMLINK) ||
            g_file_test(directory.c_str(), G_FILE_TEST_IS_SYMLINK)) throw std::runtime_error("Invalid certificate storage directory");
        g_autofree char *uuid = g_uuid_string_random();
        result->ca = directory + "/" + uuid + ".pem";
        if (g_mkdir_with_parents(directory.c_str(), 0700) ||
            !g_file_set_contents_full(result->ca.c_str(), pem.data(), static_cast<gssize>(pem.size()),
                static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE), 0600, nullptr))
            throw std::runtime_error("Cannot store public CA");
    }
    return result.release();
}
}
