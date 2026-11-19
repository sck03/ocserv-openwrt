#include "profile_editor.h"
#include "protocol.h"

namespace lansyra {
std::optional<ProfileDraft> edit_profile(GtkWindow *owner, NMConnection *existing,
                                         const ImportedProfile *imported) {
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(existing ? "编辑服务器" : "新建服务器", owner, GTK_DIALOG_MODAL, "取消",
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
    if (existing) {
        auto *vpn = nm_connection_get_setting_vpn(existing);
        const char *values[] = {nm_connection_get_id(existing), nm_setting_vpn_get_data_item(vpn, "gateway"),
                                nm_setting_vpn_get_user_name(vpn),
                                nm_setting_vpn_get_data_item(vpn, "cacert")};
        for (int i = 0; i < 4; ++i)
            gtk_entry_set_text(GTK_ENTRY(entries[i]), values[i] ? values[i] : "");
    } else if (imported) {
        gtk_entry_set_text(GTK_ENTRY(entries[0]), imported->name.c_str());
        gtk_entry_set_text(GTK_ENTRY(entries[1]), imported->server.c_str());
        gtk_entry_set_text(GTK_ENTRY(entries[3]), imported->ca.c_str());
    }
    auto *validation = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(validation), TRUE);
    gtk_grid_attach(GTK_GRID(grid), validation, 0, 4, 2, 1);
    gtk_widget_show_all(dialog);
    std::optional<ProfileDraft> result;
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        ProfileDraft draft{
            gtk_entry_get_text(GTK_ENTRY(entries[0])), gateway(gtk_entry_get_text(GTK_ENTRY(entries[1]))),
            gtk_entry_get_text(GTK_ENTRY(entries[2])), gtk_entry_get_text(GTK_ENTRY(entries[3]))};
        if (!draft.name.empty() && field(draft.name) && !draft.server.empty() && field(draft.username) &&
            (draft.ca.empty() || g_file_test(draft.ca.c_str(), G_FILE_TEST_IS_REGULAR))) {
            result = std::move(draft);
            break;
        }
        gtk_label_set_text(GTK_LABEL(validation), "请检查名称、HTTPS 地址、用户名及 CA 文件。输入已保留。");
    }
    gtk_widget_destroy(dialog);
    return result;
}
} // namespace lansyra
