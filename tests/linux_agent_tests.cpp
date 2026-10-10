#include "agent.h"
#include "protocol.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Result {
    unsigned calls = 0;
    GVariant *secrets = nullptr;
    bool failed = false;
    ~Result() {
        if (secrets)
            g_variant_unref(secrets);
    }
};
void returned(NMSecretAgentOld *, NMConnection *, GVariant *secrets, GError *error, gpointer data) {
    auto *result = static_cast<Result *>(data);
    ++result->calls;
    if (secrets)
        result->secrets = g_variant_ref(secrets);
    result->failed = error != nullptr;
}
void wait(Result &result) {
    guint timeout = g_timeout_add(3000, [](gpointer) -> gboolean { return G_SOURCE_CONTINUE; }, nullptr);
    gint64 deadline = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
    while (!result.calls && g_get_monotonic_time() < deadline)
        g_main_context_iteration(nullptr, TRUE);
    g_source_remove(timeout);
    check(result.calls == 1, "authentication callback did not complete exactly once");
}
void request(NMSecretAgentOld *agent, NMConnection *connection, Result &result) {
    NM_SECRET_AGENT_OLD_GET_CLASS(agent)->get_secrets(agent, connection, "/fixture/1", "vpn", nullptr,
                                                      NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION,
                                                      returned, &result);
}
} // namespace
int main() {
    try {
        GError *error = nullptr;
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        check(bus != nullptr, "private test D-Bus was not available");
        auto *agent = NM_SECRET_AGENT_OLD(
            g_initable_new(linkora_agent_get_type(), nullptr, &error, NM_SECRET_AGENT_OLD_IDENTIFIER,
                           "io.github.sck03.linkoravpn.test", NM_SECRET_AGENT_OLD_AUTO_REGISTER, FALSE,
                           NM_SECRET_AGENT_OLD_DBUS_CONNECTION, bus, nullptr));
        check(agent != nullptr, "could not initialize the real secret agent");
        auto *profile = nm_simple_connection_new();
        auto *setting = nm_setting_connection_new();
        constexpr char uuid[] = "12345678-1234-4234-8234-123456789012";
        g_object_set(setting, NM_SETTING_CONNECTION_ID, "fixture", NM_SETTING_CONNECTION_UUID, uuid,
                     NM_SETTING_CONNECTION_TYPE, "vpn", nullptr);
        nm_connection_add_setting(profile, setting);
        auto *vpn = NM_SETTING_VPN(nm_setting_vpn_new());
        g_object_set(vpn, NM_SETTING_VPN_SERVICE_TYPE, linkora::Service, nullptr);
        nm_setting_vpn_add_data_item(vpn, "gateway", "fixture");
        nm_connection_add_setting(profile, NM_SETTING(vpn));
        for (int cycle = 0; cycle < 3; ++cycle) {
            linkora_agent_select(agent, uuid);
            Result result;
            request(agent, profile, result);
            wait(result);
            check(!result.failed && result.secrets, "successful authentication protocol was rejected");
            auto *group = g_variant_lookup_value(result.secrets, "vpn", G_VARIANT_TYPE_VARDICT);
            auto *secrets =
                group ? g_variant_lookup_value(group, "secrets", G_VARIANT_TYPE("a{ss}")) : nullptr;
            check(secrets && g_variant_n_children(secrets) == 3,
                  "private form fields were forwarded to NetworkManager");
            const char *cookie = nullptr;
            check(g_variant_lookup(secrets, "cookie", "&s", &cookie) && !strcmp(cookie, "synthetic-cookie"),
                  "cookie was lost");
            check(!g_variant_lookup(secrets, "password", "&s", &cookie),
                  "password escaped the private helper protocol");
            g_variant_unref(secrets);
            g_variant_unref(group);
        }
        nm_setting_vpn_add_data_item(vpn, "gateway", "hang");
        linkora_agent_select(agent, uuid);
        Result canceled;
        request(agent, profile, canceled);
        g_timeout_add(
            50,
            [](gpointer data) -> gboolean {
                linkora_agent_cancel(NM_SECRET_AGENT_OLD(data));
                return G_SOURCE_REMOVE;
            },
            agent);
        wait(canceled);
        check(canceled.failed && !canceled.secrets, "canceling authentication returned credentials");
        nm_setting_vpn_add_data_item(vpn, "gateway", "oversized");
        linkora_agent_select(agent, uuid);
        Result oversized;
        request(agent, profile, oversized);
        wait(oversized);
        check(oversized.failed && !oversized.secrets, "oversized helper output was accepted");
        nm_setting_vpn_add_data_item(vpn, "gateway", "invalid\nSECRET_KEY=cookie");
        linkora_agent_select(agent, uuid);
        Result injected;
        request(agent, profile, injected);
        wait(injected);
        check(injected.failed, "newline injection reached the helper");
        linkora_agent_cancel(agent);
        g_object_unref(profile);
        g_object_unref(agent);
        g_object_unref(bus);
        g_clear_error(&error);
        std::cout << "Authentication helper IPC, redaction, cancellation, reuse and input limits passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
