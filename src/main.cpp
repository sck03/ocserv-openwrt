#include "common.h"
#include "session.h"
#include "settings.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <memory>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
using namespace bridge;
constexpr UINT kEvent = WM_APP + 1;
constexpr UINT kTrayEvent = WM_APP + 2;
constexpr UINT_PTR kTimer = 1;
const UINT kTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
enum Control { Server = 100, Username, Password, LanguageBox, ShowPassword, Remember, Connect, Configuration, About, CopyDetails,
               TrayShow=400, TrayDisconnect, TrayExit };
Handle instance_mutex;
struct App {
    HWND window = nullptr;
    HWND logo = nullptr;
    HWND title = nullptr, subtitle = nullptr, server_label = nullptr, server = nullptr, user_label = nullptr, username = nullptr;
    HWND password_label = nullptr, password = nullptr, language_box = nullptr, show_password = nullptr, remember = nullptr;
    HWND connect = nullptr, status = nullptr, traffic = nullptr, hint = nullptr, details = nullptr, configuration = nullptr, about = nullptr, copy_details = nullptr;
    HFONT font = nullptr, title_font = nullptr, status_font = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(245, 247, 251));
    Profile profile;
    Language language = Language::Chinese;
    State state = State::Idle;
    Error error = Error::None;
    std::unique_ptr<Session> session;
    std::wstring settings, profile_error, address, transport;
    uint64_t rx = 0, tx = 0;
    ULONGLONG connected_at = 0;
    int dpi = 96;
    bool closing = false;
    bool disconnect_requested = false;
    bool smoke = false;
    bool ready = false, updating_fields = false, password_from_store = false, diagnostics_visible = false;
    std::wstring restored_user;
    bool tray_added = false;
    std::wstring last_tooltip;
    std::wstring smoke_directory;
    ~App() { remove_tray(); session.reset(); if(font) DeleteObject(font); if(title_font) DeleteObject(title_font); if(status_font) DeleteObject(status_font); DeleteObject(background); }
    int scale(int value) const { return MulDiv(value, dpi, 96); }
    bool active() const { return static_cast<bool>(session); }
    const wchar_t* tr(const wchar_t* zh, const wchar_t* en) const { return choose(language, zh, en); }
    std::wstring get(HWND control, int max = 2048) {
        int size = std::min(GetWindowTextLengthW(control), max);
        std::wstring value(static_cast<size_t>(size) + 1, L'\0');
        int n = GetWindowTextW(control, value.data(), size + 1);
        value.resize(static_cast<size_t>(std::max(0, n)));
        return value;
    }
    HWND control(const wchar_t* cls, DWORD style, int id = 0, DWORD ex = 0) {
        auto handle = CreateWindowExW(ex, cls, L"", WS_CHILD | WS_VISIBLE | style, 0,0,0,0,window,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return handle;
    }
    void initialize() {
        HDC dc = GetDC(window);
        dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(window, dc);
        font = CreateFontW(-scale(16), 0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title_font = CreateFontW(-scale(30), 0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        status_font = CreateFontW(-scale(19), 0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title = control(L"STATIC", SS_LEFT);
        logo = control(L"STATIC",SS_ICON | SS_CENTERIMAGE);
        auto icon = LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(2),IMAGE_ICON,scale(40),scale(40),LR_SHARED);
        SendMessageW(logo,STM_SETIMAGE,IMAGE_ICON,reinterpret_cast<LPARAM>(icon));
        subtitle = control(L"STATIC", SS_LEFT);
        language_box = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, LanguageBox);
        SendMessageW(language_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"简体中文"));
        SendMessageW(language_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        server_label = control(L"STATIC", SS_LEFT);
        server = control(L"EDIT", ES_AUTOHSCROLL | WS_TABSTOP, Server, WS_EX_CLIENTEDGE);
        user_label = control(L"STATIC", SS_LEFT);
        username = control(L"EDIT", ES_AUTOHSCROLL | WS_TABSTOP, Username, WS_EX_CLIENTEDGE);
        password_label = control(L"STATIC", SS_LEFT);
        password = control(L"EDIT", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, Password, WS_EX_CLIENTEDGE);
        show_password = control(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, ShowPassword);
        remember = control(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, Remember);
        SendMessageW(remember,BM_SETCHECK,BST_CHECKED,0);
        connect = control(L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, Connect);
        status = control(L"STATIC", SS_LEFT);
        traffic = control(L"STATIC", SS_LEFT);
        hint = control(L"STATIC", SS_LEFT);
        details = control(L"EDIT", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 0, WS_EX_CLIENTEDGE);
        configuration = control(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, Configuration);
        about = control(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, About);
        copy_details = control(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, CopyDetails);
        SendMessageW(server, EM_SETLIMITTEXT, 1024, 0);
        SendMessageW(username, EM_SETLIMITTEXT, 256, 0);
        SendMessageW(password, EM_SETLIMITTEXT, 1024, 0);
        SendMessageW(details, EM_SETLIMITTEXT, 8192, 0);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(status_font), TRUE);
        SetWindowTextW(title, kProduct);
        load_profile(profile, profile_error, !smoke);
        language = profile.language;
        settings = settings_path();
        wchar_t value[2048]{};
        if (!settings.empty() && !smoke) {
            GetPrivateProfileStringW(L"UI", L"Language", language == Language::Chinese ? L"zh-CN" : L"en", value, 2048, settings.c_str());
            language = std::wstring(value) == L"en" ? Language::English : Language::Chinese;
            GetPrivateProfileStringW(L"User", L"Username", L"", value, 2048, settings.c_str());
            SetWindowTextW(username, value);
            bool saved = GetPrivateProfileIntW(L"User", L"RememberCredentials", 1, settings.c_str()) != 0;
            SendMessageW(remember, BM_SETCHECK, saved ? BST_CHECKED : BST_UNCHECKED, 0);
            if (!profile.lock_server) {
                GetPrivateProfileStringW(L"User", L"Server", profile.server.c_str(), value, 2048, settings.c_str());
                profile.server = value;
            }
        }
        SetWindowTextW(server, profile.server.c_str());
        SendMessageW(server, EM_SETREADONLY, profile.lock_server ? TRUE : FALSE, 0);
        SendMessageW(language_box, CB_SETCURSEL, language == Language::Chinese ? 0 : 1, 0);
        if (!is_admin()) SendMessageW(connect, BCM_SETSHIELD, 0, TRUE);
        ShowWindow(hint,SW_HIDE);
        ShowWindow(details,SW_HIDE);
        ShowWindow(copy_details,SW_HIDE);
        if (smoke) SetWindowTextW(username,L"");
        else restore_credentials();
        translate();
        layout();
        if (!profile_error.empty()) { display_error(Error::Internal,profile_error); EnableWindow(connect, FALSE); }
        ready = true;
        update_tray();
        SetTimer(window, kTimer, 1000, nullptr);
    }
    void save() {
        if (settings.empty() || smoke) return;
        // Create a UTF-16 INI so usernames survive on systems with different ANSI code pages.
        if (GetFileAttributesW(settings.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Handle file(CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file) { WORD bom = 0xfeff; DWORD bytes; WriteFile(file.get(), &bom, sizeof(bom), &bytes, nullptr); }
        }
        bool save_user = SendMessageW(remember, BM_GETCHECK, 0, 0) == BST_CHECKED;
        auto user = save_user ? get(username, 256) : L"";
        WritePrivateProfileStringW(L"User", L"Username", user.c_str(), settings.c_str());
        WritePrivateProfileStringW(L"User", L"RememberCredentials", save_user ? L"1" : L"0", settings.c_str());
        WritePrivateProfileStringW(L"User", L"Server", get(server).c_str(), settings.c_str());
        WritePrivateProfileStringW(L"UI", L"Language", language == Language::Chinese ? L"zh-CN" : L"en", settings.c_str());
    }
    void layout() {
        RECT rect{};
        GetClientRect(window, &rect);
        int width = MulDiv(rect.right, 96, dpi), content = width - 64;
        auto place = [&](HWND h, int x, int y, int w, int height) { MoveWindow(h, scale(x), scale(y), scale(w), scale(height), TRUE); };
        place(logo,32,20,40,40); place(title,84,20,320,40); place(language_box,width-167,24,135,160);
        place(subtitle,34,65,content,26);
        place(server_label,34,100,content,22); place(server,32,126,content,32);
        place(user_label,34,173,content,22); place(username,32,199,content,32);
        place(password_label,34,246,content,22); place(password,32,272,content,32);
        place(show_password,width-169,319,137,29);
        place(remember,32,320,content-150,26);
        place(connect,32,363,content,42);
        place(status,34,426,content,28); place(traffic,34,461,content,24);
        place(details,32,501,content,105);
        int footer = diagnostics_visible ? 627 : 516;
        place(configuration,32,footer,175,30); place(about,width-135,footer,103,30);
        place(copy_details,222,footer,190,30);
        int height = scale(diagnostics_visible ? 678 : 568);
        if (rect.bottom != height) {
            RECT outer{}; GetWindowRect(window,&outer);
            SetWindowPos(window,nullptr,0,0,outer.right-outer.left,height + (outer.bottom-outer.top-rect.bottom),SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    void translate() {
        SetWindowTextW(subtitle, tr(L"安全连接 · 简单使用", L"Secure access. Simple to use."));
        SetWindowTextW(server_label, tr(L"服务器", L"Server"));
        SetWindowTextW(user_label, tr(L"账号", L"Username"));
        SetWindowTextW(password_label, tr(L"密码", L"Password"));
        SetWindowTextW(show_password, tr(L"显示密码", L"Show password"));
        SetWindowTextW(remember, tr(L"记住账号和密码", L"Remember username and password"));
        SetWindowTextW(configuration, tr(L"导入配置", L"Import profile"));
        SetWindowTextW(about, tr(L"关于", L"About"));
        SetWindowTextW(copy_details, tr(L"复制诊断", L"Copy diagnostics"));
        refresh();
    }
    static std::wstring bytes(uint64_t count) {
        wchar_t text[64]{};
        if (count < 1024) swprintf(text, 64, L"%llu B", static_cast<unsigned long long>(count));
        else if (count < 1024*1024) swprintf(text, 64, L"%.1f KiB", static_cast<double>(count)/1024.0);
        else swprintf(text, 64, L"%.1f MiB", static_cast<double>(count)/1048576.0);
        return text;
    }
    void refresh() {
        const wchar_t* action = active() ? (state == State::Connected || state == State::Reconnecting ? tr(L"断开连接", L"Disconnect") : tr(L"取消连接", L"Cancel")) : tr(L"连接", L"Connect");
        SetWindowTextW(connect, action);
        std::wstring status_text = state_text(state, language);
        if (state == State::Connected && !address.empty()) status_text += L"  ·  " + address;
        if ((state == State::Connected || state == State::Reconnecting) && connected_at)
            status_text += L"  ·  " + connection_duration((GetTickCount64() - connected_at) / 1000);
        SetWindowTextW(status, status_text.c_str());
        std::wstring line = tr(L"下载 ", L"Received ") + bytes(rx) + tr(L"    上传 ", L"    Sent ") + bytes(tx);
        if (!transport.empty()) line += L"    " + transport;
        SetWindowTextW(traffic, line.c_str());
        EnableWindow(server, !active()); EnableWindow(username, !active()); EnableWindow(password, !active());
        EnableWindow(remember, !active()); EnableWindow(show_password, !active()); EnableWindow(configuration, !active());
        EnableWindow(connect, !closing && profile_error.empty() && state != State::Disconnecting);
        InvalidateRect(status, nullptr, TRUE);
        update_tray();
    }
    bool update_tray() {
        if (smoke || !window) return false;
        std::wstring tip=std::wstring(kProduct)+L" · "+state_text(state,language);
        if (tray_added && tip==last_tooltip) return true;
        NOTIFYICONDATAW icon{};
        icon.cbSize=sizeof(icon);
        icon.hWnd=window; icon.uID=1;
        icon.uFlags=NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        icon.uCallbackMessage=kTrayEvent;
        icon.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(2));
        wcsncpy(icon.szTip,tip.c_str(),sizeof(icon.szTip)/sizeof(wchar_t)-1);
        bool ok=Shell_NotifyIconW(tray_added ? NIM_MODIFY : NIM_ADD,&icon) != FALSE;
        if (ok && !tray_added) { icon.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&icon); }
        if (ok) { tray_added=true; last_tooltip=tip; }
        return ok;
    }
    void remove_tray() {
        if (!tray_added) return;
        NOTIFYICONDATAW icon{}; icon.cbSize=sizeof(icon); icon.hWnd=window; icon.uID=1;
        Shell_NotifyIconW(NIM_DELETE,&icon);
        tray_added=false;
    }
    void minimize_to_tray() { if (update_tray()) ShowWindow(window,SW_HIDE); }
    void restore_window() { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
    void tray_menu() {
        HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_STRING,TrayShow,tr(L"显示窗口",L"Show window"));
        AppendMenuW(menu,MF_STRING | (active() && state!=State::Disconnecting ? MF_ENABLED : MF_GRAYED),TrayDisconnect,
            state==State::Connected || state==State::Reconnecting ? tr(L"断开连接",L"Disconnect") : tr(L"取消连接",L"Cancel connection"));
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,TrayExit,tr(L"退出",L"Exit"));
        POINT point{}; GetCursorPos(&point);
        SetForegroundWindow(window);
        TrackPopupMenu(menu,TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        PostMessageW(window,WM_NULL,0,0);
        DestroyMenu(menu);
    }
    void display_error(Error code, const std::wstring& detail = {}) {
        error = code;
        auto text = error_text(code, language);
        if (!detail.empty()) text += L"\r\n" + detail;
        SetWindowTextW(details, text.c_str());
        diagnostics_visible = true;
        ShowWindow(details,SW_SHOW);
        ShowWindow(copy_details,SW_SHOW);
        layout();
    }
    void clear_error() {
        error = Error::None;
        SetWindowTextW(details,L"");
        diagnostics_visible = false;
        ShowWindow(details,SW_HIDE);
        ShowWindow(copy_details,SW_HIDE);
        layout();
    }
    void copy_diagnostics() {
        auto user = utf8(get(username));
        auto wide_pass = get(password);
        auto pass = utf8(wide_pass);
        erase(wide_pass);
        auto diagnostic = redacted(utf8(get(details, 8192)), user, pass);
        erase(pass);
        std::wstring text = std::wstring(kProduct) + L" " + kVersion + L"\r\n" + state_text(state, language) +
            L"\r\nOpenConnect " + wide(openconnect_get_version()) + L"\r\n" + wide(diagnostic);
        if (!OpenClipboard(window)) return;
        SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void* target = GlobalLock(memory);
            if (target) {
                std::memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) memory = nullptr;
            }
            if (memory) GlobalFree(memory);
        }
        CloseClipboard();
    }
    void restore_credentials() {
        if (smoke || SendMessageW(remember,BM_GETCHECK,0,0) != BST_CHECKED) return;
        Profile selected = profile;
        selected.server = get(server);
        std::wstring user, pass;
        if (read_credentials(selected,user,pass)) {
            updating_fields = true;
            SetWindowTextW(username,user.c_str());
            SetWindowTextW(password,pass.c_str());
            updating_fields = false;
            restored_user = user;
            password_from_store = true;
            erase(pass);
        }
    }
    void import_profile() {
        std::vector<wchar_t> filename(32768);
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = language==Language::Chinese ? L"VPN 连接配置 (*.bvpn)\0*.bvpn\0CA 证书 (*.cer;*.crt;*.pem)\0*.cer;*.crt;*.pem\0\0" : L"VPN profile (*.bvpn)\0*.bvpn\0CA certificate (*.cer;*.crt;*.pem)\0*.cer;*.crt;*.pem\0\0";
        dialog.lpstrFile = filename.data();
        dialog.nMaxFile = static_cast<DWORD>(filename.size());
        dialog.lpstrTitle = tr(L"导入连接配置或 CA 证书",L"Import a connection profile or CA certificate");
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
        if (!GetOpenFileNameW(&dialog)) return;
        Profile selected = profile;
        selected.server = get(server);
        std::wstring message;
        if (!import_connection(filename.data(),selected,message)) { display_error(Error::Certificate,message); return; }
        profile_error.clear();
        if (!load_profile(profile,profile_error)) { display_error(Error::Internal,profile_error); return; }
        updating_fields = true;
        SetWindowTextW(server,profile.server.c_str());
        SendMessageW(server,EM_SETREADONLY,profile.lock_server,0);
        SetWindowTextW(password,L"");
        updating_fields = false;
        password_from_store = false;
        restore_credentials();
        clear_error();
        refresh();
        SetWindowTextW(status,tr(L"配置已导入",L"Profile imported"));
        save();
    }
    void elevate() {
        save();
        SetWindowTextW(password, L"");
        instance_mutex.reset();
        auto path = executable_path(), directory = executable_dir();
        SHELLEXECUTEINFOW execute{};
        execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS;
        execute.hwnd = window;
        execute.lpVerb = L"runas";
        execute.lpFile = path.c_str();
        execute.lpParameters = L"--elevated";
        execute.lpDirectory = directory.c_str();
        execute.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&execute)) {
            if (execute.hProcess) CloseHandle(execute.hProcess);
            DestroyWindow(window);
        } else {
            DWORD error = GetLastError();
            instance_mutex.reset(CreateMutexW(nullptr, FALSE, L"Local\\BridgeVPN.NativeClient"));
            display_error(Error::Administrator, system_error(error));
        }
    }
    void connect_clicked() {
        if (active()) { disconnect_requested = true; state = State::Disconnecting; session->cancel(); refresh(); return; }
        Profile current_profile;
        profile_error.clear();
        if (!load_profile(current_profile, profile_error, !smoke)) { display_error(Error::Internal, profile_error); return; }
        profile = std::move(current_profile);
        if (profile.lock_server && get(server) != profile.server) { updating_fields = true; SetWindowTextW(server, profile.server.c_str()); updating_fields = false; }
        SendMessageW(server, EM_SETREADONLY, profile.lock_server ? TRUE : FALSE, 0);
        std::wstring normalized;
        if (!normalize_server(get(server), normalized)) { display_error(Error::InvalidServer); SetFocus(server); return; }
        auto user = trim(get(username, 256));
        auto pass = get(password, 1024);
        if (user.empty() || pass.empty()) { erase(pass); display_error(Error::MissingCredentials); SetFocus(user.empty() ? username : password); return; }
        profile.server = normalized;
        DWORD credential_error = 0;
        if (SendMessageW(remember,BM_GETCHECK,0,0) == BST_CHECKED) {
            if (!save_credentials(profile,user,pass,credential_error)) { erase(pass); display_error(Error::Internal,system_error(credential_error)); return; }
        } else forget_credentials(profile,credential_error);
        save();
        if (!is_admin()) { erase(pass); elevate(); return; }
        ConnectOptions options;
        options.profile = profile;
        options.username = utf8(user);
        options.password = utf8(pass);
        erase(pass);
        SetWindowTextW(password, L"");
        clear_error();
        rx = tx = 0; address.clear(); transport.clear(); connected_at = 0;
        disconnect_requested = false;
        session = std::make_unique<Session>(std::move(options), [target=window](Event event) {
            auto message = std::make_unique<Event>(std::move(event));
            if (PostMessageW(target, kEvent, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
        if (!session->start()) { session.reset(); display_error(Error::Internal, system_error(GetLastError())); }
        else { state = State::Connecting; error = Error::None; }
        refresh();
    }
    void event(Event event) {
        // A queued statistics or handshake event must not undo a user's cancel/close.
        state = disconnect_requested && !event.terminal ? State::Disconnecting : event.state;
        if (event.statistics) { rx = event.rx; tx = event.tx; address = event.address; transport = event.transport; }
        else if (state == State::Connected && !connected_at) connected_at = GetTickCount64();
        if (event.error != Error::None && (!disconnect_requested || event.terminal)) display_error(event.error, event.detail);
        if (event.terminal) {
            session.reset();
            disconnect_requested = false;
            address.clear(); transport.clear();
            if (closing) { DestroyWindow(window); return; }
            // Restore from Credential Manager only after the worker has discarded
            // its plaintext password. Reconnect can then use one click again.
            if (event.error != Error::Authentication) restore_credentials();
            SetFocus(GetWindowTextLengthW(password) == 0 ? password : connect);
        }
        refresh();
    }
    bool capture(const std::wstring& file) {
        RECT rect{}; GetClientRect(window, &rect);
        HDC dc = GetDC(window), memory = CreateCompatibleDC(dc);
        BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = rect.right; bitmap.bmiHeader.biHeight = -rect.bottom;
        bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!image || !memory) { if(image) DeleteObject(image); if(memory) DeleteDC(memory); ReleaseDC(window,dc); return false; }
        auto old = SelectObject(memory, image);
        PrintWindow(window, memory, PW_CLIENTONLY);
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        DWORD size = static_cast<DWORD>(rect.right * rect.bottom * 4);
        header.bfSize = header.bfOffBits + size;
        Handle out(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD n = 0;
        bool ok = out && WriteFile(out.get(), &header, sizeof(header), &n, nullptr) && WriteFile(out.get(), &bitmap.bmiHeader, sizeof(BITMAPINFOHEADER), &n, nullptr) && WriteFile(out.get(), pixels, size, &n, nullptr);
        SelectObject(memory, old); DeleteObject(image); DeleteDC(memory); ReleaseDC(window, dc);
        return ok;
    }
};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, w, l);
    if (message==kTaskbarCreated) { app->tray_added=false; app->update_tray(); return 0; }
    switch (message) {
    case WM_CREATE: app->initialize(); return 0;
    case WM_SIZE:
        if (w==SIZE_MINIMIZED) { app->minimize_to_tray(); return 0; }
        if (app->title) app->layout();
        return 0;
    case WM_CTLCOLORSTATIC: {
        SetBkColor(reinterpret_cast<HDC>(w), RGB(245,247,251));
        SetTextColor(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l) == app->status && app->state == State::Connected ? RGB(20,121,87) : RGB(31,45,63));
        return reinterpret_cast<LRESULT>(app->background);
    }
    case WM_ERASEBKGND: { RECT rect{}; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(w), &rect, app->background); return 1; }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case TrayShow: app->restore_window(); break;
        case TrayDisconnect: if(app->session) app->connect_clicked(); break;
        case TrayExit: SendMessageW(window,WM_CLOSE,0,0); break;
        case Connect: if (HIWORD(w) == BN_CLICKED) app->connect_clicked(); break;
        case LanguageBox:
            if (HIWORD(w) == CBN_SELCHANGE) { app->language = SendMessageW(app->language_box, CB_GETCURSEL, 0,0) == 0 ? Language::Chinese : Language::English; app->translate(); app->save(); }
            break;
        case ShowPassword:
            SendMessageW(app->password, EM_SETPASSWORDCHAR, SendMessageW(app->show_password,BM_GETCHECK,0,0) == BST_CHECKED ? 0 : 0x25cf, 0);
            InvalidateRect(app->password,nullptr,TRUE); break;
        case Remember:
            if (HIWORD(w) == BN_CLICKED) {
                if (SendMessageW(app->remember,BM_GETCHECK,0,0) != BST_CHECKED) {
                    Profile selected = app->profile; selected.server = app->get(app->server);
                    DWORD error = 0; forget_credentials(selected,error);
                } else if (app->get(app->password).empty()) app->restore_credentials();
                app->save();
            }
            break;
        case Server:
            if (app->ready && !app->updating_fields && HIWORD(w) == EN_CHANGE) { SetWindowTextW(app->password,L""); app->password_from_store=false; }
            if (app->ready && !app->updating_fields && HIWORD(w) == EN_KILLFOCUS && app->get(app->password).empty()) app->restore_credentials();
            break;
        case Username:
            if (app->ready && !app->updating_fields && app->password_from_store && HIWORD(w) == EN_CHANGE && app->get(app->username) != app->restored_user) {
                SetWindowTextW(app->password,L""); app->password_from_store=false;
            }
            break;
        case Configuration: app->import_profile(); break;
        case CopyDetails: app->copy_diagnostics(); break;
        case About:
            MessageBoxW(window, app->tr(L"布利杰VPN 0.3.0\n原生 Windows OpenConnect 客户端\n支持目标：Windows 7 SP1 及以上，x86 / x64\n\nOpenConnect 9.21 · OpenSSL 3.5.8\nWintun 0.14.1（官方签名驱动）\n\n源代码与许可证随发行包提供。", L"布利杰VPN 0.3.0\nNative Windows OpenConnect client\nTarget: Windows 7 SP1 and later, x86 / x64\n\nOpenConnect 9.21 · OpenSSL 3.5.8\nWintun 0.14.1 (official signed driver)\n\nSource and licenses accompany the release."), kProduct, MB_OK | MB_ICONINFORMATION); break;
        }
        return 0;
    case kEvent: { std::unique_ptr<Event> event(reinterpret_cast<Event*>(l)); app->event(std::move(*event)); return 0; }
    case kTrayEvent:
        switch(LOWORD(l)) {
        case NIN_SELECT: case NIN_KEYSELECT: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: app->restore_window(); break;
        case WM_CONTEXTMENU: case WM_RBUTTONUP: app->tray_menu(); break;
        }
        return 0;
    case WM_TIMER:
        if (!app->tray_added) app->update_tray();
        if (w == kTimer && app->session && (app->state == State::Connected || app->state == State::Reconnecting)) app->session->request_stats();
        return 0;
    case WM_CLOSE:
        app->save();
        if (app->session) { app->closing = true; app->disconnect_requested = true; app->state = State::Disconnecting; app->session->cancel(); app->refresh(); }
        else DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION: if(app->session) app->session->cancel(); return TRUE;
    case WM_DESTROY: app->remove_tray(); KillTimer(window, kTimer); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    bool smoke = count == 3 && std::wstring(args[1]) == L"--smoke-test";
    std::wstring output = smoke ? args[2] : L"";
    if (args) LocalFree(args);
    if (!smoke) {
        instance_mutex.reset(CreateMutexW(nullptr, FALSE, L"Local\\BridgeVPN.NativeClient"));
        if (!instance_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (auto current = FindWindowW(L"BridgeVPNWindow", nullptr)) { ShowWindow(current, SW_RESTORE); SetForegroundWindow(current); }
            return 0;
        }
    }
    SetDllDirectoryW(L"");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    SetProcessDPIAware();
    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2,2), &sockets) || openconnect_init_ssl()) {
        MessageBoxW(nullptr, L"Network initialization failed. / 网络初始化失败。", kProduct, MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    App app;
    app.smoke = smoke;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = window_proc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(2));
    cls.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(2),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    cls.hbrBackground = app.background; cls.lpszClassName = L"BridgeVPNWindow";
    if (!RegisterClassExW(&cls)) { WSACleanup(); return 1; }
    HDC dc = GetDC(nullptr); int dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr,dc);
    RECT rect{0,0,MulDiv(620,dpi,96),MulDiv(568,dpi,96)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work_area,0);
    int x = static_cast<int>(work_area.left + std::max(0L, (work_area.right-work_area.left-(rect.right-rect.left))/2));
    int y = static_cast<int>(work_area.top + std::max(0L, (work_area.bottom-work_area.top-(rect.bottom-rect.top))/2));
    auto window = CreateWindowExW(smoke ? WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE : 0, cls.lpszClassName, kProduct, style, smoke ? -32000 : x,smoke ? -32000 : y,
                                 rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&app);
    if (!window) { WSACleanup(); return 1; }
    if (smoke) {
        CreateDirectoryW(output.c_str(), nullptr);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        bool ok = true;
        for (Language language : {Language::Chinese, Language::English}) {
            app.language = language;
            SendMessageW(app.language_box, CB_SETCURSEL, language == Language::Chinese ? 0 : 1, 0);
            app.translate();
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            ok = app.capture(output + (language == Language::Chinese ? L"\\ui-zh.bmp" : L"\\ui-en.bmp")) && ok;
        }
        app.language=Language::Chinese;
        SendMessageW(app.language_box,CB_SETCURSEL,0,0);
        app.translate();
        SetWindowTextW(app.username,L""); SetWindowTextW(app.password,L"");
        app.connect_clicked();
        ok = ok && !app.active() && app.error==Error::MissingCredentials;
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        ok = app.capture(output+L"\\ui-empty-credentials.bmp") && ok;
        app.disconnect_requested=true;
        Event stale_stats; stale_stats.statistics=true; stale_stats.state=State::Connected;
        app.event(std::move(stale_stats));
        ok = ok && app.state==State::Disconnecting;
        Event stale_handshake; stale_handshake.state=State::Authenticating;
        app.event(std::move(stale_handshake));
        ok = ok && app.state==State::Disconnecting;
        Event stopped; stopped.state=State::Idle; stopped.terminal=true;
        app.event(std::move(stopped));
        ok = ok && app.state==State::Idle && !app.disconnect_requested;
        app.clear_error();
        app.state=State::Connected; app.address=L"10.77.0.2"; app.connected_at=GetTickCount64()-65000;
        app.rx=7654321; app.tx=1234567; app.transport=L"UDP / DTLS"; app.refresh();
        SetWindowTextW(app.connect,app.tr(L"断开连接",L"Disconnect"));
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        ok = app.capture(output+L"\\ui-connected.bmp") && ok;
        DestroyWindow(window);
        WSACleanup();
        return ok ? 0 : 1;
    }
    ShowWindow(window, show); UpdateWindow(window);
    SetFocus(app.username);
    MSG message{};
    while (GetMessageW(&message,nullptr,0,0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
            (message.hwnd == app.password || message.hwnd == app.username || message.hwnd == app.server)) app.connect_clicked();
        else if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    app.session.reset();
    WSACleanup();
    return static_cast<int>(message.wParam);
}
