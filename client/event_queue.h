#pragma once
#include "session.h"
#include <algorithm>
#include <deque>

namespace vpn {
// Only the UI consumes events. The worker must never wait for it to catch up.
class EventQueue {
public:
    static constexpr size_t MaxLogCharacters = 256 * 1024;
    static constexpr size_t MaxLogEvents = 512;
    bool push(Event event) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool wake = events_.empty();
        if (event.kind == Event::Kind::Log) {
            if (event.text.size() > 8192)
                event.text.resize(8192);
            if (logs_ >= MaxLogEvents || characters_ + event.text.size() > MaxLogCharacters) {
                ++dropped_;
                return false;
            }
            characters_ += event.text.size();
            ++logs_;
        } else if (event.kind == Event::Kind::Statistics || event.kind == Event::Kind::ProfilesChanged ||
                   (event.kind == Event::Kind::State && !event.terminal)) {
            // Keep the newest snapshot at its correct position relative to prompts
            // and terminal events. Log pressure must never discard those events.
            auto previous = std::find_if(events_.begin(), events_.end(), [&](const Event &queued) {
                return queued.session_id == event.session_id && queued.kind == event.kind && !queued.terminal;
            });
            if (previous != events_.end())
                events_.erase(previous);
        }
        events_.push_back(std::move(event));
        return wake;
    }
    std::deque<Event> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<Event> events;
        events.swap(events_);
        characters_ = logs_ = 0;
        return events;
    }
    uint64_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    size_t characters_ = 0, logs_ = 0;
    uint64_t dropped_ = 0;
};
// UI-thread cursor: stale sessions and delayed snapshots cannot revive canceled
// connections. Logs remain ordered even when state snapshots are coalesced.
class EventCursor {
public:
    void begin(uint64_t id) {
        id_ = id;
        generation_ = 0;
        canceled_ = terminal_ = false;
    }
    void cancel() {
        canceled_ = true;
    }
    bool canceled() const {
        return canceled_;
    }
    bool accept(const Event &event) {
        if (!id_ || event.session_id != id_)
            return false;
        if (event.kind == Event::Kind::Log || event.kind == Event::Kind::ProfilesChanged)
            return true;
        if (terminal_ || event.generation < generation_)
            return false;
        if (canceled_ &&
            !(event.kind == Event::Kind::State && (event.terminal || event.state == State::Disconnecting)))
            return false;
        generation_ = event.generation;
        if (event.kind == Event::Kind::State && event.terminal)
            terminal_ = true;
        return true;
    }

private:
    uint64_t id_ = 0, generation_ = 0;
    bool canceled_ = false, terminal_ = false;
};
} // namespace vpn
