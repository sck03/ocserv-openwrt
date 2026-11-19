#pragma once
#include "platform.h"

namespace vpn {
// Held by the session worker through every reconnect and final core cleanup.
// Windows sharing rules protect the file from writers/deleters in other processes.
inline Handle retain_certificate(const std::wstring &path) {
    return Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
}
}
