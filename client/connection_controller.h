#pragma once
#include "event_queue.h"

namespace vpn {
// Owns the worker and its event ordering. Window code consumes value snapshots.
class ConnectionController {
public:
    explicit ConnectionController(ProfileStore &store) : store_(store) {}
    ~ConnectionController();
    void set_wakeup(std::function<void()> wakeup) {
        wakeup_ = std::move(wakeup);
    }
    bool start(Profile profile, Language language, Session::Options options, int log_level);
    void cancel();
    void stop(); // Final teardown after the window's message pump has ended.
    void network_changed();
    void suspend(bool suspended);
    void request_statistics();
    void set_log_level(int level);
    void drain(const Session::Sink &consume);
    bool poll(const Session::Sink &consume);
    bool active() const {
        return session_ != nullptr;
    }
    bool canceled() const {
        return cursor_.canceled();
    }
    bool dispatching() const {
        return dispatching_;
    }
    bool prompt_active(uint64_t id) const;
    uint64_t id() const {
        return session_ ? session_->id() : 0;
    }
    uint64_t dropped_logs() const {
        return events_.dropped();
    }

private:
    friend struct ApplicationTestAccess;
    ProfileStore &store_;
    EventQueue events_;
    EventCursor cursor_;
    std::unique_ptr<Session> session_;
    std::function<void()> wakeup_;
    bool dispatching_ = false;
};
} // namespace vpn
