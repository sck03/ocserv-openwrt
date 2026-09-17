#include "ui.h"
#include <commdlg.h>
#include <algorithm>
#include <cstdio>

namespace bulijie::ui {
Window::~Window() {
    if (window_)
        DestroyWindow(window_);
    if (font_)
        DeleteObject(font_);
}
LRESULT CALLBACK Window::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *self = reinterpret_cast<Window *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, wparam, lparam);
    LRESULT result = 0;
    try {
        result = self->message(message, wparam, lparam);
    } catch (...) {
        result = DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_NCDESTROY) {
        self->window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return result;
}
bool Window::create(HWND owner, const wchar_t *type, const std::wstring &title, int width, int height,
                    DWORD style, DWORD extended, HMENU menu) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(2), IMAGE_ICON, 32, 32, LR_SHARED));
    cls.hIconSm =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(2), IMAGE_ICON, 16, 16, LR_SHARED));
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = type;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    HDC screen = GetDC(nullptr);
    dpi_ = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // Keep the complete form reachable on small displays and at large text scales.
    dpi_ = std::max(96, std::min({dpi_, MulDiv(work.right - work.left - 40, 96, width),
                                  MulDiv(work.bottom - work.top - 80, 96, height)}));
    font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                        L"Microsoft YaHei UI");
    RECT rect{0, 0, px(width), px(height)};
    AdjustWindowRectEx(&rect, style, menu != nullptr, extended);
    RECT parent = work;
    if (owner)
        GetWindowRect(owner, &parent);
    int x = std::clamp((parent.left + parent.right - rect.right + rect.left) / 2, work.left,
                       std::max(work.left, work.right - (rect.right - rect.left)));
    int y = std::clamp((parent.top + parent.bottom - rect.bottom + rect.top) / 2, work.top,
                       std::max(work.top, work.bottom - (rect.bottom - rect.top)));
    return CreateWindowExW(extended, type, title.c_str(), style, x, y, rect.right - rect.left,
                           rect.bottom - rect.top, owner, menu, instance, this) != nullptr;
}
HWND Window::control(const wchar_t *type, const std::wstring &text, DWORD style, int id, int x, int y,
                     int width, int height, DWORD extended) {
    HWND result = CreateWindowExW(
        extended, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(width), px(height),
        window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return result;
}
HWND Window::edit(const std::wstring &text, int id, int x, int y, int width, int height, DWORD style) {
    HWND result = control(L"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL | style, id, x, y, width, height,
                          WS_EX_CLIENTEDGE);
    SendMessageW(result, EM_SETLIMITTEXT, 8192, 0);
    return result;
}
HWND Window::button(const wchar_t *text, int id, int x, int y, int width, int height, DWORD style) {
    return control(L"BUTTON", text, WS_TABSTOP | style, id, x, y, width, height);
}
HWND Window::combo(int id, int x, int y, int width, bool editable) {
    return control(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST),
                   id, x, y, width, 260);
}
void Window::move(int id, int x, int y, int width, int height) {
    MoveWindow(item(id), px(x), px(y), px(width), px(height), TRUE);
}
LRESULT Window::message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        RECT rect{};
        GetClientRect(window_, &rect);
        FillRect(reinterpret_cast<HDC>(wparam), &rect, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}
bool Dialog::run(HWND owner, const wchar_t *type, const std::wstring &title, int width, int height) {
    if (!create(owner, type, title, width, height, WS_CAPTION | WS_SYSMENU,
                WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT))
        return false;
    const bool was_enabled = owner && IsWindowEnabled(owner);
    if (was_enabled)
        EnableWindow(owner, FALSE);
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
    HWND first = initial_focus_ ? item(initial_focus_) : GetNextDlgTabItem(window_, nullptr, FALSE);
    if (first)
        SetFocus(first);
    MSG message{};
    while (window_) {
        int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (!result)
                PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (window_)
        DestroyWindow(window_);
    if (was_enabled && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    return accepted_;
}
void Dialog::finish(bool accepted) {
    accepted_ = accepted;
    if (window_)
        DestroyWindow(window_);
}
LRESULT Dialog::message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == DM_GETDEFID)
        return MAKELRESULT(default_button_, DC_HASDEFID);
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
        finish(false);
        return 0;
    }
    return Window::message(message, wparam, lparam);
}
std::optional<std::filesystem::path> choose_file(HWND owner, bool save, const wchar_t *filter,
                                                 const wchar_t *extension, const std::wstring &initial) {
    std::vector<wchar_t> buffer(32768);
    if (initial.size() < buffer.size())
        std::copy(initial.begin(), initial.end(), buffer.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = extension;
    dialog.Flags =
        OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)))
        return {};
    return std::filesystem::path(buffer.data());
}
void error(HWND owner, const std::wstring &message) {
    MessageBoxW(owner, message.c_str(), Product, MB_OK | MB_ICONERROR);
}
void add_choice(HWND combo, const std::wstring &label, LPARAM value) {
    LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    if (index >= 0)
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), value);
}
void select_choice(HWND combo, LPARAM value) {
    LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i)
        if (SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(i), 0) == value) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}
