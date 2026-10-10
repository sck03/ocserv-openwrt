#include "agent.h"
#include "protocol.h"
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <deque>
#include <sstream>

using namespace linkora;
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
const char *explanation(const std::string &error) {
    if (error == "authentication")
        return "认证失败或已取消，请检查账号、密码和证书。";
    if (error == "network")
        return "网络连接已中断，请检查当前网络。";
    if (error == "timeout")
        return "连接超时，请检查服务器和网络。";
    if (error == "adapter")
        return "系统未能配置 VPN 网络，请检查服务器下发的地址。";
    if (error == "configuration")
        return "请检查服务器配置及 OpenConnect 系统插件。";
    return "系统已结束连接；可重新连接或导出诊断信息。";
}
class Application {
public:
    GtkApplication *app;
    GtkWidget *window = nullptr, *servers = nullptr, *status = nullptr, *detail = nullptr,
              *connectButton = nullptr;
    GtkWidget *newButton = nullptr, *editButton = nullptr, *deleteButton = nullptr, *login = nullptr,
              *retryCheck = nullptr;
    NMClient *client = nullptr;
    NMSecretAgentOld *agent = nullptr;
    NMActiveConnection *active = nullptr;
    Generation request;
    uint64_t stateGeneration = 0;
    unsigned pending = 0, attempt = 1, retrySeconds = 0;
    guint retryTimer = 0;
    bool closing = false, startup, smoke, refreshing = false, deactivating = false;
    bool retryFailed = false;
    std::string selectedID, autoID, targetID, state = "idle", error = "none";
    std::deque<std::string> events;

