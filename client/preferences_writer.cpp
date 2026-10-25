#include "preferences_writer.h"
#include <chrono>

namespace vpn {
PreferencesWriter::~PreferencesWriter() {
    stop();
}
void PreferencesWriter::start(std::function<void()> notify) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable())
        return;
    notify_ = std::move(notify);
    stopping_ = false;
    worker_ = std::thread([this] { run(); });
    started_ = true;
}
bool PreferencesWriter::request(const Preferences &preferences, std::wstring &error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || stopping_) {
            error = L"偏好保存线程不可用。 / The preference writer is unavailable.";
            return false;
        }
        snapshot_ = preferences;
        ++generation_;
        dirty_ = true;
    }
    condition_.notify_all();
    return true;
}
bool PreferencesWriter::flush(std::wstring &error) {
    std::unique_lock<std::mutex> lock(mutex_);
    force_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this] { return !dirty_ && !writing_; });
    force_ = false;
    if (result_ && result_->generation == generation_ && !result_->success) {
        error = result_->error;
        if (error.empty())
            error = L"保存偏好时发生错误。 / Could not save preferences.";
        return false;
    }
    return true;
}
void PreferencesWriter::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
}
bool PreferencesWriter::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dirty_ || writing_;
}
bool PreferencesWriter::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return started_ && !stopping_;
}
std::optional<PreferencesWriter::Result> PreferencesWriter::take_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result_pending_ || !result_ || result_->generation != generation_)
        return {};
    result_pending_ = false;
    return result_;
}
void PreferencesWriter::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        condition_.wait(lock, [this] { return stopping_ || dirty_; });
        if (stopping_ && !dirty_)
            break;
        if (!stopping_ && !force_) {
            const auto generation = generation_;
            const bool changed = condition_.wait_for(
                lock, std::chrono::milliseconds(250), [this, generation] {
                    return stopping_ || force_ || generation_ != generation;
                });
            if (changed && !stopping_ && !force_)
                continue;
        }
        Preferences preferences = std::move(snapshot_);
        const auto generation = generation_;
        dirty_ = false;
        writing_ = true;
        lock.unlock();
        Result completed{generation, false, {}};
        try {
            completed.success = save_(preferences, completed.error);
        } catch (...) {
            // Unexpected filesystem/JSON/allocation errors are a failed save,
            // never an exception escaping a std::thread entry point. The UI
            // supplies a generic message when no detailed error is available.
            completed.success = false;
        }
        lock.lock();
        writing_ = false;
        result_ = std::move(completed);
        result_pending_ = true;
        condition_.notify_all();
        lock.unlock();
        try {
            if (notify_)
                notify_();
        } catch (...) {
            // The saved result remains available to a polling caller.
        }
        lock.lock();
        if (stopping_ && !dirty_)
            break;
    }
}
} // namespace vpn
