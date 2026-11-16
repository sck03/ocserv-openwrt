#include "ui.h"
#include <future>

namespace vpn::ui {
namespace {
thread_local bool active = false;
// A modal operation owns both its worker and result. The owner cannot submit a
// second mutation, while the normal message loop continues to paint and drain
// session events. No detached threads, worker HWND access or unbounded queue.
class StorageOperation final : public Dialog {
public:
    StorageOperation(Language language, std::function<bool(std::wstring &)> operation)
        : language_(language), operation_(std::move(operation)) {}
    std::wstring error;
    void settle() {
        // WM_QUIT can end a modal loop before the completion timer. Join before
        // reading the result or releasing captures even on that shutdown path.
        if (worker_.valid()) worker_.get();
    }

private:
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_CREATE) {
            control(L"STATIC", tr(language_, L"正在处理配置，请稍候…", L"Updating configuration, please wait…"),
                    0, -1, 16, 18, 360, 32);
            try {
                worker_ = std::async(std::launch::async, [this] {
                    try { return operation_(error); }
                    catch (...) {
                        error = tr(language_, L"配置操作失败。", L"The configuration operation failed.");
                        return false;
                    }
                });
            } catch (...) {
                error = tr(language_, L"无法启动配置工作线程。", L"Could not start the configuration worker.");
                return -1;
            }
            SetTimer(window_, 1, 50, nullptr);
            return 0;
        }
        if (message == WM_TIMER && worker_.valid() &&
            worker_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            const bool success = worker_.get();
            KillTimer(window_, 1);
            finish(success);
            return 0;
        }
        // A file commit cannot be safely canceled halfway through.
        if (message == WM_CLOSE || message == WM_COMMAND)
            return 0;
        if (message == WM_QUERYENDSESSION)
            return FALSE;
        return Dialog::message(message, wparam, lparam);
    }
    Language language_;
    std::function<bool(std::wstring &)> operation_;
    std::future<bool> worker_;
};
} // namespace
bool storage_pending() { return active; }

bool run_storage(HWND owner, Language language, std::function<bool(std::wstring &)> operation,
                 std::wstring &error) {
    // Startup before a window exists has no message loop to keep responsive.
    if (!owner)
        return operation(error);
    if (active) {
        error = tr(language, L"请等待当前配置操作完成。", L"Wait for the current configuration operation.");
        return false;
    }
    struct Scope {
        Scope() { active = true; }
        ~Scope() { active = false; }
    } scope;
    StorageOperation dialog(language, std::move(operation));
    const bool result = dialog.run(owner, L"Lansyra.Storage", Product, 392, 70);
    dialog.settle();
    error = std::move(dialog.error);
    return result;
}
} // namespace vpn::ui
