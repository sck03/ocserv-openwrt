#pragma once
#include "profile.h"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace vpn {
// One bounded latest-value slot; file IO and cross-process lock waits stay off
// the UI thread. Notifications never transfer ownership through HWND messages.
class PreferencesWriter {
public:
    using Save = std::function<bool(const Preferences &, std::wstring &)>;
    struct Result {
        uint64_t generation = 0;
        bool success = false;
        std::wstring error;
    };
    explicit PreferencesWriter(ProfileStore &store)
        : PreferencesWriter([&store](const Preferences &preferences, std::wstring &error) {
              return store.save_preferences(preferences, error);
          }) {}
    explicit PreferencesWriter(Save save) : save_(std::move(save)) {}
    ~PreferencesWriter();
    void start(std::function<void()> notify);
    bool request(const Preferences &preferences, std::wstring &error);
    bool flush(std::wstring &error);
    void stop();
    bool ready() const;
    bool pending() const;
    std::optional<Result> take_result();

private:
    void run();
    Save save_;
    std::function<void()> notify_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    Preferences snapshot_;
    std::optional<Result> result_;
    bool result_pending_ = false;
    uint64_t generation_ = 0;
    bool started_ = false, dirty_ = false, writing_ = false, force_ = false, stopping_ = false;
};
} // namespace vpn
