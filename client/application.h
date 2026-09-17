#pragma once
#include "ui.h"
#include <deque>

namespace bulijie {
class Application final : public ui::Window {
public:
    explicit Application(std::filesystem::path data_directory);
    ~Application() override;
    int run(int show, const std::string &connect_profile);

private:
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override;
    const wchar_t *t(const wchar_t *zh, const wchar_t *en) const {
        return tr(preferences_.language, zh, en);
    }
    const Profile *selected() const;
    bool busy() const {
        return session_ && !session_->finished();
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
    void receive(Event event);
    void drain_events();
    void show_statistics(const Statistics &stats);
    bool claim_instance();
    ProfileStore store_;
    Preferences preferences_;
    std::vector<Profile> profiles_;
    std::unique_ptr<Session> session_;
    ui::LogWindow log_;
    State state_ = State::Idle;
    std::mutex queue_mutex_;
    std::deque<Event> events_;
    Handle instance_;
    std::wstring instance_name_;
    std::string auto_connect_;
    std::wstring active_name_, active_gateway_;
    bool active_minimize_ = false;
    bool closing_ = false, tray_added_ = false;
    UINT taskbar_created_ = 0;
    HACCEL accelerators_ = nullptr;
};
} // namespace bulijie
