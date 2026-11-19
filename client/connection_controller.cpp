#include "connection_controller.h"

namespace vpn {
ConnectionController::~ConnectionController() {
    stop();
}
void ConnectionController::stop() {
    cancel();
    session_.reset();
}
bool ConnectionController::start(Profile profile, Language language, Session::Options options, int level) {
    if (session_)
        return false;
    session_ = std::make_unique<Session>(
        std::move(profile), &store_, language,
        [this](Event event) {
            if (events_.push(std::move(event)) && wakeup_)
                wakeup_();
        },
        options);
    cursor_.begin(session_->id());
    session_->set_log_level(level);
    if (session_->start())
        return true;
    session_.reset();
    return false;
}
void ConnectionController::cancel() {
    cursor_.cancel();
    if (session_)
        session_->cancel();
}
void ConnectionController::network_changed() {
    if (session_ && !canceled())
        session_->network_changed();
}
void ConnectionController::suspend(bool value) {
    if (session_ && !canceled())
        session_->suspend(value);
}
void ConnectionController::request_statistics() {
    if (session_)
        session_->request_statistics();
}
void ConnectionController::set_log_level(int level) {
    if (session_)
        session_->set_log_level(level);
}
bool ConnectionController::prompt_active(uint64_t id) const {
    return session_ && session_->id() == id && !session_->finished() && !canceled();
}
void ConnectionController::drain(const Session::Sink &consume) {
    if (dispatching_)
        return;
    struct Dispatch {
        bool &active;
        explicit Dispatch(bool &value) : active(value) {
            active = true;
        }
        ~Dispatch() {
            active = false;
        }
    } dispatch(dispatching_);
    for (auto &event : events_.take()) {
        if (cursor_.accept(event))
            consume(std::move(event));
        else if (event.prompt)
            event.prompt->answer(false);
    }
}
bool ConnectionController::poll(const Session::Sink &consume) {
    if (dispatching_)
        return false;
    drain(consume);
    if (!session_ || !session_->finished())
        return false;
    // The terminal event may arrive after the first drain and before finished().
    drain(consume);
    session_.reset();
    return true;
}
} // namespace vpn
