#pragma once
#include "connection.h"
#include <string_view>

namespace vpn {
// The worker owns the redactor. It remembers every authentication field, including
// hidden fields and cookies, until all cleanup callbacks have completed.
class Redactor {
public:
    ~Redactor();
    void remember(const std::string &value);
    std::string clean(std::string message) const;
    void clear();

private:
    std::vector<std::string> values_;
    size_t characters_ = 0;
    bool saturated_ = false;
};
std::string diagnostic_report(const SessionStatus &status, const Statistics &statistics,
                              std::wstring_view log, uint64_t dropped_logs);
} // namespace vpn
