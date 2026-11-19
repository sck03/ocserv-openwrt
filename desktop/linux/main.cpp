#include "connection_controller.h"
#include "profile_editor.h"
#include "preferences.h"
#include "protocol.h"
#include "profile_settings.h"
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <cstdio>
#include <deque>
#include <sstream>
#include <memory>

using namespace lansyra;
namespace {
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
    GtkWidget *newButton = nullptr, *editButton = nullptr, *deleteButton = nullptr, *importButton = nullptr,
              *login = nullptr, *retryCheck = nullptr;
    NMClient *client = nullptr;
    std::unique_ptr<ConnectionController> connection;
    uint64_t stateGeneration = 0;
    unsigned storagePending = 0;
    PreferencesRepository preferences{g_get_user_config_dir()};
    unsigned pending = 0;
    bool closing = false, startup, smoke, refreshing = false;
    bool retryFailed = false;
    std::string selectedID, autoID, state = "idle", error = "none";
    std::deque<std::string> events;

    Application(GtkApplication *application, bool atLogin, bool test)
        : app(application), startup(atLogin), smoke(test) {}
    ~Application() {
        connection.reset();
        if (client)
            g_signal_handlers_disconnect_by_data(client, this);
        g_clear_object(&client);
    }
    uint64_t session_id() const {
        return connection ? connection->state().session : 0;
    }
    void transition(const char *phase, const char *label, const char *description = "") {
        state = phase;
        ++stateGeneration;
        gtk_label_set_text(GTK_LABEL(status), label);
        gtk_label_set_text(GTK_LABEL(detail), description);
        GDateTime *now = g_date_time_new_now_utc();
        char *time = g_date_time_format_iso8601(now);
        events.push_back(std::string(time) + " session=" + std::to_string(session_id()) + " generation=" +
                         std::to_string(stateGeneration) + " state=" + phase + " error=" + error);
        g_free(time);
        g_date_time_unref(now);
        if (events.size() > 256)
            events.pop_front();
        update();
    }
    void update() {
        const bool connecting = connection && connection->busy();
        const bool busy = connecting || pending;
        const bool hasProfile = selected() != nullptr;
        const bool available = connection && connection->ready();
        gtk_button_set_label(GTK_BUTTON(connectButton), connecting ? "断开 / 取消" : "连接");
        gtk_widget_set_sensitive(connectButton,
                                 !closing && !pending &&
                                     (connecting || (!storagePending && available && hasProfile)));
        for (auto *widget : {servers, newButton, editButton, deleteButton, importButton, login, retryCheck})
            gtk_widget_set_sensitive(widget, !busy && !storagePending && !closing && (available || smoke));
        gtk_widget_set_sensitive(editButton, !busy && !storagePending && !closing && hasProfile);
        gtk_widget_set_sensitive(deleteButton, !busy && !storagePending && !closing && hasProfile);
        gtk_widget_set_sensitive(login,
                                 !busy && !storagePending && !closing && (hasProfile || !autoID.empty()));
        std::string loginLabel = "登录后自动连接所选服务器";
        if (client && !autoID.empty()) {
            if (auto *profile = nm_client_get_connection_by_uuid(client, autoID.c_str()))
                loginLabel = std::string("登录后自动连接：") + nm_connection_get_id(NM_CONNECTION(profile));
        }
        gtk_button_set_label(GTK_BUTTON(login), loginLabel.c_str());
        if (startup && available && !busy && !storagePending && !closing) {
            startup = false;
            if (!autoID.empty() && gtk_combo_box_set_active_id(GTK_COMBO_BOX(servers), autoID.c_str()))
                connect();
        }
    }
    NMRemoteConnection *selected() const {
        if (!client)
            return nullptr;
        const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(servers));
        return id ? nm_client_get_connection_by_uuid(client, id) : nullptr;
    }
    void loadPreferences() {
        Preferences value;
        if (!preferences.load(value))
            gtk_label_set_text(GTK_LABEL(detail), "设置文件无效，请检查配置目录。不会自动连接。");
        selectedID = value.selected;
        autoID = value.automatic;
        retryFailed = value.retry;
    }
    void savePreferences(std::function<void(bool)> completed = {}) {
        ++storagePending;
        preferences.save(
            {selectedID, autoID, retryFailed}, [this, completed = std::move(completed)](bool ok) {
                --storagePending;
                if (!ok && !closing)
                    gtk_label_set_text(GTK_LABEL(detail), "无法保存偏好，请检查配置目录权限和剩余空间。");
                if (completed)
                    completed(ok);
                update();
                maybeClose();
            });
        update();
    }
    void setLogin() {
        if (refreshing)
            return;
        const auto previous = autoID;
        auto *profile = selected();
        autoID = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(login)) && profile
                     ? nm_connection_get_uuid(NM_CONNECTION(profile))
                     : "";
        savePreferences([this, previous](bool ok) {
            if (!ok)
                autoID = previous;
            refreshing = true;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(login), !autoID.empty());
            refreshing = false;
        });
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
            auto *user = NM_SETTING_USER(nm_connection_get_setting(connection, NM_TYPE_SETTING_USER));
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
    void connectionChanged() {
        if (!connection || !window)
            return;
        const auto current = connection->state();
        error = current.error;
        std::string label, description;
        if (current.phase == "connected")
            label = "已连接";
        else if (current.phase == "connecting") {
            label = "正在连接…";
            description = "请在系统认证窗口中核对证书并完成登录。";
        } else if (current.phase == "authenticating")
            label = "等待身份认证…";
        else if (current.phase == "configuring")
            label = "正在配置 VPN 网络…";
        else if (current.phase == "disconnecting")
            label = "正在清理连接…";
        else if (current.phase == "retry_wait")
            label = std::to_string(current.retry_seconds) + " 秒后重试（" + std::to_string(current.attempt) +
                    "/3）";
        else if (current.phase == "disconnect_failed") {
            closing = false;
            label = "VPN 尚未断开";
            description = "系统未能断开 VPN。请重试，或在系统网络设置中断开后退出。";
        } else if (current.phase == "failed") {
            label = "连接已结束";
            description = explanation(error);
        } else
            label = "未连接";
        transition(current.phase.c_str(), label.c_str(), description.c_str());
        maybeClose();
    }
    void cancel() {
        if (connection)
            connection->cancel();
        else
            transition("idle", "未连接");
    }
    void connect() {
        if (closing || pending || storagePending || !connection)
            return;
        if (connection->busy()) {
            cancel();
            return;
        }
        auto *profile = selected();
        if (!profile || !connection->ready())
            return;
        selectedID = nm_connection_get_uuid(NM_CONNECTION(profile));
        savePreferences();
        connection->start(selectedID, retryFailed);
    }
    void maybeClose() {
        if (!window)
            return;
        if (closing && !pending && !storagePending && (!connection || !connection->busy())) {
            gtk_widget_destroy(window);
            window = nullptr;
            g_application_quit(G_APPLICATION(app));
        } else if (window)
            update();
    }
    void edit(bool existing, std::shared_ptr<ImportedProfile> imported = {}) {
        auto *selected_profile = existing ? selected() : nullptr;
        g_autoptr(NMRemoteConnection) remote =
            selected_profile ? NM_REMOTE_CONNECTION(g_object_ref(selected_profile)) : nullptr;
        auto draft =
            edit_profile(GTK_WINDOW(window), remote ? NM_CONNECTION(remote) : nullptr, imported.get());
        if (!draft)
            return;
        auto *connection = profile_candidate(remote ? NM_CONNECTION(remote) : nullptr, draft->name,
                                             draft->server, draft->username, draft->ca);
        const char *uuid = nm_connection_get_uuid(connection);
        const std::string previousID = selectedID;
        selectedID = uuid;
        if (imported && draft->ca != imported->ca)
            imported.reset();
        ++pending;
        struct SaveOperation {
            Application *self;
            std::string previousID, id, ca;
            std::shared_ptr<ImportedProfile> imported;
            void finish(bool ok) {
                if (ok && imported)
                    imported->retained = true;
                if (!ok)
                    self->selectedID = previousID;
                if (ok)
                    prune_profile_certificates(nm_client_get_connections(self->client), id, ca);
                auto *owner = self;
                delete this;
                owner->saved(ok);
            }
        };
        auto *operation = new SaveOperation{this, previousID, uuid, draft->ca, std::move(imported)};
        if (remote) {
            // Send an immutable candidate. Failed authorization must not mutate
            // the cached remote connection the UI will use for its next attempt.
            GVariant *settings = nm_connection_to_dbus(connection, NM_CONNECTION_SERIALIZE_ALL);
            nm_remote_connection_update2(
                remote, settings, NM_SETTINGS_UPDATE2_FLAG_TO_DISK, nullptr, nullptr,
                [](GObject *object, GAsyncResult *result, gpointer data) {
                    auto *operation = static_cast<SaveOperation *>(data);
                    GError *failure = nullptr;
                    GVariant *reply =
                        nm_remote_connection_update2_finish(NM_REMOTE_CONNECTION(object), result, &failure);
                    bool ok = reply != nullptr;
                    if (reply)
                        g_variant_unref(reply);
                    g_clear_error(&failure);
                    operation->finish(ok);
                },
                operation);
            g_variant_unref(settings);
        } else
            nm_client_add_connection_async(
                client, connection, TRUE, nullptr,
                [](GObject *object, GAsyncResult *result, gpointer data) {
                    auto *operation = static_cast<SaveOperation *>(data);
                    GError *failure = nullptr;
                    auto *profile = nm_client_add_connection_finish(NM_CLIENT(object), result, &failure);
                    bool ok = profile != nullptr;
                    g_clear_object(&profile);
                    g_clear_error(&failure);
                    operation->finish(ok);
                },
                operation);
        g_object_unref(connection);
        update();
    }
    void saved(bool ok) {
        --pending;
        if (!closing) {
            reload();
            if (!ok)
                gtk_label_set_text(GTK_LABEL(detail), "无法保存配置，请检查系统授权。");
            else
                savePreferences([this](bool saved) {
                    if (saved && !closing)
                        gtk_label_set_text(GTK_LABEL(detail), "配置已保存");
                });
        }
        maybeClose();
    }
    void importProfile() {
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "导入 .vpn 公共配置", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN, "取消",
            GTK_RESPONSE_CANCEL, "导入", GTK_RESPONSE_ACCEPT, nullptr);
        auto *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Lansyra .vpn");
        gtk_file_filter_add_pattern(filter, "*.vpn");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        char *path = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT
                         ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))
                         : nullptr;
        gtk_widget_destroy(dialog);
        if (!path)
            return;
        ++pending;
        update();
        GTask *task = g_task_new(
            nullptr, nullptr,
            [](GObject *, GAsyncResult *result, gpointer data) {
                auto *self = static_cast<Application *>(data);
                GError *error = nullptr;
                std::shared_ptr<ImportedProfile> imported(
                    static_cast<ImportedProfile *>(g_task_propagate_pointer(G_TASK(result), &error)));
                --self->pending;
                if (!self->closing) {
                    if (imported)
                        self->edit(false, imported);
                    else
                        gtk_label_set_text(
                            GTK_LABEL(self->detail),
                            "导入失败。请选择 Linux 的版本 1 配置（CA 或首次确认），并检查文件和权限。");
                }
                g_clear_error(&error);
                self->maybeClose();
            },
            this);
        g_task_set_task_data(task, path, g_free);
        g_task_run_in_thread(task, [](GTask *task, gpointer, gpointer data, GCancellable *) {
            try {
                g_task_return_pointer(task, read_public_profile(static_cast<const char *>(data)),
                                      [](gpointer value) { delete static_cast<ImportedProfile *>(value); });
            } catch (...) {
                g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                        "Public profile import failed");
            }
        });
        g_object_unref(task);
    }
    void remove() {
        auto *selected_profile = selected();
        g_autoptr(NMRemoteConnection) profile =
            selected_profile ? NM_REMOTE_CONNECTION(g_object_ref(selected_profile)) : nullptr;
        if (!profile)
            return;
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_OK_CANCEL, "删除所选服务器配置？");
        bool confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(dialog);
        if (!confirmed)
            return;
        ++pending;
        struct Deletion {
            Application *self;
            std::string id;
        };
        auto *operation = new Deletion{this, nm_connection_get_uuid(NM_CONNECTION(profile))};
        nm_remote_connection_delete_async(
            profile, nullptr,
            [](GObject *object, GAsyncResult *result, gpointer data) {
                auto *operation = static_cast<Deletion *>(data);
                auto *self = operation->self;
                GError *failure = nullptr;
                bool ok = nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(object), result, &failure);
                if (ok)
                    prune_profile_certificates(nm_client_get_connections(self->client), operation->id);
                if (ok && self->autoID == operation->id) {
                    self->autoID.clear();
                    self->savePreferences();
                    self->refreshing = true;
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->login), FALSE);
                    self->refreshing = false;
                }
                g_clear_error(&failure);
                delete operation;
                self->saved(ok);
            },
            operation);
        update();
    }
    void exportDiagnostics() {
        GtkWidget *dialog =
            gtk_file_chooser_dialog_new("导出诊断信息", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE,
                                        "取消", GTK_RESPONSE_CANCEL, "保存", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "Lansyra-diagnostics.txt");
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            std::ostringstream report;
            report << "Lansyra " LANSYRA_VERSION "\nPlatform: Linux\nNetworkManager: "
                   << (client ? nm_client_get_version(client) : "unavailable")
                   << "\nSession: " << session_id() << "\nGeneration: " << stateGeneration
                   << "\nState: " << state << "\nError: " << error
                   << "\nAttempt: " << (connection ? connection->state().attempt : 0) << "\n\n";
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
        gtk_window_set_title(GTK_WINDOW(window), "岚序 Lansyra");
        gtk_window_set_default_size(GTK_WINDOW(window), 540, 330);
        gtk_window_set_icon_name(GTK_WINDOW(window), ProductID);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
        gtk_container_set_border_width(GTK_CONTAINER(box), 24);
        gtk_container_add(GTK_CONTAINER(window), box);
        GtkWidget *title = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>岚序 Lansyra</span>");
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
        importButton = gtk_button_new_with_label("导入 .vpn 公共配置…");
        gtk_box_pack_start(GTK_BOX(box), importButton, FALSE, FALSE, 0);
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
        g_signal_connect(importButton, "clicked", G_CALLBACK((+[](GtkButton *, gpointer data) {
                             static_cast<Application *>(data)->importProfile();
                         })),
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
                             if (self->refreshing)
                                 return;
                             const bool previous = self->retryFailed;
                             self->retryFailed = gtk_toggle_button_get_active(button);
                             self->savePreferences([self, previous](bool ok) {
                                 if (!ok) {
                                     self->retryFailed = previous;
                                     self->refreshing = true;
                                     gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->retryCheck),
                                                                  previous);
                                     self->refreshing = false;
                                 }
                             });
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
        if (client && nm_client_get_nm_running(client) && !lansyra_auth_dialog().empty())
            connection =
                std::make_unique<ConnectionController>(client, [this] { connectionChanged(); }, &failure);
        if (!client || !connection || !connection->initialized() || !nm_client_get_nm_running(client)) {
            error = "configuration";
            transition("failed", "系统 VPN 服务不可用",
                       "请启动 NetworkManager，并安装 network-manager-openconnect-gnome。");
        } else {
            if (connection->restore(retryFailed)) {
                startup = false;
                selectedID = connection->state().profile;
            }
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
        }
        g_clear_error(&failure);
        update();
    }
};
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        std::puts("Lansyra " LANSYRA_VERSION);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--check-runtime")) {
        const auto dialog = lansyra_auth_dialog();
        if (dialog.empty()) {
            std::fputs("Install network-manager-openconnect-gnome to provide the authentication helper.\n",
                       stderr);
            return 1;
        }
        std::puts(dialog.c_str());
        return 0;
    }
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
