#include "ui.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned ticks = 0;
void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) { ++ticks; }
}

int main() {
    HWND owner = CreateWindowExW(0, L"STATIC", L"Storage test", WS_OVERLAPPED,
                                 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!owner) return 1;
    SetTimer(owner, 1, 10, tick);
    const auto ui = std::this_thread::get_id();
    std::wstring error;
    bool background = false;
    const bool saved = vpn::ui::run_storage(owner, vpn::Language::English, [&](std::wstring &) {
        background = std::this_thread::get_id() != ui;
        Sleep(200);
        return true;
    }, error);
    const bool failed = vpn::ui::run_storage(owner, vpn::Language::English, [](std::wstring &) -> bool {
        throw std::runtime_error("fixture IO failure");
    }, error);
    KillTimer(owner, 1);
    DestroyWindow(owner);
    if (!saved || !background || ticks < 2 || failed || error.empty() || vpn::ui::storage_pending())
        return 1;
    std::cout << "Storage runs off the UI thread, pumps messages and reports failures.\n";
}
