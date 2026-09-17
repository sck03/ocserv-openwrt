#include "application.h"
#include <shellapi.h>
#include <algorithm>

namespace bulijie {
using namespace ui;
namespace {
constexpr UINT SessionMessage = WM_APP + 20, TrayMessage = WM_APP + 21, AutoConnectMessage = WM_APP + 22;
constexpr int ServerCaption = 510, InfoCaption = 520, InfoValue = 540;
constexpr wchar_t ProfileFilter[] =
    L"VPN profiles / VPN 配置 (*.bvpn;*.ini)\0*.bvpn;*.ini\0All files / 所有文件\0*.*\0\0";
void menu_item(HMENU menu, UINT id, const wchar_t *label, bool checked = false) {
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), id, label);
}
void submenu(HMENU menu, HMENU child, const wchar_t *label) {
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(child), label);
}
} // namespace
Application::Application(std::filesystem::path directory) : store_(std::move(directory)) {}
Application::~Application() {
    if (session_) {
        session_->cancel();
        session_.reset();
    }
    remove_tray();
    if (accelerators_)
        DestroyAcceleratorTable(accelerators_);
}
bool Application::claim_instance() {
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t c : store_.directory().wstring()) {
        hash ^= static_cast<uint64_t>(std::towlower(c));
        hash *= 1099511628211ULL;
    }
    instance_name_ =
        L"Local\\BulijieVPN.App." + std::to_wstring(hash) + (administrator() ? L".admin" : L".user");
    instance_.reset(CreateMutexW(nullptr, FALSE, instance_name_.c_str()));
    if (!instance_) {
        error(nullptr, system_error(GetLastError()));
        return false;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS || !preferences_.single_instance)
        return true;
    struct Search {
        const wchar_t *property;
        HWND window = nullptr;
        static BOOL CALLBACK visit(HWND window, LPARAM data) {
            auto *search = reinterpret_cast<Search *>(data);
            if (GetPropW(window, search->property)) {
                search->window = window;
                return FALSE;
            }
            return TRUE;
        }
    } search{instance_name_.c_str()};
    EnumWindows(Search::visit, reinterpret_cast<LPARAM>(&search));
    if (search.window) {
        ShowWindow(search.window, SW_RESTORE);
        SetForegroundWindow(GetLastActivePopup(search.window));
    }
    return false;
}
int Application::run(int show, const std::string &connect_profile) {
    std::wstring reason;
    if (!store_.load(profiles_, preferences_, reason)) {
        error(nullptr, reason);
        return 1;
    }
    if (!claim_instance())
        return 0;
    auto_connect_ = connect_profile;
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!create(nullptr, L"BulijieVPN.Main", Product, 480, 264,
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, WS_EX_CONTROLPARENT,
                CreateMenu())) {
        error(nullptr, system_error(GetLastError()));
        return 1;
    }
    SetPropW(window_, instance_name_.c_str(), reinterpret_cast<HANDLE>(1));
    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'N', NewProfile},   {FCONTROL | FSHIFT | FVIRTKEY, 'N', NewAdvanced},
        {FCONTROL | FVIRTKEY, 'E', EditSelected}, {FCONTROL | FVIRTKEY, 'R', RemoveSelected},
        {FCONTROL | FVIRTKEY, 'L', ShowLog},      {FCONTROL | FVIRTKEY, 'Q', Quit}};
    accelerators_ = CreateAcceleratorTableW(keys, static_cast<int>(sizeof(keys) / sizeof(keys[0])));
    ShowWindow(window_, show == SW_HIDE ? SW_SHOWNORMAL : show);
    tray(true);
    if (preferences_.start_minimized && auto_connect_.empty())
        minimize();
    SetTimer(window_, 1, 250, nullptr);
    if (!auto_connect_.empty())
        PostMessageW(window_, AutoConnectMessage, 0, 0);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(window_, accelerators_, &message))
            continue;
        if (log_.handle() && IsDialogMessageW(log_.handle(), &message))
            continue;
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
void Application::create_controls() {
    control(WC_TABCONTROLW, L"", WS_TABSTOP, Tabs, 12, 10, 456, 218);
    control(L"STATIC", L"", SS_ICON, MainIcon, 34, 80, 64, 64);
    HICON icon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(2), IMAGE_ICON, px(64), px(64), LR_SHARED));
    SendMessageW(item(MainIcon), STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
    control(L"STATIC", L"", 0, ServerCaption, 120, 79, 64, 24);
    HWND servers = combo(Servers, 184, 76, 184, true);
    SendMessageW(servers, CB_LIMITTEXT, 4096, 0);
    button(L"", EditProfile, 376, 76, 72);
    button(L"", Connect, 184, 119, 150, 31, BS_DEFPUSHBUTTON);
    button(L"", ViewLog, 342, 119, 106, 31);
    control(L"STATIC", L"", SS_PATHELLIPSIS, Gateway, 120, 169, 328, 38);
    for (int i = 0; i < 7; ++i) {
        control(L"STATIC", L"", 0, InfoCaption + i, 28, 46 + i * 24, 112, 22);
        control(L"EDIT", L"—", ES_READONLY | ES_AUTOHSCROLL, InfoValue + i, 148, 44 + i * 24, 296, 23);
    }
    control(L"STATIC", L"", SS_LEFTNOWORDWRAP, Status, 16, 239, 448, 24);
    translate();
    reload();
}
void Application::rebuild_menu() {
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), profiles = CreatePopupMenu(),
          view = CreatePopupMenu(), settings = CreatePopupMenu(), levels = CreatePopupMenu(),
          languages = CreatePopupMenu(), help = CreatePopupMenu();
    menu_item(file, Quit, t(L"退出(&Q)\tCtrl+Q", L"&Quit\tCtrl+Q"));
    menu_item(profiles, NewProfile, t(L"新建配置(&N)…\tCtrl+N", L"&New profile…\tCtrl+N"));
    menu_item(profiles, NewAdvanced,
              t(L"新建高级配置…\tCtrl+Shift+N", L"New profile (advanced)…\tCtrl+Shift+N"));
    menu_item(profiles, EditSelected, t(L"编辑所选配置(&E)…\tCtrl+E", L"&Edit selected profile…\tCtrl+E"));
    menu_item(profiles, RemoveSelected,
              t(L"删除所选配置(&R)…\tCtrl+R", L"&Remove selected profile…\tCtrl+R"));
    AppendMenuW(profiles, MF_SEPARATOR, 0, nullptr);
    menu_item(profiles, ImportProfile, t(L"导入配置…", L"Import profile…"));
    menu_item(profiles, ExportProfile, t(L"导出配置…", L"Export profile…"));
    menu_item(view, ShowLog, t(L"日志窗口(&L)\tCtrl+L", L"&Log window\tCtrl+L"));
    menu_item(view, Minimize, t(L"最小化", L"Minimize"));
    menu_item(view, Restore, t(L"还原", L"Restore"));
    menu_item(settings, MinimizeToTray, t(L"最小化到通知区域", L"Minimize to notification area"),
              preferences_.minimize_to_tray);
    menu_item(settings, MinimizeOnClose, t(L"关闭窗口时最小化", L"Minimize instead of closing"),
              preferences_.minimize_instead_of_close);
    menu_item(settings, StartMinimized, t(L"启动时最小化", L"Start minimized"), preferences_.start_minimized);
    menu_item(settings, SingleInstance, t(L"单实例模式", L"Single instance mode"),
              preferences_.single_instance);
    menu_item(levels, LogError, t(L"错误", L"Error"), preferences_.log_level == 0);
    menu_item(levels, LogInfo, t(L"信息", L"Info"), preferences_.log_level == 1);
    menu_item(levels, LogDebug, t(L"调试", L"Debug"), preferences_.log_level == 2);
    menu_item(levels, LogTrace, t(L"跟踪", L"Trace"), preferences_.log_level == 3);
    submenu(settings, levels, t(L"日志级别", L"Log level"));
    menu_item(languages, Chinese, L"简体中文", preferences_.language == Language::Chinese);
    menu_item(languages, English, L"English", preferences_.language == Language::English);
    submenu(settings, languages, t(L"语言", L"Language"));
    menu_item(help, Updates, t(L"检查更新", L"Check for updates"));
    menu_item(help, Website, t(L"OpenConnect 网站", L"OpenConnect website"));
    menu_item(help, ReportIssue, t(L"报告问题", L"Report an issue"));
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    menu_item(help, License, t(L"许可证信息", L"License information"));
    menu_item(help, About, t(L"关于布利杰VPN", L"About BulijieVPN"));
    submenu(menu, file, t(L"文件(&F)", L"&File"));
    submenu(menu, profiles, t(L"配置(&P)", L"&Profiles"));
    submenu(menu, view, t(L"查看(&V)", L"&View"));
    submenu(menu, settings, t(L"设置(&S)", L"&Settings"));
    submenu(menu, help, t(L"帮助(&H)", L"&Help"));
    HMENU previous = GetMenu(window_);
    SetMenu(window_, menu);
    if (previous)
        DestroyMenu(previous);
    DrawMenuBar(window_);
}
void Application::translate() {
    rebuild_menu();
    SetWindowTextW(window_, preferences_.language == Language::Chinese ? L"布利杰VPN" : L"BulijieVPN");
    int selection = std::max(0, TabCtrl_GetCurSel(item(Tabs)));
    TabCtrl_DeleteAllItems(item(Tabs));
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    tab.pszText = const_cast<wchar_t *>(t(L"主界面", L"Main"));
    TabCtrl_InsertItem(item(Tabs), 0, &tab);
    tab.pszText = const_cast<wchar_t *>(t(L"VPN 信息", L"VPN Info"));
    TabCtrl_InsertItem(item(Tabs), 1, &tab);
    TabCtrl_SetCurSel(item(Tabs), selection);
    label(ServerCaption, t(L"服务器：", L"Server:"));
    label(EditProfile, t(L"编辑", L"Edit"));
    label(ViewLog, t(L"查看日志", L"View log"));
    const wchar_t *zh[] = {L"IPv4：",      L"IPv6：", L"DNS：", L"加密方式：",
                           L"DTLS 加密：", L"下载：", L"上传："};
    const wchar_t *en[] = {L"IPv4:",        L"IPv6:",     L"DNS:",   L"Cipher:",
                           L"DTLS cipher:", L"Download:", L"Upload:"};
    for (int i = 0; i < 7; ++i)
        label(InfoCaption + i, t(zh[i], en[i]));
    log_.language(preferences_.language);
    select_tab();
    update_controls();
}
const Profile *Application::selected() const {
    LRESULT index = SendMessageW(item(Servers), CB_GETCURSEL, 0, 0);
    if (index >= 0 && static_cast<size_t>(index) < profiles_.size() &&
        value(Servers) == profiles_[static_cast<size_t>(index)].name)
        return &profiles_[static_cast<size_t>(index)];
    return nullptr;
}
bool Application::reload(const std::string &id) {
    std::wstring reason;
    Preferences current;
    std::vector<Profile> profiles;
    if (!store_.load(profiles, current, reason)) {
        error(window_, reason);
        return false;
    }
    profiles_ = std::move(profiles);
    preferences_ = current;
    SendMessageW(item(Servers), CB_RESETCONTENT, 0, 0);
    int index = -1;
    const auto &wanted = id.empty() ? preferences_.selected : id;
    for (size_t i = 0; i < profiles_.size(); ++i) {
        add_choice(item(Servers), profiles_[i].name, static_cast<LPARAM>(i));
        if (profiles_[i].id == wanted)
            index = static_cast<int>(i);
    }
    if (index < 0 && !profiles_.empty())
        index = 0;
    SendMessageW(item(Servers), CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    if (index < 0)
        SetWindowTextW(item(Servers), L"");
    update_controls();
    return true;
}
void Application::select_tab() {
    bool main = TabCtrl_GetCurSel(item(Tabs)) == 0;
    for (int id :
         std::initializer_list<int>{Servers, EditProfile, Connect, ViewLog, Gateway, MainIcon, ServerCaption})
        ShowWindow(item(id), main ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < 7; ++i) {
        ShowWindow(item(InfoCaption + i), main ? SW_HIDE : SW_SHOW);
        ShowWindow(item(InfoValue + i), main ? SW_HIDE : SW_SHOW);
    }
}
void Application::update_controls() {
    bool active = busy();
    const Profile *profile = selected();
    std::wstring normalized;
    bool valid = profile || normalize_gateway(value(Servers), normalized);
    EnableWindow(item(Servers), !active && !closing_);
    EnableWindow(item(EditProfile), profile && !active && !closing_);
    EnableWindow(item(Connect), !closing_ && (active || valid));
    label(Connect, active ? (state_ == State::Connected || state_ == State::Reconnecting
                                 ? t(L"断开连接", L"Disconnect")
                                 : t(L"取消", L"Cancel"))
                          : t(L"连接", L"Connect"));
    label(Status, state_text(state_, preferences_.language));
    label(Gateway, active ? active_gateway_
                   : profile
                       ? profile->gateway
                       : t(L"输入网关地址，或从“配置”菜单新建。", L"Enter a gateway or create a profile."));
    HMENU menu = GetMenu(window_);
    for (int id : {EditSelected, RemoveSelected, ExportProfile})
        EnableMenuItem(menu, id, MF_BYCOMMAND | ((profile && !active && !closing_) ? MF_ENABLED : MF_GRAYED));
    for (int id : {NewProfile, NewAdvanced, ImportProfile})
        EnableMenuItem(menu, id, MF_BYCOMMAND | ((!active && !closing_) ? MF_ENABLED : MF_GRAYED));
    tray();
}
void Application::save_preferences() {
    std::wstring reason;
    if (!store_.save_preferences(preferences_, reason))
        error(window_, reason);
}
void Application::command(int id) {
    if (id == Quit) {
        shutdown();
        return;
    }
    if (id == Connect) {
        if (busy())
            disconnect();
        else
            connect();
        return;
    }
    if (id == ViewLog || id == ShowLog) {
        log_.show(window_, preferences_.language);
        return;
    }
    if (id == Restore) {
        restore();
        return;
    }
    if (id == Minimize) {
        minimize();
        return;
    }
    if (id == About || id == License) {
        show_about(window_, preferences_.language, id == License);
        return;
    }
    if (id == Updates || id == Website || id == ReportIssue) {
        const wchar_t *url = id == Updates   ? L"https://github.com/sck03/ocserv-openwrt/releases"
                             : id == Website ? L"https://www.infradead.org/openconnect/"
                                             : L"https://github.com/sck03/ocserv-openwrt/issues";
        auto result = ShellExecuteW(window_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
            error(window_, t(L"无法打开浏览器。", L"Could not open the browser."));
        return;
    }
    if (id >= MinimizeToTray && id <= LogTrace) {
        if (id == MinimizeToTray)
            preferences_.minimize_to_tray = !preferences_.minimize_to_tray;
        else if (id == MinimizeOnClose)
            preferences_.minimize_instead_of_close = !preferences_.minimize_instead_of_close;
        else if (id == StartMinimized)
            preferences_.start_minimized = !preferences_.start_minimized;
        else if (id == SingleInstance)
            preferences_.single_instance = !preferences_.single_instance;
        else if (id == Chinese || id == English)
            preferences_.language = id == Chinese ? Language::Chinese : Language::English;
        else if (id >= LogError && id <= LogTrace) {
            preferences_.log_level = id - LogError;
            if (session_)
                session_->set_log_level(preferences_.log_level);
        }
        save_preferences();
        translate();
        return;
    }
    if (busy() || closing_)
        return;
    const Profile *profile = selected();
    if (id == NewProfile || id == NewAdvanced || id == EditProfile || id == EditSelected) {
        bool edit = id == EditProfile || id == EditSelected;
        if (edit && !profile)
            return;
        Profile value = edit ? *profile : Profile{};
        if (edit_profile(window_, store_, value, preferences_.language, edit || id == NewAdvanced))
            reload(value.id);
    } else if (id == RemoveSelected && profile) {
        std::wstring text =
            std::wstring(t(L"删除配置“", L"Remove profile “")) + profile->name + t(L"”？", L"”?");
        if (MessageBoxW(window_, text.c_str(), Product, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) ==
            IDYES) {
            std::wstring reason;
            if (!store_.remove(profile->id, reason))
                error(window_, reason);
            else
                reload();
        }
    } else if (id == ImportProfile) {
        auto path = choose_file(window_, false, ProfileFilter);
        if (!path)
            return;
        Profile value;
        std::wstring reason;
        if (!store_.import_connection(*path, value, reason))
            error(window_, reason);
        else
            reload(value.id);
    } else if (id == ExportProfile && profile) {
        auto path = choose_file(window_, true, ProfileFilter, L"bvpn", profile->name + L".bvpn");
        if (!path)
            return;
        std::wstring reason;
        if (!store_.export_connection(*path, *profile, reason))
            error(window_, reason);
    }
}
void Application::connect() {
    if (busy() || closing_)
        return;
    session_.reset();
    Profile profile;
    if (auto *existing = selected())
        profile = *existing;
    else {
        std::wstring gateway;
        if (!normalize_gateway(value(Servers), gateway)) {
            error(window_, t(L"请输入有效的 HTTPS 网关地址。", L"Enter a valid HTTPS gateway."));
            return;
        }
        profile.gateway = gateway;
        profile.name = gateway.substr(8);
        if (profile.name.size() > 100)
            profile.name.resize(100);
        auto base = profile.name;
        int suffix = 2;
        while (std::any_of(profiles_.begin(), profiles_.end(), [&](const auto &p) {
            return CompareStringOrdinal(p.name.c_str(), -1, profile.name.c_str(), -1, TRUE) == CSTR_EQUAL;
        }))
            profile.name = base + L" (" + std::to_wstring(suffix++) + L")";
        std::wstring reason;
        if (!store_.save(profile, reason)) {
            error(window_, reason);
            return;
        }
        reload(profile.id);
    }
    preferences_.selected = profile.id;
    save_preferences();
    if (!administrator()) {
        std::vector<wchar_t> module(32768);
        GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        std::wstring args = L"--data-dir " + quote_argument(store_.directory().wstring()) + L" --connect " +
                            quote_argument(wide(profile.id));
        SHELLEXECUTEINFOW execute{};
        execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        execute.hwnd = window_;
        execute.lpVerb = L"runas";
        execute.lpFile = module.data();
        execute.lpParameters = args.c_str();
        execute.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&execute)) {
            if (execute.hProcess)
                CloseHandle(execute.hProcess);
            shutdown();
        } else if (GetLastError() != ERROR_CANCELLED)
            error(window_, system_error(GetLastError()));
        return;
    }
    active_name_ = profile.name;
    active_gateway_ = profile.gateway;
    active_minimize_ = profile.minimize_on_connect;
    if (profile.secret_unavailable)
        log_.append(t(L"保存的凭据不属于当前 Windows 用户，请重新输入。",
                      L"Saved credentials are unavailable for this Windows user. Enter them again."));
    int level = profile.log_level >= 0 ? profile.log_level : preferences_.log_level;
    session_ = std::make_unique<Session>(std::move(profile), &store_, preferences_.language,
                                         [this](Event event) { receive(std::move(event)); });
    session_->set_log_level(level);
    state_ = State::Connecting;
    show_statistics({});
    log_.append(t(L"开始连接。", L"Starting connection."));
    if (!session_->start()) {
        session_.reset();
        state_ = State::Failed;
        error(window_, t(L"无法启动连接线程。", L"Could not start the connection worker."));
    }
    update_controls();
}
void Application::disconnect() {
    if (!session_)
        return;
    state_ = State::Disconnecting;
    session_->cancel();
    update_controls();
}
void Application::shutdown() {
    if (closing_)
        return;
    closing_ = true;
    if (busy()) {
        disconnect();
        return;
    }
    session_.reset();
    DestroyWindow(window_);
}
void Application::restore() {
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(GetLastActivePopup(window_));
}
void Application::minimize() {
    if (preferences_.minimize_to_tray && tray())
        ShowWindow(window_, SW_HIDE);
    else
        ShowWindow(window_, SW_MINIMIZE);
}
bool Application::tray(bool add) {
    if (!window_)
        return false;
    if (!tray_added_ && !add)
        return false;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window_;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = TrayMessage;
    icon.hIcon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(2), IMAGE_ICON, 16, 16, LR_SHARED));
    auto tip = std::wstring(Product) + L" — " + state_text(state_, preferences_.language);
    wcsncpy(icon.szTip, tip.c_str(), sizeof(icon.szTip) / sizeof(wchar_t) - 1);
    bool ok = Shell_NotifyIconW(tray_added_ ? NIM_MODIFY : NIM_ADD, &icon) != FALSE;
    if (ok && !tray_added_) {
        tray_added_ = true;
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
    }
    return ok;
}
void Application::remove_tray() {
    if (tray_added_) {
        NOTIFYICONDATAW icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window_;
        icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon);
        tray_added_ = false;
    }
}
void Application::tray_menu() {
    HMENU menu = CreatePopupMenu();
    menu_item(menu, Restore, t(L"显示窗口", L"Show window"));
    if (busy())
        menu_item(menu, Connect, t(L"断开连接", L"Disconnect"));
    menu_item(menu, ShowLog, t(L"查看日志", L"View log"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    menu_item(menu, Quit, t(L"退出", L"Quit"));
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (id)
        command(id);
}
void Application::receive(Event event) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (events_.size() >= 4096 && event.kind == Event::Kind::Log)
            return;
        events_.push_back(std::move(event));
    }
    PostMessageW(window_, SessionMessage, 0, 0);
}
void Application::show_statistics(const Statistics &stats) {
    const std::wstring values[] = {stats.ipv4,           stats.ipv6,        stats.dns,
                                   stats.tls_cipher,     stats.dtls_cipher, bytes(stats.downloaded),
                                   bytes(stats.uploaded)};
    for (int i = 0; i < 7; ++i)
        label(InfoValue + i, values[i].empty() ? L"—" : values[i]);
}
void Application::drain_events() {
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        events.swap(events_);
    }
    for (auto &event : events) {
        if (event.kind == Event::Kind::Log)
            log_.append(event.text);
        else if (event.kind == Event::Kind::Statistics)
            show_statistics(event.statistics);
        else if (event.kind == Event::Kind::ProfilesChanged)
            reload();
        else if (event.kind == Event::Kind::Prompt) {
            restore();
            show_prompt(window_, event.prompt, preferences_.language, [this] {
                return closing_ || !session_ || session_->finished() || state_ == State::Disconnecting;
            });
        } else {
            state_ = event.state;
            if (!event.text.empty())
                log_.append(event.text);
            if (event.terminal)
                log_.append(state_text(event.state, preferences_.language));
            update_controls();
            if (event.state == State::Connected && active_minimize_)
                minimize();
        }
    }
}
LRESULT Application::message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CTLCOLORSTATIC && GetDlgCtrlID(reinterpret_cast<HWND>(lparam)) != Status) {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (message == WM_CREATE) {
        create_controls();
        return 0;
    }
    if (message == SessionMessage) {
        drain_events();
        return 0;
    }
    if (message == AutoConnectMessage) {
        const auto wanted = auto_connect_;
        auto_connect_.clear();
        auto found =
            std::find_if(profiles_.begin(), profiles_.end(), [&](const auto &p) { return p.id == wanted; });
        if (found == profiles_.end())
            error(window_, t(L"所选配置不存在。", L"The selected profile does not exist."));
        else {
            SendMessageW(item(Servers), CB_SETCURSEL,
                         static_cast<WPARAM>(std::distance(profiles_.begin(), found)), 0);
            connect();
        }
        return 0;
    }
    if (message == WM_TIMER) {
        if (session_ && session_->finished()) {
            drain_events();
            session_.reset();
            update_controls();
            if (closing_) {
                DestroyWindow(window_);
                return 0;
            }
        }
        static unsigned tick = 0;
        if (++tick % 4 == 0 && session_)
            session_->request_statistics();
        return 0;
    }
    if (message == WM_COMMAND) {
        int id = LOWORD(wparam);
        if (id == Servers) {
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                // The edit text can still contain the previous item during CBN_SELCHANGE.
                LRESULT index = SendMessageW(item(Servers), CB_GETCURSEL, 0, 0);
                if (index >= 0 && static_cast<size_t>(index) < profiles_.size()) {
                    SendMessageW(item(Servers), CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                    preferences_.selected = profiles_[static_cast<size_t>(index)].id;
                    save_preferences();
                }
            }
            update_controls();
            return 0;
        }
        if (id == IDOK) {
            command(Connect);
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == 0)
            command(id);
        return 0;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR *>(lparam)->idFrom == Tabs &&
        reinterpret_cast<NMHDR *>(lparam)->code == TCN_SELCHANGE) {
        select_tab();
        return 0;
    }
    if (message == WM_CLOSE) {
        if (preferences_.minimize_instead_of_close)
            minimize();
        else
            shutdown();
        return 0;
    }
    if (message == WM_SIZE && wparam == SIZE_MINIMIZED && preferences_.minimize_to_tray) {
        minimize();
        return 0;
    }
    if (message == TrayMessage) {
        UINT action = LOWORD(lparam);
        if (action == NIN_SELECT || action == NIN_KEYSELECT || action == WM_LBUTTONDBLCLK)
            restore();
        else if (action == WM_CONTEXTMENU || action == WM_RBUTTONUP)
            tray_menu();
        return 0;
    }
    if (message == taskbar_created_ && taskbar_created_) {
        tray_added_ = false;
        tray(true);
        return 0;
    }
    if (message == WM_QUERYENDSESSION) {
        if (session_)
            session_->cancel();
        return TRUE;
    }
    if (message == WM_ENDSESSION && wparam) {
        shutdown();
        return 0;
    }
    if (message == WM_DESTROY) {
        remove_tray();
        RemovePropW(window_, instance_name_.c_str());
        PostQuitMessage(0);
        return 0;
    }
    return Window::message(message, wparam, lparam);
}
} // namespace bulijie