    Application(GtkApplication *application, bool atLogin, bool test)
        : app(application), startup(atLogin), smoke(test) {}
    ~Application() {
        if (retryTimer)
            g_source_remove(retryTimer);
        linkora_agent_cancel(agent);
        clearActive();
        g_clear_object(&agent);
        g_clear_object(&client);
    }
    std::string settingsPath() const {
        return std::string(g_get_user_config_dir()) + "/linkora-vpn/settings.ini";
    }
    std::string startupPath() const {
        return std::string(g_get_user_config_dir()) + "/autostart/io.github.sck03.linkoravpn.desktop";
    }
    void transition(const char *phase, const char *label, const char *description = "") {
        state = phase;
        ++stateGeneration;
        gtk_label_set_text(GTK_LABEL(status), label);
        gtk_label_set_text(GTK_LABEL(detail), description);
        GDateTime *now = g_date_time_new_now_utc();
        char *time = g_date_time_format_iso8601(now);
        events.push_back(std::string(time) + " session=" + std::to_string(request.value) + " generation=" +
                         std::to_string(stateGeneration) + " state=" + phase + " error=" + error);
        g_free(time);
        g_date_time_unref(now);
        if (events.size() > 256)
            events.pop_front();
        update();
    }
    void update() {
        bool busy = request.wanted || active || pending;
        bool available =
            client && nm_client_get_nm_running(client) && agent && nm_secret_agent_old_get_registered(agent);
        gtk_button_set_label(GTK_BUTTON(connectButton), busy ? "断开 / 取消" : "连接");
        gtk_widget_set_sensitive(connectButton, !closing && (busy || (available && selected())));
        for (auto *widget : {servers, newButton, editButton, deleteButton, login, retryCheck})
            gtk_widget_set_sensitive(widget, !busy && !closing && (available || smoke));
        gtk_widget_set_sensitive(editButton, !busy && !closing && selected());
        gtk_widget_set_sensitive(deleteButton, !busy && !closing && selected());
        gtk_widget_set_sensitive(login, !busy && !closing && (selected() || !autoID.empty()));
    }
    NMRemoteConnection *selected() const {
        if (!client)
            return nullptr;
        const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(servers));
        return id ? nm_client_get_connection_by_uuid(client, id) : nullptr;
    }
    void loadPreferences() {
        GKeyFile *settings = g_key_file_new();
        GError *failure = nullptr;
        if (g_key_file_load_from_file(settings, settingsPath().c_str(), G_KEY_FILE_NONE, &failure)) {
            auto read = [&](const char *key) {
                char *value = g_key_file_get_string(settings, "client", key, nullptr);
                std::string result = value ? value : "";
                g_free(value);
                return result;
            };
            selectedID = read("selected");
            autoID = read("auto_connect");
            retryFailed = g_key_file_get_boolean(settings, "client", "retry_failed", nullptr);
        }
        if (failure && !g_error_matches(failure, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            gtk_label_set_text(GTK_LABEL(detail), "设置文件无效，请检查配置目录。不会自动连接。");
        g_clear_error(&failure);
        g_key_file_unref(settings);
    }
    bool savePreferences() {
        std::string directory = std::string(g_get_user_config_dir()) + "/linkora-vpn";
        if (g_mkdir_with_parents(directory.c_str(), 0700))
            return false;
        GKeyFile *settings = g_key_file_new();
        g_key_file_set_string(settings, "client", "selected", selectedID.c_str());
        g_key_file_set_string(settings, "client", "auto_connect", autoID.c_str());
        g_key_file_set_boolean(settings, "client", "retry_failed", retryFailed);
        gsize size = 0;
        char *text = g_key_file_to_data(settings, &size, nullptr);
        bool ok = g_file_set_contents_full(settingsPath().c_str(), text, static_cast<gssize>(size),
                                           G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr);
        g_free(text);
        g_key_file_unref(settings);
        return ok;
    }
    void setLogin() {
        if (refreshing)
            return;
        bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(login));
        const auto previous = autoID;
        auto *profile = selected();
        autoID = enabled && profile ? nm_connection_get_uuid(NM_CONNECTION(profile)) : "";
        bool ok = savePreferences();
        if (ok && enabled) {
            std::string directory = std::string(g_get_user_config_dir()) + "/autostart";
            constexpr char entry[] = "[Desktop Entry]\nType=Application\nName=Linkora "
                                     "VPN\nExec=/usr/bin/linkora-vpn --autoconnect\nTerminal=false\n";
            ok = g_mkdir_with_parents(directory.c_str(), 0700) == 0 &&
                 g_file_set_contents_full(startupPath().c_str(), entry, -1, G_FILE_SET_CONTENTS_CONSISTENT,
                                          0600, nullptr);
        } else if (ok)
            ok = g_unlink(startupPath().c_str()) == 0 || errno == ENOENT;
        if (!ok) {
            autoID = previous;
            savePreferences();
            gtk_label_set_text(GTK_LABEL(detail), "无法保存登录后自动连接设置，请检查目录权限。");
        }
        refreshing = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(login), !autoID.empty());
        refreshing = false;
    }
    void reload() {
        if (!client)
            return;
        refreshing = true;
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(servers));
        const GPtrArray *profiles = nm_client_get_connections(client);
        for (guint i = 0; i < profiles->len; ++i) {
            auto *connection = NM_CONNECTION(g_ptr_array_index(profiles, i));
            auto *vpn = nm_connection_get_setting_vpn(connection);
            auto *user = nm_connection_get_setting_user(connection);
            if (!vpn || g_strcmp0(nm_setting_vpn_get_service_type(vpn), Service) || !user ||
                g_strcmp0(nm_setting_user_get_data(user, Marker), "1"))
                continue;
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(servers), nm_connection_get_uuid(connection),
                                      nm_connection_get_id(connection));
        }
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(servers), selectedID.c_str()))
            gtk_combo_box_set_active(GTK_COMBO_BOX(servers), 0);
        if (const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(servers)))
            selectedID = id;
        refreshing = false;
        update();
    }
    void clearActive() {
        if (active)
            g_signal_handlers_disconnect_by_data(active, this);
        g_clear_object(&active);
        deactivating = false;
    }
    void deactivate() {
        if (!active || deactivating)
            return;
        deactivating = true;
        ++pending;
        struct Deactivation {
            Application *self;
            NMActiveConnection *connection;
        };
        auto *operation = new Deactivation{this, NM_ACTIVE_CONNECTION(g_object_ref(active))};
        nm_client_deactivate_connection_async(
            client, active, nullptr,
            [](GObject *object, GAsyncResult *result, gpointer data) {
                auto *operation = static_cast<Deactivation *>(data);
                auto *self = operation->self;
                GError *failure = nullptr;
                nm_client_deactivate_connection_finish(NM_CLIENT(object), result, &failure);
                g_clear_error(&failure);
                --self->pending;
                if (nm_active_connection_get_state(operation->connection) ==
                    NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
                    self->ended(operation->connection);
                g_object_unref(operation->connection);
                delete operation;
                self->maybeClose();
            },
            operation);
    }
    void cancel() {
        request.cancel();
        linkora_agent_cancel(agent);
        if (retryTimer) {
            g_source_remove(retryTimer);
            retryTimer = 0;
        }
        retrySeconds = 0;
        error = "canceled";
        transition(active || pending ? "disconnecting" : "idle",
                   active || pending ? "正在清理连接…" : "未连接");
        deactivate();
    }
    void connect() {
        if (request.wanted || active || pending) {
            cancel();
            return;
        }
        auto *profile = selected();
        if (!profile || !agent)
            return;
        targetID = nm_connection_get_uuid(NM_CONNECTION(profile));
        selectedID = targetID;
        savePreferences();
        request.begin();
        attempt = 1;
        activate();
    }
    void activate() {
        auto *profile = nm_client_get_connection_by_uuid(client, targetID.c_str());
        if (!request.wanted || !profile) {
            request.cancel();
            error = "configuration";
            transition("failed", "配置已不存在");
            return;
        }
        error = "none";
        transition("connecting", "正在连接…", "请在系统认证窗口中核对证书并完成登录。");
        linkora_agent_select(agent, targetID.c_str());
        ++pending;
        struct Activation {
            Application *self;
            uint64_t token;
        };
        auto *operation = new Activation{this, request.value};
        // Do not cancel the D-Bus reply: cancellation must reap a late activation
        // and explicitly deactivate its own connection instead of losing its handle.
        nm_client_activate_connection_async(
            client, NM_CONNECTION(profile), nullptr, nullptr, nullptr,
            [](GObject *object, GAsyncResult *result, gpointer data) {
                auto *operation = static_cast<Activation *>(data);
                auto *self = operation->self;
                GError *failure = nullptr;
                auto *connection = nm_client_activate_connection_finish(NM_CLIENT(object), result, &failure);
                --self->pending;
                if (connection) {
                    self->active = connection;
                    g_signal_connect(connection, "state-changed", G_CALLBACK(activeChanged), self);
                    if (NM_IS_VPN_CONNECTION(connection))
                        g_signal_connect(connection, "vpn-state-changed", G_CALLBACK(vpnChanged), self);
                    if (!self->request.accepts(operation->token) || self->closing)
                        self->deactivate();
                    else if (nm_active_connection_get_state(connection) ==
                             NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
                        self->ended(connection);
                } else if (self->request.accepts(operation->token)) {
                    self->error = "configuration";
                    self->ended();
                }
                g_clear_error(&failure);
                delete operation;
                self->update();
                self->maybeClose();
            },
            operation);
    }
    static void activeChanged(NMActiveConnection *connection, guint state, guint, gpointer data) {
        auto *self = static_cast<Application *>(data);
        if (self->active != connection)
            return;
        if (state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED)
            self->ended(connection);
    }
    static void vpnChanged(NMVpnConnection *connection, guint phase, guint reason, gpointer data) {
        auto *self = static_cast<Application *>(data);
        if (self->active != NM_ACTIVE_CONNECTION(connection))
            return;
        if (!self->request.wanted) {
            self->deactivate();
            return;
        }
        if (phase == NM_VPN_CONNECTION_STATE_ACTIVATED)
            self->transition("connected", "已连接");
        else if (phase == NM_VPN_CONNECTION_STATE_NEED_AUTH)
            self->transition("authenticating", "等待身份认证…");
        else if (phase == NM_VPN_CONNECTION_STATE_IP_CONFIG_GET)
            self->transition("configuring", "正在配置 VPN 网络…");
        else if (phase == NM_VPN_CONNECTION_STATE_FAILED || phase == NM_VPN_CONNECTION_STATE_DISCONNECTED) {
            self->error = category(reason);
            self->deactivate();
        }
    }
    void ended(NMActiveConnection *source = nullptr) {
        if (source && source != active)
            return;
        clearActive();
        linkora_agent_cancel(agent);
        if (request.wanted && !closing) {
            unsigned delay = retryFailed ? retry_delay(error, attempt) : 0;
            if (delay) {
                retrySeconds = delay;
                transition("retry_wait", "网络连接失败，等待重试…");
                retryTimer = g_timeout_add_seconds(
                    1,
                    [](gpointer data) -> gboolean {
                        auto *self = static_cast<Application *>(data);
                        if (!self->request.wanted || self->closing) {
                            self->retryTimer = 0;
                            return G_SOURCE_REMOVE;
                        }
                        if (--self->retrySeconds == 0) {
                            self->retryTimer = 0;
                            ++self->attempt;
                            self->activate();
                            return G_SOURCE_REMOVE;
                        }
                        auto text = std::to_string(self->retrySeconds) + " 秒后重试（" +
                                    std::to_string(self->attempt) + "/3）";
                        gtk_label_set_text(GTK_LABEL(self->status), text.c_str());
                        return G_SOURCE_CONTINUE;
                    },
                    this);
            } else {
                request.wanted = false;
                transition("failed", "连接已结束", explanation(error));
            }
        } else
            transition("idle", "未连接");
        maybeClose();
    }
    void maybeClose() {
        if (closing && !pending && !active) {
            gtk_widget_destroy(window);
            window = nullptr;
            g_application_quit(G_APPLICATION(app));
        } else if (window)
            update();
    }
    void edit(bool existing) {
        auto *remote = existing ? selected() : nullptr;
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            existing ? "编辑服务器" : "新建服务器", GTK_WINDOW(window), GTK_DIALOG_MODAL, "取消",
            GTK_RESPONSE_CANCEL, "保存", GTK_RESPONSE_OK, nullptr);
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 0);
        GtkWidget *entries[4];
        const char *labels[] = {"名称", "HTTPS 服务器", "用户名（可选）", "CA 证书文件（可选）"};
        for (int i = 0; i < 4; ++i) {
            entries[i] = gtk_entry_new();
            gtk_entry_set_max_length(GTK_ENTRY(entries[i]), i ? 4096 : 128);
            gtk_widget_set_size_request(entries[i], 340, -1);
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[i]), 0, i, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), entries[i], 1, i, 1, 1);
        }
        if (remote) {
            auto *vpn = nm_connection_get_setting_vpn(NM_CONNECTION(remote));
            const char *values[] = {
                nm_connection_get_id(NM_CONNECTION(remote)), nm_setting_vpn_get_data_item(vpn, "gateway"),
                nm_setting_vpn_get_user_name(vpn), nm_setting_vpn_get_data_item(vpn, "cacert")};
            for (int i = 0; i < 4; ++i)
                gtk_entry_set_text(GTK_ENTRY(entries[i]), values[i] ? values[i] : "");
        }
        gtk_widget_show_all(dialog);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
            gtk_widget_destroy(dialog);
            return;
        }
        std::string name = gtk_entry_get_text(GTK_ENTRY(entries[0]));
        auto server = gateway(gtk_entry_get_text(GTK_ENTRY(entries[1])));
        std::string username = gtk_entry_get_text(GTK_ENTRY(entries[2])),
                    ca = gtk_entry_get_text(GTK_ENTRY(entries[3]));
        gtk_widget_destroy(dialog);
        if (name.empty() || !field(name) || server.empty() || !field(username) ||
            (!ca.empty() && !g_file_test(ca.c_str(), G_FILE_TEST_IS_REGULAR))) {
            gtk_label_set_text(GTK_LABEL(detail), "请检查名称、HTTPS 地址、用户名及 CA 文件。");
            return;
        }
        auto *connection = nm_simple_connection_new();
        auto *setting = NM_SETTING_CONNECTION(nm_setting_connection_new());
        char *uuid =
            remote ? g_strdup(nm_connection_get_uuid(NM_CONNECTION(remote))) : g_uuid_string_random();
        selectedID = uuid;
        g_object_set(setting, NM_SETTING_CONNECTION_ID, name.c_str(), NM_SETTING_CONNECTION_UUID, uuid,
                     NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME,
                     NM_SETTING_CONNECTION_AUTOCONNECT, FALSE, nullptr);
        nm_setting_connection_add_permission(setting, "user", g_get_user_name(), nullptr);
        nm_connection_add_setting(connection, NM_SETTING(setting));
        auto *vpn = NM_SETTING_VPN(nm_setting_vpn_new());
        g_object_set(vpn, NM_SETTING_VPN_SERVICE_TYPE, Service, NM_SETTING_VPN_USER_NAME, username.c_str(),
                     nullptr);
        nm_setting_vpn_add_data_item(vpn, "gateway", server.c_str());
        nm_setting_vpn_add_data_item(vpn, "protocol", "anyconnect");
        if (!ca.empty())
            nm_setting_vpn_add_data_item(vpn, "cacert", ca.c_str());
        for (const char *key : {"cookie", "gateway", "gwcert", "resolve"})
            nm_setting_set_secret_flags(NM_SETTING(vpn), key, NM_SETTING_SECRET_FLAG_NOT_SAVED, nullptr);
        nm_connection_add_setting(connection, NM_SETTING(vpn));
        auto *user = NM_SETTING_USER(nm_setting_user_new());
        nm_setting_user_set_data(user, Marker, "1", nullptr);
        nm_connection_add_setting(connection, NM_SETTING(user));
        for (NMSetting *ip : {nm_setting_ip4_config_new(), nm_setting_ip6_config_new()}) {
            g_object_set(ip, NM_SETTING_IP_CONFIG_METHOD, "auto", NM_SETTING_IP_CONFIG_DNS_PRIORITY, -50,
                         nullptr);
            nm_connection_add_setting(connection, ip);
        }
        ++pending;
        if (remote) {
            nm_connection_replace_settings_from_connection(NM_CONNECTION(remote), connection);
            nm_remote_connection_commit_changes_async(
                remote, TRUE, nullptr,
                [](GObject *object, GAsyncResult *result, gpointer data) {
                    auto *self = static_cast<Application *>(data);
                    GError *failure = nullptr;
                    bool ok = nm_remote_connection_commit_changes_finish(NM_REMOTE_CONNECTION(object), result,
                                                                         &failure);
                    g_clear_error(&failure);
                    self->saved(ok);
                },
                this);
        } else
            nm_client_add_connection_async(
                client, connection, TRUE, nullptr,
                [](GObject *object, GAsyncResult *result, gpointer data) {
                    auto *self = static_cast<Application *>(data);
                    GError *failure = nullptr;
                    auto *profile = nm_client_add_connection_finish(NM_CLIENT(object), result, &failure);
                    bool ok = profile != nullptr;
                    g_clear_object(&profile);
                    g_clear_error(&failure);
                    self->saved(ok);
                },
                this);
        g_free(uuid);
        g_object_unref(connection);
        update();
    }
    void saved(bool ok) {
        --pending;
        if (!closing) {
            reload();
            if (ok)
                savePreferences();
            gtk_label_set_text(GTK_LABEL(detail), ok ? "配置已保存" : "无法保存配置，请检查系统授权。");
        }
        maybeClose();
    }
    void remove() {
        auto *profile = selected();
        if (!profile)
            return;
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_OK_CANCEL, "删除所选服务器配置？");
        bool confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(dialog);
        if (!confirmed)
            return;
        ++pending;
        if (autoID == nm_connection_get_uuid(NM_CONNECTION(profile))) {
            autoID.clear();
            savePreferences();
            g_unlink(startupPath().c_str());
            refreshing = true;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(login), FALSE);
            refreshing = false;
        }
        nm_remote_connection_delete_async(
            profile, nullptr,
            [](GObject *object, GAsyncResult *result, gpointer data) {
                auto *self = static_cast<Application *>(data);
                GError *failure = nullptr;
                bool ok = nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(object), result, &failure);
                g_clear_error(&failure);
                self->saved(ok);
            },
            this);
        update();
    }
    void exportDiagnostics() {
        GtkWidget *dialog =
            gtk_file_chooser_dialog_new("导出诊断信息", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                        "取消", GTK_RESPONSE_CANCEL, "保存", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "LinkoraVPN-diagnostics.txt");
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            std::ostringstream report;
            report << "Linkora VPN 0.1.0\nPlatform: Linux\nNetworkManager: "
                   << (client ? nm_client_get_version(client) : "unavailable")
                   << "\nSession: " << request.value << "\nGeneration: " << stateGeneration
                   << "\nState: " << state << "\nError: " << error << "\nAttempt: " << attempt << "\n\n";
            for (const auto &event : events)
                report << event << '\n';
            if (!path || !g_file_set_contents_full(path, report.str().c_str(), -1,
                                                   G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr))
                gtk_label_set_text(GTK_LABEL(detail), "无法写入诊断文件。");
            g_free(path);
        }
        gtk_widget_destroy(dialog);
    }
    void create() {
        window = gtk_application_window_new(app);
        gtk_window_set_title(GTK_WINDOW(window), "Linkora VPN");
        gtk_window_set_default_size(GTK_WINDOW(window), 540, 330);
        gtk_window_set_icon_name(GTK_WINDOW(window), ProductID);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_container_set_border_width(GTK_CONTAINER(box), 24);
        gtk_container_add(GTK_CONTAINER(window), box);
        GtkWidget *title = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Linkora VPN</span>");
        gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);
        servers = gtk_combo_box_text_new();
        gtk_box_pack_start(GTK_BOX(box), servers, FALSE, FALSE, 0);
        GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        newButton = gtk_button_new_with_label("新建服务器");
        editButton = gtk_button_new_with_label("编辑");
        deleteButton = gtk_button_new_with_label("删除");
        for (auto *button : {newButton, editButton, deleteButton})
            gtk_box_pack_start(GTK_BOX(buttons), button, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
        login = gtk_check_button_new_with_label("登录后自动连接所选服务器");
        retryCheck = gtk_check_button_new_with_label("网络失败后重试（最多 3 次）");
        gtk_box_pack_start(GTK_BOX(box), login, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), retryCheck, FALSE, FALSE, 0);
        status = gtk_label_new("未连接");
        detail = gtk_label_new("账号密码由系统认证窗口处理，可在该窗口选择保存到钥匙串。");
        gtk_label_set_line_wrap(GTK_LABEL(detail), TRUE);
        gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), detail, TRUE, TRUE, 0);
        connectButton = gtk_button_new_with_label("连接");
        GtkWidget *exportButton = gtk_button_new_with_label("导出诊断…");
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(actions), connectButton, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(actions), exportButton, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);
        loadPreferences();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(login), !autoID.empty());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(retryCheck), retryFailed);
        g_signal_connect(
            newButton, "clicked",
            G_CALLBACK((+[](GtkButton *, gpointer data) { static_cast<Application *>(data)->edit(false); })),
            this);
        g_signal_connect(
            editButton, "clicked",
            G_CALLBACK((+[](GtkButton *, gpointer data) { static_cast<Application *>(data)->edit(true); })),
            this);
        g_signal_connect(
            deleteButton, "clicked",
            G_CALLBACK((+[](GtkButton *, gpointer data) { static_cast<Application *>(data)->remove(); })),
            this);
        g_signal_connect(
            connectButton, "clicked",
            G_CALLBACK((+[](GtkButton *, gpointer data) { static_cast<Application *>(data)->connect(); })),
            this);
        g_signal_connect(exportButton, "clicked", G_CALLBACK((+[](GtkButton *, gpointer data) {
                             static_cast<Application *>(data)->exportDiagnostics();
                         })),
                         this);
        g_signal_connect(login, "toggled", G_CALLBACK((+[](GtkToggleButton *, gpointer data) {
                             static_cast<Application *>(data)->setLogin();
                         })),
                         this);
        g_signal_connect(retryCheck, "toggled", G_CALLBACK((+[](GtkToggleButton *button, gpointer data) {
                             auto *self = static_cast<Application *>(data);
                             self->retryFailed = gtk_toggle_button_get_active(button);
                             self->savePreferences();
                         })),
                         this);
        g_signal_connect(servers, "changed", G_CALLBACK((+[](GtkComboBox *box, gpointer data) {
                             auto *self = static_cast<Application *>(data);
                             if (!self->refreshing) {
                                 const char *id = gtk_combo_box_get_active_id(box);
                                 self->selectedID = id ? id : "";
                                 self->savePreferences();
                                 self->update();
                             }
                         })),
                         this);
        g_signal_connect(window, "delete-event",
                         G_CALLBACK((+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                             auto *self = static_cast<Application *>(data);
                             self->closing = true;
                             self->cancel();
                             self->maybeClose();
                             return TRUE;
                         })),
                         this);
        gtk_widget_show_all(window);
        if (smoke) {
            update();
            g_timeout_add(
                800,
                [](gpointer data) -> gboolean {
                    auto *self = static_cast<Application *>(data);
                    GtkAllocation size;
                    gtk_widget_get_allocation(self->window, &size);
                    GdkPixbuf *image = gdk_pixbuf_get_from_window(gtk_widget_get_window(self->window), 0, 0,
                                                                  size.width, size.height);
                    g_mkdir_with_parents("test-results", 0700);
                    if (image) {
                        gdk_pixbuf_save(image, "test-results/linux-window.png", "png", nullptr, nullptr);
                        g_object_unref(image);
                    }
                    self->closing = true;
                    self->maybeClose();
                    return G_SOURCE_REMOVE;
                },
                this);
            return;
        }
        GError *failure = nullptr;
        client = nm_client_new(nullptr, &failure);
        if (client)
            agent = linkora_agent_new(&failure);
        if (!client || !agent || !nm_client_get_nm_running(client)) {
            error = "configuration";
            transition("failed", "系统 VPN 服务不可用",
                       "请启动 NetworkManager，并安装 network-manager-openconnect-gnome。");
        } else {
            reload();
            g_signal_connect(client, "connection-added",
                             G_CALLBACK((+[](NMClient *, NMRemoteConnection *, gpointer data) {
                                 static_cast<Application *>(data)->reload();
                             })),
                             this);
            g_signal_connect(client, "connection-removed",
                             G_CALLBACK((+[](NMClient *, NMRemoteConnection *, gpointer data) {
                                 static_cast<Application *>(data)->reload();
                             })),
                             this);
            g_signal_connect(agent, "notify::registered",
                             G_CALLBACK((+[](GObject *, GParamSpec *, gpointer data) {
                                 static_cast<Application *>(data)->update();
                             })),
                             this);
            if (startup && !autoID.empty() &&
                gtk_combo_box_set_active_id(GTK_COMBO_BOX(servers), autoID.c_str()))
                connect();
        }
        g_clear_error(&failure);
        update();
    }
};
} // namespace
int main(int argc, char **argv) {
    bool startup = argc == 2 && !strcmp(argv[1], "--autoconnect");
    bool smoke = argc == 2 && !strcmp(argv[1], "--smoke-test");
    if (argc > 1 && !startup && !smoke)
        return 2;
    GtkApplication *app = gtk_application_new(ProductID, G_APPLICATION_DEFAULT_FLAGS);
    Application window(app, startup, smoke);
    g_signal_connect(app, "activate", G_CALLBACK((+[](GtkApplication *, gpointer data) {
                         auto *self = static_cast<Application *>(data);
                         if (self->window)
                             gtk_window_present(GTK_WINDOW(self->window));
                         else
                             self->create();
                     })),
                     &window);
    int result = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return result;
}
