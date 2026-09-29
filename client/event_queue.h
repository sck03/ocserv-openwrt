#pragma once
#include "session.h"
#include <algorithm>
#include <deque>

namespace bulijie {
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
            if (logs_ >= MaxLogEvents || characters_ + event.text.size() > MaxLogCharacters)
                return false;
            characters_ += event.text.size();
            ++logs_;
        } else if (event.kind == Event::Kind::Statistics || event.kind == Event::Kind::ProfilesChanged ||
                   (event.kind == Event::Kind::State && !event.terminal)) {
            // Keep the newest snapshot at its correct position relative to prompts
            // and terminal events. Log pressure must never discard those events.
            auto previous = std::find_if(events_.begin(), events_.end(), [&](const Event &queued) {
                return queued.kind == event.kind && !queued.terminal;
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

private:
    std::mutex mutex_;
    std::deque<Event> events_;
    size_t characters_ = 0, logs_ = 0;
};
} // namespace bulijie
