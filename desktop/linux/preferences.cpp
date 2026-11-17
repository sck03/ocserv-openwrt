#include "preferences.h"
#include <glib/gstdio.h>
#include <cerrno>

namespace lansyra {
PreferencesRepository::~PreferencesRepository() { g_assert(!active_ && !queued_); }
bool PreferencesRepository::load(Preferences &value) const {
    value = {};
    g_autoptr(GKeyFile) settings = g_key_file_new();
    g_autoptr(GError) error = nullptr;
    const auto path = directory_ + "/lansyra/settings.ini";
    if (!g_key_file_load_from_file(settings, path.c_str(), G_KEY_FILE_NONE, &error))
        return g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
    auto read = [&](const char *key) {
        g_autofree char *text = g_key_file_get_string(settings, "client", key, nullptr);
        return std::string(text ? text : "");
    };
    value.selected = read("selected");
    value.automatic = read("auto_connect");
    value.retry = g_key_file_get_boolean(settings, "client", "retry_failed", nullptr);
    return true;
}
bool PreferencesRepository::write(const Preferences &value) const {
    Preferences previous;
    if (!load(previous)) return false;
    const auto directory = directory_ + "/lansyra";
    const auto startupDirectory = directory_ + "/autostart";
    const auto startup = startupDirectory + "/io.github.sck03.lansyra.desktop";
    if (g_mkdir_with_parents(directory.c_str(), 0700)) return false;
    // The launcher consults auto_connect; installing it before settings commits is harmless.
    const bool existed = g_file_test(startup.c_str(), G_FILE_TEST_EXISTS);
    if (!value.automatic.empty() && !existed) {
        constexpr char entry[] = "[Desktop Entry]\nType=Application\nName=岚序 Lansyra\n"
                                 "Exec=/usr/bin/lansyra --autoconnect\nTerminal=false\n";
        if (g_mkdir_with_parents(startupDirectory.c_str(), 0700) ||
            !g_file_set_contents_full(startup.c_str(), entry, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr))
            return false;
    }
    g_autoptr(GKeyFile) settings = g_key_file_new();
    g_key_file_set_string(settings, "client", "selected", value.selected.c_str());
    g_key_file_set_string(settings, "client", "auto_connect", value.automatic.c_str());
    g_key_file_set_boolean(settings, "client", "retry_failed", value.retry);
    gsize size = 0;
    g_autofree char *text = g_key_file_to_data(settings, &size, nullptr);
    const bool ok = g_file_set_contents_full((directory + "/settings.ini").c_str(), text,
        static_cast<gssize>(size), G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr);
    if (!ok && !existed && !value.automatic.empty()) g_unlink(startup.c_str());
    // A leftover disabled launcher cannot connect because the committed setting is empty.
    if (ok && value.automatic.empty()) g_unlink(startup.c_str());
    return ok;
}
void PreferencesRepository::save(Preferences value, std::function<void(bool)> completed) {
    if (!queued_) queued_ = Request{};
    queued_->value = std::move(value);
    queued_->completions.push_back(std::move(completed));
    if (!active_) start();
}
void PreferencesRepository::start() {
    active_ = true;
    auto *request = new Request(std::move(*queued_));
    queued_.reset();
    GTask *task = g_task_new(nullptr, nullptr, [](GObject *, GAsyncResult *result, gpointer data) {
        auto *self = static_cast<PreferencesRepository *>(data);
        auto *task = G_TASK(result);
        const bool ok = g_task_propagate_boolean(task, nullptr);
        auto *request = static_cast<Request *>(g_task_get_task_data(task));
        // Keep active while delivering callbacks; reentrant saves join the next batch.
        for (auto &completed : request->completions) completed(ok);
        self->active_ = false;
        if (self->queued_) self->start();
    }, this);
    g_task_set_task_data(task, request, [](gpointer data) { delete static_cast<Request *>(data); });
    g_task_run_in_thread(task, [](GTask *task, gpointer, gpointer data, GCancellable *) {
        auto *self = static_cast<PreferencesRepository *>(g_async_result_get_user_data(G_ASYNC_RESULT(task)));
        const auto *request = static_cast<Request *>(data);
        g_task_return_boolean(task, self->write(request->value));
    });
    g_object_unref(task);
}
} // namespace lansyra
