#include "agent.h"
#include "protocol.h"
#include <gio/gio.h>
#include <libsecret/secret.h>

using namespace linkora;
struct Request;
struct LinkoraAgent {
    NMSecretAgentOld parent;
    char *uuid;
    Request *request;
};
struct LinkoraAgentClass {
    NMSecretAgentOldClass parent;
};
G_DEFINE_TYPE(LinkoraAgent, linkora_agent, NM_TYPE_SECRET_AGENT_OLD)

struct Request {
    LinkoraAgent *agent;
    NMConnection *connection;
    NMSecretAgentOldGetSecretsFunc callback;
    gpointer data;
    GSubprocess *process = nullptr;
    GCancellable *cancel = g_cancellable_new();
    AuthReply reply;
    std::string path;
    Request(LinkoraAgent *owner, NMConnection *profile, NMSecretAgentOldGetSecretsFunc done, gpointer context)
        : agent(reinterpret_cast<LinkoraAgent *>(g_object_ref(owner))),
          connection(NM_CONNECTION(g_object_ref(profile))), callback(done), data(context) {}
    ~Request() {
        g_clear_object(&process);
        g_clear_object(&cancel);
        g_object_unref(connection);
        g_object_unref(agent);
    }
};
namespace {
void complete(Request *request, bool success) {
    auto *agent = request->agent;
    agent->request = nullptr;
    GVariant *result = nullptr;
    GError *error = nullptr;
    if (success && request->reply.valid() && !g_cancellable_is_cancelled(request->cancel)) {
        auto *connection = nm_simple_connection_new();
        auto *setting = NM_SETTING_VPN(nm_setting_vpn_new());
        for (const auto &entry : request->reply.values)
            nm_setting_vpn_add_secret(setting, entry.first.c_str(), entry.second.c_str());
        nm_connection_add_setting(connection, NM_SETTING(setting));
        result = nm_connection_to_dbus(connection, NM_CONNECTION_SERIALIZE_ONLY_SECRETS);
        g_object_unref(connection);
        if (request->process) {
            auto *input = g_subprocess_get_stdin_pipe(request->process);
            g_output_stream_write_all(input, "QUIT\n", 5, nullptr, nullptr, nullptr);
            g_output_stream_close(input, nullptr, nullptr);
        }
    } else {
        error = g_error_new_literal(NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_USER_CANCELED,
                                    "Authentication canceled or unavailable");
        if (request->process)
            g_subprocess_force_exit(request->process);
    }
    request->callback(NM_SECRET_AGENT_OLD(agent), request->connection, result, error, request->data);
    if (result)
        g_variant_unref(result);
    g_clear_error(&error);
    delete request;
}
void read_reply(Request *request);
void read_ready(GObject *stream, GAsyncResult *result, gpointer data) {
    auto *request = static_cast<Request *>(data);
    GError *error = nullptr;
    GBytes *bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(stream), result, &error);
    if (!bytes) {
        g_clear_error(&error);
        complete(request, false);
        return;
    }
    gsize size = 0;
    const auto *block = static_cast<const char *>(g_bytes_get_data(bytes, &size));
    bool valid = size && request->reply.append(block, size);
    g_bytes_unref(bytes);
    if (!valid || request->reply.complete)
        complete(request, valid);
    else
        read_reply(request);
}
void read_reply(Request *request) {
    g_input_stream_read_bytes_async(g_subprocess_get_stdout_pipe(request->process), 4096, G_PRIORITY_DEFAULT,
                                    request->cancel, read_ready, request);
}
void cancel_request(LinkoraAgent *agent) {
    if (agent->request) {
        g_cancellable_cancel(agent->request->cancel);
        if (agent->request->process)
            g_subprocess_force_exit(agent->request->process);
    }
}
void get_secrets(NMSecretAgentOld *base, NMConnection *connection, const char *path, const char *setting,
                 const char **, NMSecretAgentGetSecretsFlags flags, NMSecretAgentOldGetSecretsFunc callback,
                 gpointer data) {
    auto *agent = reinterpret_cast<LinkoraAgent *>(base);
    if (agent->request || !agent->uuid || g_strcmp0(agent->uuid, nm_connection_get_uuid(connection)) ||
        g_strcmp0(setting, NM_SETTING_VPN_SETTING_NAME) ||
        !(flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION)) {
        GError *error = g_error_new_literal(NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_NO_SECRETS,
                                            "No active Linkora authentication request");
        callback(base, connection, nullptr, error, data);
        g_error_free(error);
        return;
    }
    auto *request = new Request(agent, connection, callback, data);
    request->path = path;
    agent->request = request;
    NMSettingVpn *vpn = nm_connection_get_setting_vpn(connection);
    struct Input {
        std::string text;
        bool valid = true;
    } builder;
    if (vpn)
        nm_setting_vpn_foreach_data_item(
            vpn,
            [](const char *key, const char *value, gpointer data) {
                auto &input = *static_cast<Input *>(data);
                if (!key || !value || !field(key) || !field(value)) {
                    input.valid = false;
                    return;
                }
                input.text += std::string("DATA_KEY=") + key + "\nDATA_VAL=" + value + "\n";
            },
            &builder);
    auto &input = builder.text;
    const char *username = vpn ? nm_setting_vpn_get_user_name(vpn) : nullptr;
    if (username && field(username))
        input += std::string("SECRET_KEY=form:main:username\nSECRET_VAL=") + username + "\n";
    input += "DONE\n";
    if (!vpn || !builder.valid || input.size() > 16384) {
        complete(request, false);
        return;
    }
    GError *error = nullptr;
    request->process = g_subprocess_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                                      G_SUBPROCESS_FLAGS_STDERR_SILENCE),
                                        &error, LINKORA_AUTH_DIALOG, "--uuid", agent->uuid, "--name",
                                        "Linkora VPN", "--service", Service, "--allow-interaction", nullptr);
    // The helper owns authentication forms and the system keyring. Its output is
    // a private protocol channel; never route stdout/stderr into application logs.
    if (!request->process ||
        !g_output_stream_write_all(g_subprocess_get_stdin_pipe(request->process), input.data(), input.size(),
                                   nullptr, request->cancel, &error)) {
        g_clear_error(&error);
        complete(request, false);
        return;
    }
    read_reply(request);
}
void cancel_secrets(NMSecretAgentOld *base, const char *path, const char *) {
    auto *agent = reinterpret_cast<LinkoraAgent *>(base);
    if (agent->request && agent->request->path == path)
        cancel_request(agent);
}
void save_secrets(NMSecretAgentOld *agent, NMConnection *connection, const char *,
                  NMSecretAgentOldSaveSecretsFunc callback, gpointer data) {
    // Cookies are session-only. Password storage belongs to the authentication helper's keyring UI.
    callback(agent, connection, nullptr, data);
}
void delete_secrets(NMSecretAgentOld *agent, NMConnection *connection, const char *,
                    NMSecretAgentOldDeleteSecretsFunc callback, gpointer data) {
    static const SecretSchema schema = [] {
        SecretSchema value{};
        value.name = "org.freedesktop.NetworkManager.openconnect";
        value.flags = SECRET_SCHEMA_DONT_MATCH_NAME;
        value.attributes[0] = {"vpn_uuid", SECRET_SCHEMA_ATTRIBUTE_STRING};
        return value;
    }();
    struct Cleanup {
        NMSecretAgentOld *agent;
        NMConnection *connection;
        NMSecretAgentOldDeleteSecretsFunc callback;
        gpointer data;
    };
    auto *cleanup = new Cleanup{NM_SECRET_AGENT_OLD(g_object_ref(agent)),
                                NM_CONNECTION(g_object_ref(connection)), callback, data};
    secret_password_clear(
        &schema, nullptr,
        [](GObject *, GAsyncResult *result, gpointer context) {
            auto *cleanup = static_cast<Cleanup *>(context);
            GError *error = nullptr;
            secret_password_clear_finish(result, &error);
            cleanup->callback(cleanup->agent, cleanup->connection, error, cleanup->data);
            g_clear_error(&error);
            g_object_unref(cleanup->agent);
            g_object_unref(cleanup->connection);
            delete cleanup;
        },
        cleanup, "vpn_uuid", nm_connection_get_uuid(connection), nullptr);
}
void finalize(GObject *object) {
    auto *agent = reinterpret_cast<LinkoraAgent *>(object);
    g_free(agent->uuid);
    G_OBJECT_CLASS(linkora_agent_parent_class)->finalize(object);
}
} // namespace
static void linkora_agent_class_init(LinkoraAgentClass *klass) {
    auto *agent = NM_SECRET_AGENT_OLD_CLASS(klass);
    agent->get_secrets = get_secrets;
    agent->cancel_get_secrets = cancel_secrets;
    agent->save_secrets = save_secrets;
    agent->delete_secrets = delete_secrets;
    G_OBJECT_CLASS(klass)->finalize = finalize;
}
static void linkora_agent_init(LinkoraAgent *) {}
NMSecretAgentOld *linkora_agent_new(GError **error) {
    return NM_SECRET_AGENT_OLD(
        g_initable_new(linkora_agent_get_type(), nullptr, error, NM_SECRET_AGENT_OLD_IDENTIFIER, ProductID,
                       NM_SECRET_AGENT_OLD_AUTO_REGISTER, TRUE, NM_SECRET_AGENT_OLD_CAPABILITIES,
                       NM_SECRET_AGENT_CAPABILITY_VPN_HINTS, nullptr));
}
void linkora_agent_cancel(NMSecretAgentOld *base) {
    if (!base)
        return;
    auto *agent = reinterpret_cast<LinkoraAgent *>(base);
    g_clear_pointer(&agent->uuid, g_free);
    cancel_request(agent);
}
void linkora_agent_select(NMSecretAgentOld *base, const char *uuid) {
    linkora_agent_cancel(base);
    if (base)
        reinterpret_cast<LinkoraAgent *>(base)->uuid = g_strdup(uuid);
}
