#pragma once
#include "platform.h"
namespace vpn {
class StoreLease {
public:
    StoreLease(const std::wstring &name, std::wstring &error)
        : mutex_(CreateMutexW(nullptr, FALSE, name.c_str())) {
        if (!mutex_) { error = system_error(GetLastError()); return; }
        const DWORD wait = WaitForSingleObject(mutex_.get(), 5000);
        locked_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!locked_)
            error = L"另一个实例正在保存配置。 / Another instance is saving profiles.";
    }
    ~StoreLease() { if (locked_) ReleaseMutex(mutex_.get()); }
    explicit operator bool() const { return locked_; }
private:
    Handle mutex_;
    bool locked_ = false;
};
} // namespace vpn
