#pragma once
#include "ui.h"
#include "event_queue.h"
#include "preferences_writer.h"
#include <atomic>

namespace vpn {
struct NetworkWatchContext {
    HWND window = nullptr;
    std::atomic_bool message_pending{false};
};

class Application final : public ui::Window {
public:
    explicit Application(std::filesystem::path data_directory);
    ~Application() override;
    int run(int show, const std::string &connect_profile, bool startup = false);

private:
    friend struct ApplicationTestAccess;
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override;
    const wchar_t *t(const wchar_t *zh, const wchar_t *en) const {
        return tr(preferences_.language, zh, en);
    }
    const Profile *selected() const;
    bool busy() const {
        return session_ != nullptr;
    }
    void create_controls();
    void rebuild_menu();
    void translate();
    bool reload(const std::string &selected = {});
    void update_controls();
    void select_tab();
    void command(int id);
    void connect();
    void disconnect();
    void shutdown();
    void restore();
    void minimize();
    bool tray(bool add = false);
    void remove_tray();
    void tray_menu();
    void save_preferences();
    bool flush_preferences(std::wstring &error);
    void preferences_saved();
    void toggle_auto_connect();
    void export_diagnostics();
    void watch_network();
    void stop_network_watch();
    void drain_network_change();
    void receive(Event event);
    void drain_events();
    void show_statistics(const Statistics &stats);
    bool claim_instance();
    ProfileStore store_;
    PreferencesWriter preferences_writer_;
    Preferences preferences_;
    std::vector<Profile> profiles_;
    std::unique_ptr<Session> session_;
    ui::LogWindow log_;
    State state_ = State::Idle;
    SessionStatus status_;
    Statistics statistics_;
    EventQueue events_;
    EventCursor cursor_;
    Handle instance_;
    std::wstring instance_name_;
    std::string auto_connect_;
    std::wstring active_gateway_;
    bool active_minimize_ = false;
    bool closing_ = false, tray_added_ = false;
    bool draining_ = false, system_suspended_ = false;
    unsigned timer_tick_ = 0;
    uint64_t network_change_at_ = 0;
    HANDLE network_notifications_[3]{};
    NetworkWatchContext network_watch_;
    UINT taskbar_created_ = 0;
    HACCEL accelerators_ = nullptr;
};
} // namespace vpn
