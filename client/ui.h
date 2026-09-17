#pragma once
#include "session.h"
#include <commctrl.h>
#include <optional>

namespace bulijie::ui {
enum Control : int {
    Tabs = 100,
    Servers,
    Connect,
    EditProfile,
    ViewLog,
    Status,
    Gateway,
    MainIcon,
    ProfileName = 200,
    ProfileGateway,
    ProfileProtocol,
    ProfileUsername,
    ProfileGroup,
    ProfileCA,
    BrowseCA,
    ClearCA,
    ProfilePin,
    ClearPin,
    TokenType,
    TokenValue,
    ImportToken,
    InterfaceName,
    Script,
    BrowseScript,
    Certificate,
    BrowseCertificate,
    ClearCertificate,
    PrivateKey,
    BrowseKey,
    ClearKey,
    SystemStore,
    ResetStore,
    ReconnectTimeout,
    DTLSPeriod,
    LogLevel,
    MinimizeOnConnect,
    BatchMode,
    DisableUDP,
    UseProxy,
    Customize,
    ProfilePassword,
    PromptText = 300,
    PromptShowPassword,
    PromptDetails,
    PromptCopy,
    PromptAccept,
    LogText = 400,
    LogCopy,
    LogSelectAll,
    LogClear,
    LogAutoScroll,
    NewProfile = 1000,
    NewAdvanced,
    EditSelected,
    RemoveSelected,
    ImportProfile,
    ExportProfile,
    Quit,
    ShowLog,
    Minimize,
    Restore,
    MinimizeToTray,
    MinimizeOnClose,
    StartMinimized,
    SingleInstance,
    Chinese,
    English,
    LogError,
    LogInfo,
    LogDebug,
    LogTrace,
    Website,
    Updates,
    ReportIssue,
    License,
    About
};
class Window {
public:
    Window() = default;
    virtual ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    HWND handle() const {
        return window_;
    }
    int px(int value) const {
        return MulDiv(value, dpi_, 96);
    }
    HWND item(int id) const {
        return GetDlgItem(window_, id);
    }
    void label(int id, const std::wstring &value) const {
        SetWindowTextW(item(id), value.c_str());
    }
    std::wstring value(int id) const {
        return read_window_text(item(id));
    }
    bool checked(int id) const {
        return SendMessageW(item(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void check(int id, bool value) const {
        SendMessageW(item(id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    HWND control(const wchar_t *type, const std::wstring &text, DWORD style, int id, int x, int y, int width,
                 int height, DWORD extended = 0);
    HWND edit(const std::wstring &text, int id, int x, int y, int width, int height = 25, DWORD style = 0);
    HWND button(const wchar_t *text, int id, int x, int y, int width, int height = 27,
                DWORD style = BS_PUSHBUTTON);
    HWND combo(int id, int x, int y, int width, bool editable = false);
    void move(int id, int x, int y, int width, int height);

protected:
    bool create(HWND owner, const wchar_t *type, const std::wstring &title, int width, int height,
                DWORD style, DWORD extended = 0, HMENU menu = nullptr);
    virtual LRESULT message(UINT message, WPARAM wparam, LPARAM lparam);
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    int dpi_ = 96;

private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
};
class Dialog : public Window {
public:
    bool run(HWND owner, const wchar_t *type, const std::wstring &title, int width, int height);

protected:
    void finish(bool accepted);
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override;
    int initial_focus_ = 0;
    int default_button_ = IDOK;

private:
    bool accepted_ = false;
};
std::optional<std::filesystem::path> choose_file(HWND owner, bool save, const wchar_t *filter,
                                                 const wchar_t *extension = nullptr,
                                                 const std::wstring &initial = {});
void error(HWND owner, const std::wstring &message);
void add_choice(HWND combo, const std::wstring &label, LPARAM value = 0);
void select_choice(HWND combo, LPARAM value);
LPARAM selected_choice(HWND combo, LPARAM fallback = 0);
std::wstring bytes(uint64_t value);
std::wstring quote_argument(const std::wstring &argument);
std::wstring state_text(State state, Language language);
bool edit_profile(HWND owner, ProfileStore &store, Profile &profile, Language language, bool advanced);
void show_prompt(HWND owner, const std::shared_ptr<Prompt> &prompt, Language language,
                 const std::function<bool()> &canceled);
void show_about(HWND owner, Language language, bool license);
class LogWindow : public Window {
public:
    void show(HWND owner, Language language);
    void append(const std::wstring &line);
    void language(Language value);

private:
    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) override;
    void update_text();
    std::wstring content_;
    Language language_ = Language::Chinese;
    bool auto_scroll_ = true;
};
} // namespace bulijie::ui
