#include "connection_controller.h"
#include <utility>

namespace lansyra {
namespace {
const char *category(guint reason) {
    switch (reason) {
    case NM_VPN_CONNECTION_STATE_REASON_CONNECT_TIMEOUT:
        return "timeout";
    case NM_VPN_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED:
        return "network";
    case NM_VPN_CONNECTION_STATE_REASON_LOGIN_FAILED:
    case NM_VPN_CONNECTION_STATE_REASON_NO_SECRETS:
        return "authentication";
    case NM_VPN_CONNECTION_STATE_REASON_IP_CONFIG_INVALID:
        return "adapter";
    case NM_VPN_CONNECTION_STATE_REASON_SERVICE_START_FAILED:
    case NM_VPN_CONNECTION_STATE_REASON_CONNECTION_REMOVED:
        return "configuration";
    case NM_VPN_CONNECTION_STATE_REASON_USER_DISCONNECTED:
        return "canceled";
    default:
        return "server";
    }
}
} // namespace
ConnectionController::ConnectionController(NMClient *client, std::function<void()> changed, GError **error)
    : client_(NM_CLIENT(g_object_ref(client))), changed_(std::move(changed)) {
    agent_ = lansyra_agent_new(error);
    if (agent_)
        g_signal_connect(agent_, "notify::registered",
                         G_CALLBACK(+[](GObject *, GParamSpec *, gpointer data) {
                             static_cast<ConnectionController *>(data)->changed_();
                         }),
                         this);
    g_signal_connect(client_, "notify::nm-running", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer data) {
                         static_cast<ConnectionController *>(data)->changed_();
                     }),
                     this);
}
ConnectionController::~ConnectionController() {
    if (retry_timer_)
        g_source_remove(retry_timer_);
    lansyra_agent_cancel(agent_);
    clear_active();
    if (agent_)
        g_signal_handlers_disconnect_by_data(agent_, this);
    g_signal_handlers_disconnect_by_data(client_, this);
    g_clear_object(&agent_);
    g_clear_object(&client_);
}
bool ConnectionController::ready() const {
    return nm_client_get_nm_running(client_) && agent_ && nm_secret_agent_old_get_registered(agent_);
}
void ConnectionController::transition(const char *phase) {
    state_.phase = phase;
    state_.session = request_.value;
    changed_();
}
void ConnectionController::clear_active() {
    if (active_)
        g_signal_handlers_disconnect_by_data(active_, this);
    g_clear_object(&active_);
    deactivating_ = false;
}
void ConnectionController::deactivate() {
    if (!active_ || deactivating_)
        return;
    deactivating_ = true;
    ++pending_;
    struct Operation {
        ConnectionController *self;
        NMActiveConnection *connection;
    };
    auto *operation = new Operation{this, NM_ACTIVE_CONNECTION(g_object_ref(active_))};
    nm_client_deactivate_connection_async(
        client_, active_, nullptr,
        [](GObject *object, GAsyncResult *result, gpointer data) {
            auto *operation = static_cast<Operation *>(data);
            auto *self = operation->self;
            GError *failure = nullptr;
            const bool ok = nm_client_deactivate_connection_finish(NM_CLIENT(object), result, &failure);
            --self->pending_;
            if (nm_active_connection_get_state(operation->connection) ==
                NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
                self->ended(operation->connection);
            else if (!ok && self->active_ == operation->connection) {
                self->deactivating_ = false;
                self->state_.error = "permission";
                self->transition("disconnect_failed");
            }
            g_clear_error(&failure);
            g_object_unref(operation->connection);
            delete operation;
            self->changed_();
        },
        operation);
}
void ConnectionController::cancel() {
    request_.cancel();
    lansyra_agent_cancel(agent_);
    if (retry_timer_) {
        g_source_remove(retry_timer_);
        retry_timer_ = 0;
    }
    state_.retry_seconds = 0;
    state_.error = "canceled";
    transition(active_ || pending_ ? "disconnecting" : "idle");
    deactivate();
}
void ConnectionController::start(const std::string &profile, bool retry) {
    if (busy() || !agent_)
        return;
    state_.profile = profile;
    retry_ = retry;
    request_.begin();
    state_.attempt = 1;
    activate();
}
void ConnectionController::activate() {
    auto *profile = nm_client_get_connection_by_uuid(client_, state_.profile.c_str());
    if (!request_.wanted || !profile) {
        request_.cancel();
        state_.error = "configuration";
        transition("failed");
        return;
    }
    state_.error = "none";
    transition("connecting");
    lansyra_agent_select(agent_, state_.profile.c_str());
    ++pending_;
    struct Operation {
        ConnectionController *self;
        uint64_t token;
    };
    auto *operation = new Operation{this, request_.value};
    // Keep the reply so a late activation can be explicitly deactivated.
    nm_client_activate_connection_async(
        client_, NM_CONNECTION(profile), nullptr, nullptr, nullptr,
        [](GObject *object, GAsyncResult *result, gpointer data) {
            auto *operation = static_cast<Operation *>(data);
            auto *self = operation->self;
            GError *failure = nullptr;
            auto *connection = nm_client_activate_connection_finish(NM_CLIENT(object), result, &failure);
            --self->pending_;
            self->activation_finished(operation->token, connection);
            g_clear_error(&failure);
            delete operation;
            self->changed_();
        },
        operation);
}
void ConnectionController::activation_finished(uint64_t token, NMActiveConnection *connection) {
    if (connection) {
        active_ = connection;
        g_signal_connect(connection, "state-changed", G_CALLBACK(active_changed), this);
        if (NM_IS_VPN_CONNECTION(connection))
            g_signal_connect(connection, "vpn-state-changed", G_CALLBACK(vpn_changed), this);
        if (!request_.accepts(token))
            deactivate();
        else if (nm_active_connection_get_state(connection) == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
            ended(connection);
        else if (NM_IS_VPN_CONNECTION(connection))
            vpn_changed(NM_VPN_CONNECTION(connection),
                        nm_vpn_connection_get_vpn_state(NM_VPN_CONNECTION(connection)),
                        NM_VPN_CONNECTION_STATE_REASON_NONE, this);
    } else if (request_.accepts(token)) {
        state_.error = "configuration";
        ended();
    } else if (!request_.wanted && !active_ && !pending_) {
        // Cancellation owns the final state even when activation fails
        // without ever returning a connection to deactivate.
        ended();
    }
}
void ConnectionController::active_changed(NMActiveConnection *connection, guint state, guint, gpointer data) {
    auto *self = static_cast<ConnectionController *>(data);
    if (self->active_ == connection && state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
        self->ended(connection);
}
void ConnectionController::vpn_changed(NMVpnConnection *connection, guint phase, guint reason,
                                       gpointer data) {
    auto *self = static_cast<ConnectionController *>(data);
    if (self->active_ != NM_ACTIVE_CONNECTION(connection))
        return;
    if (!self->request_.wanted) {
        self->deactivate();
        return;
    }
    if (phase == NM_VPN_CONNECTION_STATE_ACTIVATED)
        self->transition("connected");
    else if (phase == NM_VPN_CONNECTION_STATE_NEED_AUTH)
        self->transition("authenticating");
    else if (phase == NM_VPN_CONNECTION_STATE_IP_CONFIG_GET)
        self->transition("configuring");
    else if (phase == NM_VPN_CONNECTION_STATE_FAILED || phase == NM_VPN_CONNECTION_STATE_DISCONNECTED) {
        self->state_.error = category(reason);
        self->deactivate();
    }
}
void ConnectionController::ended(NMActiveConnection *source) {
    if (source && source != active_)
        return;
    clear_active();
    lansyra_agent_cancel(agent_);
    if (request_.wanted) {
        state_.retry_seconds = retry_ ? retry_delay(state_.error, state_.attempt) : 0;
        if (state_.retry_seconds) {
            transition("retry_wait");
            retry_timer_ = g_timeout_add_seconds(
                1,
                [](gpointer data) -> gboolean {
                    auto *self = static_cast<ConnectionController *>(data);
                    if (!self->request_.wanted) {
                        self->retry_timer_ = 0;
                        return G_SOURCE_REMOVE;
                    }
                    if (--self->state_.retry_seconds == 0) {
                        self->retry_timer_ = 0;
                        ++self->state_.attempt;
                        self->activate();
                        return G_SOURCE_REMOVE;
                    }
                    self->transition("retry_wait");
                    return G_SOURCE_CONTINUE;
                },
                this);
        } else {
            request_.wanted = false;
            transition("failed");
        }
    } else
        transition("idle");
}
bool ConnectionController::restore(bool retry) {
    if (busy())
        return true;
    const auto *connections = nm_client_get_active_connections(client_);
    for (guint i = 0; i < connections->len; ++i) {
        auto *connection = NM_ACTIVE_CONNECTION(g_ptr_array_index(connections, i));
        const char *uuid = nm_active_connection_get_uuid(connection);
        auto *profile = uuid ? nm_client_get_connection_by_uuid(client_, uuid) : nullptr;
        auto *marker =
            profile ? NM_SETTING_USER(nm_connection_get_setting(NM_CONNECTION(profile), NM_TYPE_SETTING_USER))
                    : nullptr;
        if (!NM_IS_VPN_CONNECTION(connection) || !marker ||
            g_strcmp0(nm_setting_user_get_data(marker, Marker), "1") ||
            nm_active_connection_get_state(connection) == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
            continue;
        retry_ = retry;
        request_.begin();
        state_.profile = uuid;
        active_ = NM_ACTIVE_CONNECTION(g_object_ref(connection));
        lansyra_agent_select(agent_, uuid);
        g_signal_connect(connection, "state-changed", G_CALLBACK(active_changed), this);
        g_signal_connect(connection, "vpn-state-changed", G_CALLBACK(vpn_changed), this);
        transition("connecting");
        vpn_changed(NM_VPN_CONNECTION(connection),
                    nm_vpn_connection_get_vpn_state(NM_VPN_CONNECTION(connection)),
                    NM_VPN_CONNECTION_STATE_REASON_NONE, this);
        return true;
    }
    return false;
}
} // namespace lansyra
