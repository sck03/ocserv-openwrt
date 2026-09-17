// In-process UI regression tests: only windows owned by this test's UI thread
// are inspected. No input is injected into the user's desktop or other apps.
#include "application.h"
#include <objbase.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace bulijie;
using namespace bulijie::ui;
namespace {
unsigned passed = 0;
DWORD ui_thread = 0;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
    ++passed;
}
HWND find_window(const wchar_t *type) {
    struct Search {
        const wchar_t *type;
        HWND window = nullptr;
        static BOOL CALLBACK visit(HWND window, LPARAM parameter) {
            auto *search = reinterpret_cast<Search *>(parameter);
            wchar_t type[128]{};
            GetClassNameW(window, type, 128);
            if (wcscmp(type, search->type) == 0) {
                search->window = window;
                return FALSE;
            }
            return TRUE;
        }
    } search{type};
    EnumThreadWindows(ui_thread, Search::visit, reinterpret_cast<LPARAM>(&search));
    return search.window;
}
template <class Predicate> void wait_for(Predicate predicate, const char *description) {
    for (unsigned i = 0; i < 500; ++i) {
        if (predicate())
            return;
        Sleep(20);
    }
    throw std::runtime_error(description);
}
HWND await_window(const wchar_t *type) {
    HWND result = nullptr;
    wait_for(
        [&] {
            result = find_window(type);
            return result && IsWindowVisible(result);
        },
        "Expected test window did not open");
    return result;
}
void command(HWND window, int id) {
    PostMessageW(window, WM_COMMAND, static_cast<WPARAM>(id), 0);
}
void set(HWND window, int id, const wchar_t *value) {
    SendMessageW(GetDlgItem(window, id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value));
}
std::wstring get(HWND window, int id) {
    return read_window_text(GetDlgItem(window, id));
}
void snapshot(HWND window, const std::filesystem::path &destination) {
    SendMessageW(window, WM_PAINT, 0, 0);
    RECT rect{};
    GetWindowRect(window, &rect);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    bool captured = PrintWindow(window, memory, 0) != FALSE;
    if (captured) {
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        DWORD size = static_cast<DWORD>(width * height * 4);
        header.bfSize = header.bfOffBits + size;
        std::ofstream file(destination, std::ios::binary);
        file.write(reinterpret_cast<const char *>(&header), sizeof(header));
        file.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        file.write(static_cast<const char *>(pixels), size);
        captured = file.good();
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, screen);
    check(captured, "Could not capture test window");
}
void controls_fit(HWND window) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    struct Check {
        HWND parent;
        RECT bounds;
        bool fits = true;
        static BOOL CALLBACK visit(HWND child, LPARAM parameter) {
            auto *state = reinterpret_cast<Check *>(parameter);
            if (GetParent(child) != state->parent || !IsWindowVisible(child))
                return TRUE;
            RECT rect{};
            GetWindowRect(child, &rect);
            MapWindowPoints(nullptr, state->parent, reinterpret_cast<POINT *>(&rect), 2);
            if (rect.left < 0 || rect.top < 0 || rect.right > state->bounds.right + 1 ||
                rect.bottom > state->bounds.bottom + 1)
                state->fits = false;
            return TRUE;
        }
    } state{window, bounds};
    EnumChildWindows(window, Check::visit, reinterpret_cast<LPARAM>(&state));
    check(state.fits, "A visible control falls outside its window");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    SetDllDirectoryW(L"");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    WSADATA sockets{};
    WSAStartup(MAKEWORD(2, 2), &sockets);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (openconnect_init_ssl() != 0)
        return 2;
    const auto root = std::filesystem::absolute(argv[1]) / wide(random_id());
    std::filesystem::create_directories(root);
    ui_thread = GetCurrentThreadId();
    std::string failure;
    try {
        Application application(root / L"data");
        std::thread actions([&] {
            try {
                HWND main = await_window(L"BulijieVPN.Main");
                check(read_window_text(main) == L"布利杰VPN", "Fresh UI must start in Chinese");
                check(get(main, Connect) == L"连接" && !IsWindowEnabled(GetDlgItem(main, Connect)),
                      "Empty gateway cannot connect");
                controls_fit(main);
                snapshot(main, root / L"main-zh.bmp");
                command(main, NewProfile);
                HWND profile = await_window(L"BulijieVPN.Profile");
                set(profile, ProfileName, L"办公 VPN（测试）");
                set(profile, ProfileGateway, L"vpn.example.test:4443");
                controls_fit(profile);
                snapshot(profile, root / L"quick-profile-zh.bmp");
                command(profile, IDOK);
                wait_for([&] { return !IsWindow(profile); }, "Quick profile did not save");
                wait_for([&] { return SendMessageW(GetDlgItem(main, Servers), CB_GETCOUNT, 0, 0) == 1; },
                         "Profile list did not refresh");
                check(IsWindowEnabled(GetDlgItem(main, Connect)) != FALSE, "Saved profile can connect");
                command(main, EditSelected);
                profile = await_window(L"BulijieVPN.Profile");
                check(get(profile, ProfileName) == L"办公 VPN（测试）",
                      "Unicode profile name did not round-trip");
                check(get(profile, ProfileGateway) == L"https://vpn.example.test:4443",
                      "Gateway did not normalize");
                check(SendMessageW(GetDlgItem(profile, ui::TokenType), CB_GETCOUNT, 0, 0) >= 4,
                      "HOTP/TOTP/STOKEN choices are missing");
                set(profile, ReconnectTimeout, L"601");
                set(profile, DTLSPeriod, L"17");
                SendMessageW(GetDlgItem(profile, DisableUDP), BM_SETCHECK, BST_CHECKED, 0);
                controls_fit(profile);
                snapshot(profile, root / L"advanced-profile-zh.bmp");
                command(profile, IDOK);
                wait_for([&] { return !IsWindow(profile); }, "Advanced profile did not save");
                ProfileStore store(root / L"data");
                std::vector<Profile> profiles;
                Preferences preferences;
                std::wstring error;
                check(store.load(profiles, preferences, error) && profiles.size() == 1 &&
                          profiles[0].reconnect_timeout == 601 && profiles[0].dtls_period == 17 &&
                          profiles[0].disable_udp,
                      "Advanced settings were not persisted");
                command(main, EditSelected);
                profile = await_window(L"BulijieVPN.Profile");
                set(profile, ProfileName, L"must-not-save");
                command(profile, IDCANCEL);
                wait_for([&] { return !IsWindow(profile); }, "Cancel did not close editor");
                check(store.load(profiles, preferences, error) && profiles[0].name == L"办公 VPN（测试）",
                      "Cancel changed the saved profile");
                SendMessageW(GetDlgItem(main, Tabs), TCM_SETCURSEL, 1, 0);
                NMHDR notification{GetDlgItem(main, Tabs), Tabs, TCN_SELCHANGE};
                SendMessageW(main, WM_NOTIFY, Tabs, reinterpret_cast<LPARAM>(&notification));
                check(!IsWindowVisible(GetDlgItem(main, Servers)) && IsWindowVisible(GetDlgItem(main, 540)),
                      "VPN Info page did not switch");
                controls_fit(main);
                snapshot(main, root / L"vpn-info-zh.bmp");
                command(main, ShowLog);
                HWND log = await_window(L"BulijieVPN.Log");
                check(get(log, LogAutoScroll) == L"自动滚动", "Log controls were not translated");
                controls_fit(log);
                snapshot(log, root / L"log-zh.bmp");
                command(log, IDCANCEL);
                wait_for([&] { return !IsWindowVisible(log); }, "Log did not hide");
                command(main, English);
                wait_for([&] { return read_window_text(main) == L"BulijieVPN"; }, "Language did not switch");
                check(get(main, Connect) == L"Connect", "English button label is missing");
                SendMessageW(GetDlgItem(main, Tabs), TCM_SETCURSEL, 0, 0);
                SendMessageW(main, WM_NOTIFY, Tabs, reinterpret_cast<LPARAM>(&notification));
                snapshot(main, root / L"main-en.bmp");
                command(main, EditSelected);
                profile = await_window(L"BulijieVPN.Profile");
                check(read_window_text(profile) == L"Edit profile", "Editor did not switch to English");
                controls_fit(profile);
                snapshot(profile, root / L"advanced-profile-en.bmp");
                command(profile, IDCANCEL);
                wait_for([&] { return !IsWindow(profile); }, "English editor did not close");
                command(main, Minimize);
                wait_for([&] { return !IsWindowVisible(main) || IsIconic(main); }, "Minimize did not work");
                command(main, Restore);
                wait_for([&] { return IsWindowVisible(main) && !IsIconic(main); }, "Restore did not work");
                check(store.load(profiles, preferences, error) && preferences.language == Language::English,
                      "Language preference was not saved");
                command(main, Quit);
            } catch (const std::exception &exception) {
                failure = exception.what();
                // Close only windows owned by the current test thread.
                if (HWND profile = find_window(L"BulijieVPN.Profile"))
                    command(profile, IDCANCEL);
                if (HWND main = find_window(L"BulijieVPN.Main"))
                    command(main, Quit);
            }
        });
        int status = application.run(SW_SHOWNORMAL, {});
        actions.join();
        if (status || !failure.empty())
            throw std::runtime_error(failure.empty() ? "UI event loop failed" : failure);
        std::cout << "{\"passed\":" << passed << ",\"screenshots\":\"" << utf8(root.generic_wstring())
                  << "\"}\n";
    } catch (const std::exception &exception) {
        std::cerr << "UI regression failed: " << exception.what() << "\n";
        return 1;
    }
    CoUninitialize();
    WSACleanup();
    return 0;
}
