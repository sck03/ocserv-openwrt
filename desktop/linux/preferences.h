#pragma once
#include <gio/gio.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lansyra {
struct Preferences {
    std::string selected, automatic;
    bool retry = false;
};
// Owned and called on one main context. The owner waits for callbacks before destruction.
class PreferencesRepository {
public:
    explicit PreferencesRepository(std::string directory) : directory_(std::move(directory)) {}
    ~PreferencesRepository();
    bool load(Preferences &value) const;
    void save(Preferences value, std::function<void(bool)> completed);
private:
    bool write(const Preferences &value) const;
    void start();
    std::string directory_;
    bool active_ = false;
    struct Request { Preferences value; std::vector<std::function<void(bool)>> completions; };
    std::optional<Request> queued_;
};
} // namespace lansyra