LPARAM selected_choice(HWND combo, LPARAM fallback) {
    LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return index < 0 ? fallback : SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
}
std::wstring bytes(uint64_t value) {
    constexpr const wchar_t *units[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB"};
    unsigned unit = 0;
    double amount = static_cast<double>(value);
    while (amount >= 1024 && unit < 4) {
        amount /= 1024;
        ++unit;
    }
    wchar_t text[80]{};
    swprintf(text, 80, unit ? L"%.2f %ls" : L"%.0f %ls", amount, units[unit]);
    return text;
}
std::wstring quote_argument(const std::wstring &argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'\"')
            result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
std::wstring state_text(State state, Language language) {
    switch (state) {
    case State::Connecting:
        return tr(language, L"正在连接…", L"Connecting…");
    case State::Authenticating:
        return tr(language, L"正在验证身份…", L"Authenticating…");
    case State::Configuring:
        return tr(language, L"正在配置 VPN 网络…", L"Configuring VPN network…");
    case State::Connected:
        return tr(language, L"已连接", L"Connected");
    case State::Reconnecting:
        return tr(language, L"正在重新连接…", L"Reconnecting…");
    case State::Disconnecting:
        return tr(language, L"正在断开连接…", L"Disconnecting…");
    case State::Failed:
        return tr(language, L"连接失败，请查看日志。", L"Connection failed. See the log.");
    default:
        return tr(language, L"未连接", L"Disconnected");
    }
}
void LogWindow::language(Language value) {
    language_ = value;
    if (!window_)
        return;
    SetWindowTextW(window_, tr(value, L"连接日志 — 布利杰VPN", L"Connection log — BulijieVPN"));
    label(LogCopy, tr(value, L"复制", L"Copy"));
    label(LogSelectAll, tr(value, L"全选", L"Select all"));
    label(LogClear, tr(value, L"清空", L"Clear"));
    label(IDCANCEL, tr(value, L"关闭", L"Close"));
    label(LogAutoScroll, tr(value, L"自动滚动", L"Auto-scroll"));
}
void LogWindow::show(HWND owner, Language value) {
    language_ = value;
    if (!window_)
        create(owner, L"BulijieVPN.Log", L"", 740, 430, WS_OVERLAPPEDWINDOW, WS_EX_CONTROLPARENT);
    language(value);
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(window_);
}
void LogWindow::update_text() {
    if (!window_)
        return;
    HWND text = item(LogText);
    LRESULT first = SendMessageW(text, EM_GETFIRSTVISIBLELINE, 0, 0);
    DWORD begin = 0, end = 0;
    SendMessageW(text, EM_GETSEL, reinterpret_cast<WPARAM>(&begin), reinterpret_cast<LPARAM>(&end));
    SetWindowTextW(text, content_.c_str());
    if (auto_scroll_) {
        SendMessageW(text, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(text, EM_SCROLLCARET, 0, 0);
    } else {
        SendMessageW(text, EM_SETSEL, begin, end);
        SendMessageW(text, EM_LINESCROLL, 0, first);
    }
}
void LogWindow::append(const std::wstring &line) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t prefix[24]{};
    swprintf(prefix, 24, L"[%02u:%02u:%02u] ", time.wHour, time.wMinute, time.wSecond);
    std::wstring normalized;
    for (wchar_t c : line) {
        if (c == L'\r')
            continue;
        if (c == L'\n')
            normalized += L'\r';
        normalized += c;
    }
    content_ += prefix;
    content_ += trim(normalized);
    content_ += L"\r\n";
    if (content_.size() > 512 * 1024) {
        size_t cut = content_.find(L'\n', content_.size() - 384 * 1024);
        content_.erase(0, cut == std::wstring::npos ? content_.size() / 2 : cut + 1);
    }
    update_text();
}
LRESULT LogWindow::message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CREATE) {
        edit(L"", LogText, 12, 12, 716, 366,
             ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
        SendMessageW(item(LogText), EM_SETLIMITTEXT, 1024 * 1024, 0);
        button(L"", LogCopy, 12, 392, 84);
        button(L"", LogSelectAll, 104, 392, 84);
        button(L"", LogClear, 196, 392, 84);
        button(L"", LogAutoScroll, 298, 392, 160, 27, BS_AUTOCHECKBOX);
        check(LogAutoScroll, auto_scroll_);
        button(L"", IDCANCEL, 644, 392, 84);
        language(language_);
        update_text();
        return 0;
    }
    if (message == WM_SIZE) {
        int width = MulDiv(LOWORD(lparam), 96, dpi_), height = MulDiv(HIWORD(lparam), 96, dpi_);
        move(LogText, 12, 12, std::max(200, width - 24), std::max(100, height - 64));
        for (int id : std::initializer_list<int>{LogCopy, LogSelectAll, LogClear, LogAutoScroll, IDCANCEL}) {
            RECT r{};
            GetWindowRect(item(id), &r);
            POINT p{r.left, r.top};
            ScreenToClient(window_, &p);
            int x = id == IDCANCEL ? width - 96 : MulDiv(p.x, 96, dpi_);
            move(id, x, height - 38, MulDiv(r.right - r.left, 96, dpi_), 27);
        }
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto *limits = reinterpret_cast<MINMAXINFO *>(lparam);
        limits->ptMinTrackSize = {px(620), px(280)};
        return 0;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
        ShowWindow(window_, SW_HIDE);
        return 0;
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(wparam)) {
        case LogCopy: {
            DWORD a = 0, b = 0;
            SendMessageW(item(LogText), EM_GETSEL, reinterpret_cast<WPARAM>(&a),
                         reinterpret_cast<LPARAM>(&b));
            if (a == b)
                copy_text(window_, content_);
            else
                SendMessageW(item(LogText), WM_COPY, 0, 0);
            return 0;
        }
        case LogSelectAll:
            SendMessageW(item(LogText), EM_SETSEL, 0, -1);
            SetFocus(item(LogText));
            return 0;
        case LogClear:
            content_.clear();
            update_text();
            return 0;
        case LogAutoScroll:
            auto_scroll_ = checked(LogAutoScroll);
            return 0;
        }
    }
    return Window::message(message, wparam, lparam);
}
} // namespace bulijie::ui
